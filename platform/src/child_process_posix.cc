#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

#include "hwlab/platform/child_process.h"
#include "output_lines.h"

extern char** environ;

namespace hwlab::platform {

namespace {

void SetError(std::string* error, const std::string& text) {
  if (error != nullptr) *error = text;
}

void CloseFd(int* fd) {
  if (*fd >= 0) {
    close(*fd);
    *fd = -1;
  }
}

bool MakePipe(int fds[2]) {
  if (pipe(fds) != 0) return false;
  // 다른 스레드가 띄우는 프로세스에 우리 파이프 끝이 새지 않게 합니다. 남으면 출력 EOF가 오지 않습니다.
  fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  return true;
}

std::vector<std::string> BuildEnvironment(const ProcessLaunch& launch) {
  std::vector<std::string> env;
  for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) env.emplace_back(*entry);
  for (const auto& [name, value] : launch.extra_env) {
    const std::string prefix = name + "=";
    std::erase_if(env, [&](const std::string& item) { return item.compare(0, prefix.size(), prefix) == 0; });
    env.push_back(prefix + value);
  }
  return env;
}

bool IsExecutable(const std::string& path) {
  struct stat info{};
  return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && access(path.c_str(), X_OK) == 0;
}

// fork 뒤 자식에서는 메모리를 잡을 수 없으므로 PATH 검색을 부모에서 끝냅니다.
std::string ResolveProgram(const std::string& program, const std::vector<std::string>& env) {
  if (program.find('/') != std::string::npos) return program;
  std::string path_list = "/usr/bin:/bin";
  for (const std::string& item : env) {
    if (item.compare(0, 5, "PATH=") == 0) path_list = item.substr(5);
  }
  size_t begin = 0;
  while (begin <= path_list.size()) {
    size_t end = path_list.find(':', begin);
    if (end == std::string::npos) end = path_list.size();
    const std::string dir = end > begin ? path_list.substr(begin, end - begin) : ".";
    const std::string candidate = dir + "/" + program;
    if (IsExecutable(candidate)) return candidate;
    begin = end + 1;
  }
  return {};
}

int32_t ExitCodeOf(int status) {
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return -WTERMSIG(status);
  return -1;
}

}  // namespace

struct ChildProcess::Impl {
  pid_t pid = -1;
  int stdin_fd = -1;
  int stdout_fd = -1;

  std::mutex state_mutex;
  bool reaped = false;
  int32_t exit_code = 0;

  std::mutex write_mutex;
  OutputLines output;
  std::atomic<bool> stop{false};
  std::thread reader;

  void ReadLoop() {
    char buffer[65536];
    while (!stop.load()) {
      pollfd entry{stdout_fd, POLLIN, 0};
      // 소멸자가 stop을 세우면 50ms 안에 빠져나옵니다. 하위 프로세스가 파이프를 쥐고 있어도 멈추지 않습니다.
      const int ready = poll(&entry, 1, 50);
      if (ready < 0 && errno != EINTR) break;
      if (ready <= 0) continue;
      const ssize_t got = read(stdout_fd, buffer, sizeof(buffer));
      if (got < 0 && errno == EINTR) continue;
      if (got <= 0) break;
      output.Append(buffer, static_cast<size_t>(got));
    }
    output.Close();
  }

  // state_mutex를 잡은 채 부릅니다.
  bool PollExitLocked(bool block) {
    if (reaped) return true;
    int status = 0;
    pid_t done = -1;
    do {
      done = waitpid(pid, &status, block ? 0 : WNOHANG);
    } while (done < 0 && errno == EINTR);
    if (done == pid) {
      reaped = true;
      exit_code = ExitCodeOf(status);
    }
    return reaped;
  }
};

