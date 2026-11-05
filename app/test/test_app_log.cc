#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>

#include "app_log.h"

namespace {

using app_log::Entry;

Entry Info(const QString& message, const QDateTime& when = QDateTime::currentDateTime()) {
  return Entry{when, QtInfoMsg, QString(), message};
}

QStringList ReadLines(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
  if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
  return lines;
}

QString TodayFile(const QString& dir) { return QDir(dir).filePath(app_log::FileNameFor(QDate::currentDate())); }

QDateTime At(int year, int month, int day, int hour, int minute, int second, int ms) {
  return QDateTime(QDate(year, month, day), QTime(hour, minute, second, ms));
}

}  // namespace

class AppLogTest : public QObject {
  Q_OBJECT

 private slots:
  void FileNameIsDatedAndReadsBack() {
    const QDate date(2026, 11, 5);
    QCOMPARE(app_log::FileNameFor(date), QStringLiteral("hwlab_app-2026-11-05.log"));
    QCOMPARE(app_log::DateOfFileName(QStringLiteral("hwlab_app-2026-11-05.log")), std::optional<QDate>(date));
    for (const char* other : {"shutdown_log.txt", "hwlab_app-2026-13-40.log", "hwlab_app-2026-1-5.log",
                              "hwlab_app-2026-11-05.log.bak", "hwlab_app-.log", "hwlab_diagnose.log"}) {
      QVERIFY2(!app_log::DateOfFileName(QString::fromLatin1(other)), other);
    }
  }

  // 오늘을 포함해 14일을 두므로 13일 전 파일은 남고 14일 전 파일부터 지웁니다. 로그 파일이 아닌 이름과 미래 날짜는 건드리지 않습니다.
  void ExpiresOnlyOldLogFiles() {
    const QDate today(2026, 11, 5);
    const QStringList names{QStringLiteral("hwlab_app-2026-11-05.log"), QStringLiteral("hwlab_app-2026-10-23.log"),
                            QStringLiteral("hwlab_app-2026-10-22.log"), QStringLiteral("hwlab_app-2025-01-01.log"),
                            QStringLiteral("hwlab_app-2026-11-30.log"), QStringLiteral("shutdown_log.txt"),
                            QStringLiteral("hwlab_app-notes.log")};
    QCOMPARE(app_log::ExpiredFileNames(names, today, 14),
             (QStringList{QStringLiteral("hwlab_app-2026-10-22.log"), QStringLiteral("hwlab_app-2025-01-01.log")}));
    QCOMPARE(app_log::ExpiredFileNames(names, today, 1000), QStringList());
  }

  void FormatsOneLinePerMessage() {
    const QDateTime when = At(2026, 11, 5, 14, 3, 22, 123);
    QCOMPARE(app_log::FormatLine(Entry{when, QtWarningMsg, QString(), QStringLiteral("분석 판: 없음")}, 1234),
             QStringLiteral("2026-11-05T14:03:22.123 W [1234] 분석 판: 없음"));
    QCOMPARE(app_log::FormatLine(Entry{when, QtCriticalMsg, QStringLiteral("qml"), QStringLiteral("x")}, 7),
             QStringLiteral("2026-11-05T14:03:22.123 E [7] qml: x"));
    QCOMPARE(app_log::FormatLine(Entry{when, QtDebugMsg, QString(), QStringLiteral("d")}, 7).mid(24, 1), QStringLiteral("D"));
    QCOMPARE(app_log::FormatLine(Entry{when, QtFatalMsg, QString(), QStringLiteral("f")}, 7).mid(24, 1), QStringLiteral("F"));
  }

  // 여러 줄 메시지(분석 프로그램의 예외 기록)는 이어지는 줄을 들여 써서, 시각으로 시작하는 줄만 찾아도 메시지 수를 셀 수 있습니다.
  void ContinuationLinesAreIndented() {
    const QDateTime when = At(2026, 11, 5, 14, 3, 22, 123);
    QCOMPARE(app_log::FormatLine(Entry{when, QtWarningMsg, QString(), QStringLiteral("첫 줄\r\n둘째 줄\n셋째 줄\n\n")}, 1),
             QStringLiteral("2026-11-05T14:03:22.123 W [1] 첫 줄\n    둘째 줄\n    셋째 줄"));
  }

