#include "app_log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <atomic>
#include <chrono>
#include <iterator>

#include "console_output.h"

namespace app_log {

namespace {

constexpr char kPrefix[] = "hwlab_app-";
constexpr char kSuffix[] = ".log";

QChar LevelChar(QtMsgType type) {
  switch (type) {
    case QtDebugMsg: return QLatin1Char('D');
    case QtInfoMsg: return QLatin1Char('I');
    case QtWarningMsg: return QLatin1Char('W');
    case QtCriticalMsg: return QLatin1Char('E');
    case QtFatalMsg: return QLatin1Char('F');
  }
  return QLatin1Char('?');
}

QString WithoutTrailingNewlines(QString text) {
  while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char('\r'))) text.chop(1);
  return text;
}

std::atomic<Sink*> g_sink{nullptr};
QtMessageHandler g_previous_handler = nullptr;
QString g_dir;  // 파일에 쓰는 폴더. 비어 있으면 파일에 쓰지 않습니다

void Handler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
  Sink* sink = g_sink.load(std::memory_order_acquire);
  if (sink == nullptr) return;
  QString category;
  if (context.category != nullptr && qstrcmp(context.category, "default") != 0) category = QString::fromUtf8(context.category);
  sink->Submit(Entry{QDateTime::currentDateTime(), type, category, message});
  // qFatal은 처리기가 돌아오면 바로 프로세스를 끝내므로, 이 줄까지는 쓰인 뒤에 돌아갑니다.
  if (type == QtFatalMsg) sink->Flush(2000);
}

}  // namespace

QString FileNameFor(const QDate& date) {
  return QLatin1String(kPrefix) + date.toString(Qt::ISODate) + QLatin1String(kSuffix);
}

std::optional<QDate> DateOfFileName(const QString& name) {
  if (!name.startsWith(QLatin1String(kPrefix)) || !name.endsWith(QLatin1String(kSuffix))) return std::nullopt;
  const QDate date = QDate::fromString(name.mid(sizeof(kPrefix) - 1, 10), Qt::ISODate);
  if (!date.isValid() || FileNameFor(date) != name) return std::nullopt;
  return date;
}

QStringList ExpiredFileNames(const QStringList& names, const QDate& today, int keep_days) {
  QStringList expired;
  for (const QString& name : names) {
    const auto date = DateOfFileName(name);
    if (date && date->daysTo(today) >= keep_days) expired << name;
  }
  return expired;
}

QString FormatLine(const Entry& entry, qint64 pid) {
  QString text = entry.when.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz")) + QLatin1Char(' ') + LevelChar(entry.type) +
                 QStringLiteral(" [%1] ").arg(pid) + (entry.category.isEmpty() ? QString() : entry.category + QStringLiteral(": ")) +
                 WithoutTrailingNewlines(entry.message);
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  text.replace(QLatin1Char('\n'), QStringLiteral("\n    "));
  return text;
}

QString ConsoleText(const Entry& entry) {
  return (entry.category.isEmpty() ? QString() : entry.category + QStringLiteral(": ")) + WithoutTrailingNewlines(entry.message);
}

bool MessageQueue::Push(Entry entry) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) return false;
    if (entries_.size() >= capacity_) {
      ++dropped_;
      return false;
    }
    entries_.push_back(std::move(entry));
  }
  wake_.notify_one();
  return true;
}

MessageQueue::Batch MessageQueue::Take() {
  std::unique_lock<std::mutex> lock(mutex_);
  wake_.wait(lock, [this] { return !entries_.empty() || closed_; });
  Batch batch;
  batch.entries.assign(std::make_move_iterator(entries_.begin()), std::make_move_iterator(entries_.end()));
  entries_.clear();
  batch.dropped = dropped_;
  dropped_ = 0;
  batch.closed = closed_;
  return batch;
}

void MessageQueue::Close() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
  }
  wake_.notify_all();
}

Sink::Sink(SinkOptions options) : options_(std::move(options)), queue_(options_.queue_capacity) {
  thread_ = std::thread([this] { Run(); });
}

Sink::~Sink() {
  queue_.Close();
  if (thread_.joinable()) thread_.join();
}

void Sink::Submit(Entry entry) {
  if (queue_.Push(std::move(entry))) submitted_.fetch_add(1);
}

bool Sink::Flush(int timeout_ms) {
  const size_t target = submitted_.load();
  std::unique_lock<std::mutex> lock(mutex_);
  return progress_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return written_ >= target || finished_; });
}

void Sink::Stop(int timeout_ms) {
  queue_.Close();
  std::unique_lock<std::mutex> lock(mutex_);
  const bool done = progress_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return finished_; });
  lock.unlock();
  if (!thread_.joinable()) return;
  if (done) {
    thread_.join();
  } else {
    thread_.detach();
  }
}

