#ifndef HWLAB_APP_APP_LOG_H_
#define HWLAB_APP_APP_LOG_H_

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QtLogging>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

// Qt 메시지(qDebug, qInfo, qWarning, qCritical, QML의 console.*)를 콘솔과 날짜별 로그 파일에 남깁니다.
//
// 메시지를 낸 스레드는 대기열에 넣기만 하고, 콘솔과 파일에는 쓰기 스레드 하나만 씁니다. 그래서 파일이 느리거나 콘솔이 멈춰도
// (Windows 콘솔은 글자를 끌어 선택하면 쓰는 쪽이 멈춥니다) 메시지를 낸 스레드는 기다리지 않습니다. 대기열이 가득 차면 새 줄을
// 버리고 버린 수를 로그에 남깁니다. 촬영 스레드(platform의 Capture)는 Qt 메시지를 내지 않으며, 앱의 호출 지점은 화면 스레드와
// 작업 스레드입니다.
namespace app_log {

constexpr int kKeepDays = 14;                       // 오늘을 포함해 남기는 날짜 수
constexpr qint64 kMaxDailyBytes = 20 * 1024 * 1024; // 하루 파일 하나의 상한. 넘으면 그날은 더 남기지 않습니다
constexpr size_t kQueueCapacity = 4096;             // 쓰기가 밀릴 때 쌓아 두는 줄 수

// ---- 순수한 부분 ----

/** 로그 파일 이름입니다(예: hwlab_app-2026-11-05.log). */
QString FileNameFor(const QDate& date);

/** 로그 파일 이름에서 날짜를 읽습니다. 로그 파일 이름이 아니면 nullopt입니다. */
std::optional<QDate> DateOfFileName(const QString& name);

/**
 * 지울 로그 파일입니다. 오늘을 포함해 keep_days일보다 오래된 로그 파일만 고르고, 로그 파일 이름이 아닌 파일은 건드리지 않습니다.
 */
QStringList ExpiredFileNames(const QStringList& names, const QDate& today, int keep_days);

/** 메시지 하나입니다. 서식은 쓰기 스레드가 입히므로 메시지를 낸 스레드는 복사만 합니다. */
struct Entry {
  QDateTime when;
  QtMsgType type = QtInfoMsg;
  QString category;  // 기본 범주는 비어 있습니다
  QString message;
};

/** 파일 한 줄입니다: "2026-11-05T14:03:22.123 W [1234] 메시지". 여러 줄 메시지는 이어지는 줄을 들여 씁니다. */
QString FormatLine(const Entry& entry, qint64 pid);

/** 콘솔에 쓰는 글입니다. Qt 기본 출력처럼 메시지만이고, 기본 범주가 아니면 "범주: "를 앞에 붙입니다. */
QString ConsoleText(const Entry& entry);

// ---- 대기열 ----

/** 여러 스레드가 넣고 스레드 하나가 꺼내는 대기열입니다. 넣을 때 입출력을 하지 않고 가득 차면 버립니다. */
class MessageQueue {
 public:
  struct Batch {
    std::vector<Entry> entries;
    size_t dropped = 0;  // 이번에 꺼내기 전까지 가득 차서 버린 수
    bool closed = false; // 닫혔고 더 꺼낼 것이 없음
  };

  explicit MessageQueue(size_t capacity) : capacity_(capacity) {}

  /** 넣었으면 true, 가득 찼거나 닫혔으면 false입니다. 어느 스레드에서나 부릅니다. */
  bool Push(Entry entry);

  /** 줄이 생기거나 닫힐 때까지 기다린 뒤 쌓인 줄을 모두 꺼냅니다. */
  Batch Take();

  /** 더 받지 않습니다. 쌓인 줄은 꺼낼 수 있습니다. */
  void Close();

 private:
  const size_t capacity_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Entry> entries_;
  size_t dropped_ = 0;
  bool closed_ = false;
};

// ---- 쓰기 ----

struct SinkOptions {
  QString dir;                                  // 로그 폴더. 비어 있으면 파일에 쓰지 않습니다
  std::function<void(const QString&)> console;  // 콘솔에 쓰는 함수. 쓰기 스레드에서 부릅니다
  qint64 pid = 0;
  int keep_days = kKeepDays;
  qint64 max_daily_bytes = kMaxDailyBytes;
  size_t queue_capacity = kQueueCapacity;
};

/** 대기열을 비우는 쓰기 스레드를 가진 로그 출력입니다. */
class Sink {
 public:
  explicit Sink(SinkOptions options);
  ~Sink();
  Sink(const Sink&) = delete;
  Sink& operator=(const Sink&) = delete;

  /** 메시지를 대기열에 넣습니다. 어느 스레드에서나 부르며 입출력을 기다리지 않습니다. */
  void Submit(Entry entry);

  /** 지금까지 넣은 줄이 콘솔과 파일에 쓰일 때까지 최대 timeout_ms 기다립니다. 쓰였으면 true입니다. */
  bool Flush(int timeout_ms);

  /** 남은 줄을 쓰고 쓰기 스레드를 끝냅니다. 최대 timeout_ms 기다리고, 넘으면 스레드를 놓아 둡니다. */
  void Stop(int timeout_ms);

 private:
  void Run();
  void Write(const MessageQueue::Batch& batch);
  void AppendToFile(const QDate& date, const QByteArray& bytes);
  void RemoveExpiredFiles() const;

  const SinkOptions options_;
  MessageQueue queue_;
  std::mutex mutex_;
  std::condition_variable progress_;
  std::atomic<size_t> submitted_{0};
  size_t written_ = 0;
  bool finished_ = false;
  std::thread thread_;
  QList<QDate> capped_;  // 상한을 넘겨 더 쓰지 않는 날짜(쓰기 스레드만 만짐)
};

// ---- 앱에 거는 부분 ----

/** 앱 데이터 폴더 아래의 로그 폴더입니다. */
QString LogDirectory(const QString& data_dir);

/**
 * Qt 메시지 처리기를 걸어 이후 메시지를 콘솔과 dir의 날짜별 파일에 남깁니다. 앱 이름을 정한 뒤 한 번 부릅니다.
 * 콘솔은 표준 오류이고 Windows는 디버거 출력에도 보냅니다. 폴더를 만들지 못하면 콘솔에만 남기고 그 사실을 알립니다.
 */
void Install(const QString& dir);

/** 처리기를 원래대로 돌리고, 남은 줄을 쓴 뒤 쓰기 스레드를 끝냅니다(최대 2초). main을 나갈 때 부릅니다. */
void Shutdown();

/** 지금 쓰는 로그 파일 경로입니다. Install 전이면 빈 문자열입니다. */
QString CurrentFilePath();

/** --help 끝에 붙는, 앱이 남기는 기록 파일의 위치와 내용입니다. */
QString HelpFooter(const QString& data_dir);

}  // namespace app_log

#endif  // HWLAB_APP_APP_LOG_H_