  void ConsoleTextIsPlainMessage() {
    const QDateTime when = At(2026, 11, 5, 14, 3, 22, 123);
    QCOMPARE(app_log::ConsoleText(Entry{when, QtWarningMsg, QString(), QStringLiteral("분석 판: 없음\n")}),
             QStringLiteral("분석 판: 없음"));
    QCOMPARE(app_log::ConsoleText(Entry{when, QtWarningMsg, QStringLiteral("qml"), QStringLiteral("x")}), QStringLiteral("qml: x"));
  }

  // 가득 차면 넣지 못하고 버린 수를 셉니다. 닫으면 새 줄은 받지 않고, 쌓인 줄은 꺼낼 수 있습니다.
  void QueueIsBoundedAndCountsDrops() {
    app_log::MessageQueue queue(3);
    for (int i = 0; i < 3; ++i) QVERIFY(queue.Push(Info(QString::number(i))));
    QVERIFY(!queue.Push(Info(QStringLiteral("3"))));
    QVERIFY(!queue.Push(Info(QStringLiteral("4"))));
    auto batch = queue.Take();
    QCOMPARE(batch.entries.size(), size_t(3));
    QCOMPARE(batch.dropped, size_t(2));
    QVERIFY(!batch.closed);

    QVERIFY(queue.Push(Info(QStringLiteral("5"))));
    queue.Close();
    QVERIFY(!queue.Push(Info(QStringLiteral("6"))));
    batch = queue.Take();
    QCOMPARE(batch.entries.size(), size_t(1));
    QCOMPARE(batch.dropped, size_t(0));
    QVERIFY(batch.closed);
  }

  void TakeWaitsForAnEntry() {
    app_log::MessageQueue queue(8);
    std::thread producer([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      queue.Push(Info(QStringLiteral("late")));
    });
    const auto batch = queue.Take();
    producer.join();
    QCOMPARE(batch.entries.size(), size_t(1));
    QCOMPARE(batch.entries[0].message, QStringLiteral("late"));
  }

