#include "shutdown_watch.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>

// windows.h는 매크로(ERROR, min, max 등)가 앞 헤더와 겹치지 않게 마지막에 넣습니다.
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

struct WatchState {
  std::mutex mutex;
  QString path;        // 기록 파일. 비어 있으면 남기지 않습니다
  QString last_step;   // 마지막으로 남긴 단계
  QString limit_what;  // 지금 제한 시간을 건 일
  std::atomic<int64_t> deadline_ms{0};  // 0이면 감시하지 않음
  std::once_flag watcher_once;
};

// 감시 스레드는 main을 나간 뒤(정적 객체 소멸 중)에도 돌므로 소멸하지 않는 객체에 둡니다.
WatchState& State() {
  static WatchState* const state = new WatchState;
  return *state;
}

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// 종료 처리 중인 스레드가 드라이버 호출에서 멈춰 있어도 끝나도록 정리 없이 프로세스를 끝냅니다.
// 녹화 파일은 이 전 단계(쓰기 스레드 끝)에서 닫히며, 그 단계에서 멈춘 경우에만 마지막 파일이 덜 쓰일 수 있습니다.
[[noreturn]] void TerminateNow() {
#ifdef _WIN32
  TerminateProcess(GetCurrentProcess(), 3);
#endif
  std::_Exit(3);
}

void Append(const QString& line) {
  if (State().path.isEmpty()) return;
  QFile file(State().path);
  if (!file.open(QIODevice::Append | QIODevice::Text)) return;
  file.write((QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz")) + QLatin1Char(' ') + line +
              QLatin1Char('\n'))
                 .toUtf8());
}

void WatchLoop() {
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const int64_t deadline = State().deadline_ms.load();
    if (deadline == 0 || NowMs() < deadline) continue;
    {
      std::lock_guard<std::mutex> lock(State().mutex);
      Append(QStringLiteral("제한 시간 안에 끝나지 않아 프로세스를 끝냄(%1). 마지막 단계: %2")
                 .arg(State().limit_what, State().last_step.isEmpty() ? QStringLiteral("-") : State().last_step));
    }
    TerminateNow();
  }
}

}  // namespace

void InitShutdownLog() {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (dir.isEmpty() || !QDir().mkpath(dir)) return;
  std::lock_guard<std::mutex> lock(State().mutex);
  State().path = QDir(dir).filePath(QStringLiteral("shutdown_log.txt"));
  // 오래 쓰면 커지므로 1MB를 넘으면 비우고 새로 씁니다.
  if (QFile(State().path).size() > 1024 * 1024) QFile::remove(State().path);
}

void LogShutdownStep(const QString& step) {
  {
    std::lock_guard<std::mutex> lock(State().mutex);
    State().last_step = step;
    Append(step);
  }
  // 앱 로그에도 남겨 한 파일에서 종료까지 따라갈 수 있게 합니다. 제한 시간 감시 스레드는 프로세스를 바로 끝내므로 여기를 거치지 않습니다.
  qInfo("종료 단계: %s", qPrintable(step));
}

void ArmShutdownLimit(int limit_ms, const QString& what) {
  std::call_once(State().watcher_once, [] { std::thread(WatchLoop).detach(); });
  {
    std::lock_guard<std::mutex> lock(State().mutex);
    State().limit_what = what;
  }
  State().deadline_ms.store(NowMs() + limit_ms);
}

void DisarmShutdownLimit() { State().deadline_ms.store(0); }

bool ShutdownLimitArmed() { return State().deadline_ms.load() != 0; }