ChildProcess::ChildProcess(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<ChildProcess> ChildProcess::Start(const ProcessLaunch& launch, std::string* error) {
  const std::vector<std::string> env = BuildEnvironment(launch);
  const std::string program = ResolveProgram(launch.program, env);
  if (program.empty() || !IsExecutable(program)) {
    SetError(error, "실행 파일을 찾지 못했습니다: " + launch.program);
    return nullptr;
  }

  // execve에 넘길 배열을 fork 전에 만듭니다.
  std::vector<std::string> args;
  args.push_back(launch.program);
  args.insert(args.end(), launch.arguments.begin(), launch.arguments.end());
  std::vector<char*> argv;
  for (std::string& arg : args) argv.push_back(arg.data());
  argv.push_back(nullptr);
  std::vector<std::string> env_copy = env;
  std::vector<char*> envp;
  for (std::string& item : env_copy) envp.push_back(item.data());
  envp.push_back(nullptr);
  const char* cwd = launch.working_dir.empty() ? nullptr : launch.working_dir.c_str();
  const bool low_priority = launch.low_priority;

  int in_pipe[2] = {-1, -1};
  int out_pipe[2] = {-1, -1};
  int err_pipe[2] = {-1, -1};
  if (!MakePipe(in_pipe) || !MakePipe(out_pipe) || !MakePipe(err_pipe)) {
    SetError(error, std::string("파이프를 만들지 못했습니다: ") + std::strerror(errno));
    for (int* fd : {&in_pipe[0], &in_pipe[1], &out_pipe[0], &out_pipe[1], &err_pipe[0], &err_pipe[1]}) CloseFd(fd);
    return nullptr;
  }

  const pid_t pid = fork();
  if (pid == 0) {
    // 자식: 비동기 신호 안전 함수만 씁니다.
    setpgid(0, 0);
    if (low_priority) {
      // 실패해도 분석은 돌려야 하므로 결과를 보지 않습니다. nice 값은 19를 넘지 않게 커널이 자릅니다.
      errno = 0;
      const int current = getpriority(PRIO_PROCESS, 0);
      if (errno == 0) setpriority(PRIO_PROCESS, 0, current + kLowPriorityNice);
    }
    dup2(in_pipe[0], STDIN_FILENO);
    dup2(out_pipe[1], STDOUT_FILENO);
    dup2(out_pipe[1], STDERR_FILENO);
    if (cwd != nullptr && chdir(cwd) != 0) {
      const int report[2] = {1, errno};
      (void)!write(err_pipe[1], report, sizeof(report));
      _exit(127);
    }
    execve(program.c_str(), argv.data(), envp.data());
    const int report[2] = {2, errno};
    (void)!write(err_pipe[1], report, sizeof(report));
    _exit(127);
  }

  CloseFd(&in_pipe[0]);
  CloseFd(&out_pipe[1]);
  CloseFd(&err_pipe[1]);
  if (pid < 0) {
    SetError(error, std::string("프로세스를 만들지 못했습니다: ") + std::strerror(errno));
    for (int* fd : {&in_pipe[1], &out_pipe[0], &err_pipe[0]}) CloseFd(fd);
    return nullptr;
  }
  // 자식이 setpgid를 부르기 전에 Kill이 와도 그룹이 있도록 부모에서도 정합니다.
  setpgid(pid, pid);

  // exec가 성공하면 CLOEXEC로 닫혀 0바이트를 읽고, 실패하면 errno를 읽습니다.
  int report[2] = {0, 0};
  ssize_t got = 0;
  do {
    got = read(err_pipe[0], report, sizeof(report));
  } while (got < 0 && errno == EINTR);
  CloseFd(&err_pipe[0]);
  if (got > 0) {
    int status = 0;
    waitpid(pid, &status, 0);
    CloseFd(&in_pipe[1]);
    CloseFd(&out_pipe[0]);
    SetError(error, std::string(report[0] == 1 ? "작업 폴더로 옮기지 못했습니다: " : "실행하지 못했습니다: ") +
                        std::strerror(report[1]));
    return nullptr;
  }

#ifdef F_SETNOSIGPIPE
  // 자식이 입력을 닫은 뒤 쓰면 SIGPIPE가 앱 전체를 끝내므로 이 fd에서만 끕니다.
  fcntl(in_pipe[1], F_SETNOSIGPIPE, 1);
#endif

  auto impl = std::make_unique<Impl>();
  impl->pid = pid;
  impl->stdin_fd = in_pipe[1];
  impl->stdout_fd = out_pipe[0];
  Impl* raw = impl.get();
  impl->reader = std::thread([raw] { raw->ReadLoop(); });
  return std::unique_ptr<ChildProcess>(new ChildProcess(std::move(impl)));
}

ChildProcess::~ChildProcess() {
  CloseInput();
  Kill();
  {
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    impl_->PollExitLocked(true);
  }
  impl_->stop.store(true);
  if (impl_->reader.joinable()) impl_->reader.join();
  CloseFd(&impl_->stdout_fd);
}

bool ChildProcess::WriteLine(const std::string& line) {
  std::lock_guard<std::mutex> lock(impl_->write_mutex);
  if (impl_->stdin_fd < 0) return false;
  const std::string data = line + "\n";
#ifndef F_SETNOSIGPIPE
  sigset_t block;
  sigset_t previous;
  sigemptyset(&block);
  sigaddset(&block, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &block, &previous);
#endif
  size_t sent = 0;
  bool ok = true;
  while (sent < data.size()) {
    const ssize_t wrote = write(impl_->stdin_fd, data.data() + sent, data.size() - sent);
    if (wrote < 0 && errno == EINTR) continue;
    if (wrote <= 0) {
      ok = false;
      break;
    }
    sent += static_cast<size_t>(wrote);
  }
#ifndef F_SETNOSIGPIPE
  if (!ok) {
    // 막아 둔 SIGPIPE를 거둬 들여 마스크를 되돌린 뒤 전달되지 않게 합니다.
    const timespec zero{0, 0};
    sigtimedwait(&block, nullptr, &zero);
  }
  pthread_sigmask(SIG_SETMASK, &previous, nullptr);
#endif
  return ok;
}

void ChildProcess::CloseInput() {
  std::lock_guard<std::mutex> lock(impl_->write_mutex);
  CloseFd(&impl_->stdin_fd);
}

LineStatus ChildProcess::ReadLine(std::string* line, int64_t timeout_ms) {
  return impl_->output.Read(line, timeout_ms);
}

std::optional<int32_t> ChildProcess::Wait(int64_t timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(impl_->state_mutex);
      if (impl_->PollExitLocked(false)) return impl_->exit_code;
    }
    if (std::chrono::steady_clock::now() >= deadline) return std::nullopt;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

void ChildProcess::Kill() {
  std::lock_guard<std::mutex> lock(impl_->state_mutex);
  // 거두기 전에는 pid와 그룹 번호가 아직 우리 것이므로 다른 프로세스에 닿지 않습니다.
  if (impl_->reaped) return;
  kill(-impl_->pid, SIGKILL);
  kill(impl_->pid, SIGKILL);
}

int64_t ChildProcess::Id() const { return impl_->pid; }

}  // namespace hwlab::platform