  // 줄은 날짜별 파일로 가고, 자정을 넘긴 줄은 새 날짜 파일에 갑니다.
  void WritesLinesToTheFileOfTheirDate() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    app_log::SinkOptions options;
    options.dir = dir.path();
    options.pid = 42;
    {
      app_log::Sink sink(options);
      sink.Submit(Info(QStringLiteral("자정 전"), At(2026, 11, 4, 23, 59, 59, 999)));
      sink.Submit(Info(QStringLiteral("자정 뒤"), At(2026, 11, 5, 0, 0, 0, 1)));
      sink.Stop(5000);
    }
    QCOMPARE(ReadLines(QDir(dir.path()).filePath(QStringLiteral("hwlab_app-2026-11-04.log"))),
             QStringList{QStringLiteral("2026-11-04T23:59:59.999 I [42] 자정 전")});
    QCOMPARE(ReadLines(QDir(dir.path()).filePath(QStringLiteral("hwlab_app-2026-11-05.log"))),
             QStringList{QStringLiteral("2026-11-05T00:00:00.001 I [42] 자정 뒤")});
  }

  // 같은 날 다시 켜면 이어서 씁니다(덮어쓰지 않습니다).
  void AppendsWhenStartedAgainOnTheSameDay() {
    QTemporaryDir dir;
    app_log::SinkOptions options;
    options.dir = dir.path();
    for (const char* text : {"첫 실행", "다음 실행"}) {
      app_log::Sink sink(options);
      sink.Submit(Info(QString::fromUtf8(text)));
      sink.Stop(5000);
    }
    const QStringList lines = ReadLines(TodayFile(dir.path()));
    QCOMPARE(lines.size(), 2);
    QVERIFY(lines[0].endsWith(QStringLiteral("첫 실행")));
    QVERIFY(lines[1].endsWith(QStringLiteral("다음 실행")));
  }

  void RemovesExpiredFilesAtStartOnly() {
    QTemporaryDir dir;
    const QDate today = QDate::currentDate();
    const QStringList files{app_log::FileNameFor(today.addDays(-30)), app_log::FileNameFor(today.addDays(-14)),
                            app_log::FileNameFor(today.addDays(-13)), QStringLiteral("shutdown_log.txt")};
    for (const QString& name : files) {
      QFile file(QDir(dir.path()).filePath(name));
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("x");
    }
    app_log::SinkOptions options;
    options.dir = dir.path();
    app_log::Sink sink(options);
    sink.Submit(Info(QStringLiteral("시작")));
    QVERIFY(sink.Flush(5000));
    sink.Stop(5000);
    QStringList left = QDir(dir.path()).entryList(QDir::Files, QDir::Name);
    left.removeAll(app_log::FileNameFor(today));
    QCOMPARE(left, (QStringList{app_log::FileNameFor(today.addDays(-13)), QStringLiteral("shutdown_log.txt")}));
  }

  // 하루 파일이 상한을 넘으면 알림 한 줄을 남기고 그날은 더 쓰지 않습니다.
  void StopsWritingAfterDailyCap() {
    QTemporaryDir dir;
    app_log::SinkOptions options;
    options.dir = dir.path();
    options.max_daily_bytes = 300;
    app_log::Sink sink(options);
    for (int i = 0; i < 20; ++i) {
      sink.Submit(Info(QStringLiteral("줄 %1 가나다라마바사아자차카타파하").arg(i)));
      QVERIFY(sink.Flush(5000));
    }
    sink.Stop(5000);
    const QStringList lines = ReadLines(TodayFile(dir.path()));
    QVERIFY(lines.size() < 20);
    QVERIFY2(lines.last().contains(QStringLiteral("더 남기지 않습니다")), qPrintable(lines.last()));
    QCOMPARE(lines.filter(QStringLiteral("더 남기지 않습니다")).size(), 1);
    QVERIFY(QFileInfo(TodayFile(dir.path())).size() < 300 + 200);
    QVERIFY(!lines.join(QLatin1Char('\n')).contains(QStringLiteral("줄 19 ")));
  }

  // 파일에만 남기는 줄은 콘솔에 보내지 않고 파일에는 범주 없이 남습니다.
  void FileOnlyEntriesSkipTheConsole() {
    QTemporaryDir dir;
    QString console;
    app_log::SinkOptions options;
    options.dir = dir.path();
    options.console = [&](const QString& text) { console += text; };
    app_log::Sink sink(options);
    Entry detail = Info(QStringLiteral("시작 정보"));
    detail.file_only = true;
    sink.Submit(detail);
    sink.Submit(Info(QStringLiteral("경고도 아닌 일반 줄")));
    sink.Stop(5000);
    QCOMPARE(console, QStringLiteral("경고도 아닌 일반 줄\n"));
    QCOMPARE(ReadLines(TodayFile(dir.path())).size(), 2);
  }

  // 콘솔이 멈춰 있어도(쓰기 스레드가 콘솔에서 돌아오지 않아도) 메시지를 내는 쪽은 기다리지 않습니다. 대기열은 정한 크기까지만
  // 쌓고 나머지는 버리며, 버린 수를 알립니다. 촬영과 화면 스레드가 로그 때문에 멈추지 않는다는 근거입니다.
  void ProducersDoNotWaitForAStuckConsole() {
    std::mutex mutex;
    QString delivered;
    std::atomic<bool> entered{false};
    std::promise<void> release;
    const std::shared_future<void> gate = release.get_future().share();
    app_log::SinkOptions options;
    options.queue_capacity = 100;
    options.console = [&](const QString& text) {
      entered = true;
      gate.wait();
      std::lock_guard<std::mutex> lock(mutex);
      delivered += text;
    };
    app_log::Sink sink(options);
    sink.Submit(Info(QStringLiteral("first")));
    QElapsedTimer wait;
    wait.start();
    while (!entered && wait.elapsed() < 5000) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    QVERIFY(entered);

    QElapsedTimer timer;
    timer.start();
    std::vector<std::thread> producers;
    for (int t = 0; t < 4; ++t) {
      producers.emplace_back([&sink, t] {
        for (int i = 0; i < 2500; ++i) sink.Submit(Info(QStringLiteral("m %1 %2").arg(t).arg(i)));
      });
    }
    for (auto& producer : producers) producer.join();
    QVERIFY2(timer.elapsed() < 1000, qPrintable(QString::number(timer.elapsed())));

    release.set_value();
    sink.Stop(5000);
    const QStringList lines = delivered.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines.filter(QRegularExpression(QStringLiteral("^m "))).size(), 100);
    QVERIFY2(delivered.contains(QStringLiteral("로그 대기열이 가득 차 9900줄을 버렸습니다")), qPrintable(delivered.right(200)));
  }

  void FlushWaitsForWrittenAndTimesOutWhenStuck() {
    QTemporaryDir dir;
    app_log::SinkOptions options;
    options.dir = dir.path();
    {
      app_log::Sink sink(options);
      sink.Submit(Info(QStringLiteral("치명적 오류 직전")));
      QVERIFY(sink.Flush(5000));
      QCOMPARE(ReadLines(TodayFile(dir.path())).size(), 1);
    }
    std::promise<void> release;
    const std::shared_future<void> gate = release.get_future().share();
    app_log::SinkOptions stuck;
    stuck.console = [&](const QString&) { gate.wait(); };
    app_log::Sink sink(stuck);
    sink.Submit(Info(QStringLiteral("x")));
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!sink.Flush(100));
    QVERIFY(timer.elapsed() < 1000);
    release.set_value();
    sink.Stop(5000);
  }

  // Qt 메시지 처리기를 거는 경로 전체입니다. 여러 스레드에서 낸 메시지가 모두 파일에 시각과 함께 남고, 끝낸 뒤의 메시지는 원래 처리기로 갑니다.
  void InstalledHandlerWritesMessagesFromAnyThread() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logs = app_log::LogDirectory(dir.path());
    app_log::Install(logs);
    QCOMPARE(app_log::CurrentFilePath(), QDir(logs).filePath(app_log::FileNameFor(QDate::currentDate())));
    qInfo("메인 스레드 메시지");
    app_log::InfoToFileOnly(QStringLiteral("파일에만 남는 줄"));
    std::vector<std::thread> threads;
    for (int t = 0; t < 3; ++t) {
      threads.emplace_back([t] {
        for (int i = 0; i < 20; ++i) qWarning("스레드 %d 줄 %d", t, i);
      });
    }
    for (auto& thread : threads) thread.join();
    app_log::Shutdown();

    const QStringList lines = ReadLines(TodayFile(logs));
    QCOMPARE(lines.size(), 62);
    QCOMPARE(lines.filter(QStringLiteral(" I [")).size(), 2);
    QCOMPARE(lines.filter(QRegularExpression(QStringLiteral(R"( I \[\d+\] 파일에만 남는 줄$)"))).size(), 1);
    QCOMPARE(lines.filter(QStringLiteral(" W [")).size(), 60);
    QCOMPARE(lines.filter(QRegularExpression(QStringLiteral(R"(^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3} I \[\d+\] 메인 스레드 메시지$)"))).size(), 1);
    QVERIFY(app_log::CurrentFilePath().isEmpty() == false);
  }

  void HelpFooterNamesEveryRecordFile() {
    const QString footer = app_log::HelpFooter(QStringLiteral("/data/hwlab"));
    for (const char* piece : {"hwlab_app-<날짜>.log", "shutdown_log.txt", "camera_restore_log.txt", "analyzer_log.txt", "14일", "20MB"}) {
      QVERIFY2(footer.contains(QString::fromUtf8(piece)), piece);
    }
    QVERIFY(footer.contains(QDir::toNativeSeparators(QStringLiteral("/data/hwlab/logs"))));
  }
};

QTEST_GUILESS_MAIN(AppLogTest)
#include "test_app_log.moc"