void Sink::Run() {
  RemoveExpiredFiles();
  for (;;) {
    const MessageQueue::Batch batch = queue_.Take();
    Write(batch);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      written_ += batch.entries.size();
      if (batch.closed) finished_ = true;
    }
    progress_.notify_all();
    if (batch.closed) return;
  }
}

void Sink::Write(const MessageQueue::Batch& batch) {
  std::vector<Entry> entries = batch.entries;
  if (batch.dropped > 0) {
    entries.push_back(Entry{QDateTime::currentDateTime(), QtWarningMsg, QString(),
                            QStringLiteral("로그 대기열이 가득 차 %1줄을 버렸습니다").arg(batch.dropped)});
  }
  if (entries.empty()) return;
  if (options_.console) {
    QString text;
    for (const Entry& entry : entries) text += ConsoleText(entry) + QLatin1Char('\n');
    options_.console(text);
  }
  if (options_.dir.isEmpty()) return;
  QDate date;
  QByteArray chunk;
  for (const Entry& entry : entries) {
    if (entry.when.date() != date && !chunk.isEmpty()) {
      AppendToFile(date, chunk);
      chunk.clear();
    }
    date = entry.when.date();
    chunk += (FormatLine(entry, options_.pid) + QLatin1Char('\n')).toUtf8();
  }
  AppendToFile(date, chunk);
}

void Sink::AppendToFile(const QDate& date, const QByteArray& bytes) {
  if (capped_.contains(date)) return;
  QFile file(QDir(options_.dir).filePath(FileNameFor(date)));
  QByteArray out = bytes;
  if (file.size() >= options_.max_daily_bytes) {
    capped_.append(date);
    out = (FormatLine(Entry{QDateTime::currentDateTime(), QtWarningMsg, QString(),
                            QStringLiteral("로그가 %1MB를 넘어 이 날짜의 로그는 더 남기지 않습니다")
                                .arg(options_.max_daily_bytes / (1024 * 1024))},
                      options_.pid) +
           QLatin1Char('\n'))
              .toUtf8();
  }
  if (!file.open(QIODevice::Append | QIODevice::Text)) return;
  file.write(out);
}

void Sink::RemoveExpiredFiles() const {
  if (options_.dir.isEmpty()) return;
  QDir dir(options_.dir);
  const QStringList names = dir.entryList({QStringLiteral("hwlab_app-*.log")}, QDir::Files);
  for (const QString& name : ExpiredFileNames(names, QDate::currentDate(), options_.keep_days)) dir.remove(name);
}

QString LogDirectory(const QString& data_dir) { return QDir(data_dir).filePath(QStringLiteral("logs")); }

void Install(const QString& dir) {
  if (g_sink.load() != nullptr) return;
  const bool usable = !dir.isEmpty() && QDir().mkpath(dir);
  SinkOptions options;
  options.dir = usable ? dir : QString();
  options.pid = QCoreApplication::applicationPid();
  options.console = [](const QString& text) {
    WriteStderr(text);
    WriteToDebugger(text);
  };
  g_dir = options.dir;
  // 처리기가 어느 스레드에서 불려도 쓸 수 있도록 지우지 않습니다. 정리 중(정적 객체 소멸)에 온 메시지도 받기 위함입니다.
  g_sink.store(new Sink(std::move(options)), std::memory_order_release);
  g_previous_handler = qInstallMessageHandler(Handler);
  if (!usable) qWarning("로그 폴더를 만들지 못해 파일에는 남기지 않습니다: %s", qUtf8Printable(dir));
}

void Shutdown() {
  Sink* sink = g_sink.exchange(nullptr);
  if (sink == nullptr) return;
  qInstallMessageHandler(g_previous_handler);
  sink->Stop(2000);
}

QString CurrentFilePath() {
  if (g_dir.isEmpty()) return QString();
  return QDir(g_dir).filePath(FileNameFor(QDate::currentDate()));
}

QString HelpFooter(const QString& data_dir) {
  const auto native = [&](const QString& name) { return QDir::toNativeSeparators(QDir(data_dir).filePath(name)); };
  return QStringLiteral(
             "기록 파일\n"
             "  로그 폴더        %1\n"
             "                   hwlab_app-<날짜>.log에 시각과 함께 남깁니다. 최근 %2일을 두고 하루 %3MB까지 씁니다\n"
             "  종료 단계        %4\n"
             "  카메라 복원      %5\n"
             "  분석 출력        %6\n"
             "자세한 설명은 docs/debugging.md에 있습니다.\n")
      .arg(QDir::toNativeSeparators(LogDirectory(data_dir)))
      .arg(kKeepDays)
      .arg(kMaxDailyBytes / (1024 * 1024))
      .arg(native(QStringLiteral("shutdown_log.txt")), native(QStringLiteral("camera_restore_log.txt")),
           QDir::toNativeSeparators(QStringLiteral("<촬영 폴더>/perfusion/<판>/analyzer_log.txt")));
}

}  // namespace app_log
