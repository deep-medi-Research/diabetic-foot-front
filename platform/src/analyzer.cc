#include "hwlab/platform/analyzer.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

#include "hwlab/platform/shot_folder.h"
#include "json_reader.h"

namespace hwlab::platform {

namespace {

using Clock = std::chrono::steady_clock;

constexpr int64_t kReadPollMs = 50;
constexpr int64_t kIdleWaitMs = 100;
constexpr size_t kRecentLines = 6;

// 관류 분석 프로그램 프로세스 쪽 단계입니다. 공개 상태(AnalyzerState)보다 잘게 나눕니다.
enum class Stage : uint8_t {
  kAwaitReady,    // 준비 줄을 기다림
  kPreloadDue,    // 준비 줄을 받았고 미리 올리기를 아직 보내지 않음(보류 중)
  kAwaitPreload,  // 미리 올리기 응답을 기다림
  kReady,
  kBusy,
  kFailed,
  kStopped,
};

struct Pending {
  PerfusionRequest request;
  std::string line;
};

int64_t MsSince(Clock::time_point since) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count();
}

bool FileExists(const std::string& utf8_path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(PathFromUtf8(utf8_path), ec);
}

std::optional<std::string> ReadText(const std::string& utf8_path) {
  std::FILE* file = OpenFileUtf8(utf8_path, "rb");
  if (file == nullptr) return std::nullopt;
  std::string text;
  char buffer[8192];
  size_t n = 0;
  while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) text.append(buffer, n);
  std::fclose(file);
  return text;
}

std::string ExitText(const std::optional<int32_t>& code) {
  if (!code) return "종료 코드 모름";
  if (*code == kKilledExitCode) return "강제 종료";
  return "종료 코드 " + std::to_string(*code);
}

}  // namespace

struct Analyzer::Impl {
  ProcessLaunch launch;
  AnalyzerTiming timing;

  mutable std::mutex mutex;
  std::condition_variable wake;
  // 내부 스레드만 바꿉니다. Cancel은 잠금 안에서 Kill만 부릅니다(ChildProcess는 여러 스레드에서 불러도 됨).
  std::unique_ptr<ChildProcess> child;
  Stage stage = Stage::kAwaitReady;
  std::deque<Pending> queue;
  std::optional<Pending> current;
  Clock::time_point sent_at;
  Clock::time_point last_activity;
  bool hold = false;
  bool cancel_requested = false;
  bool timed_out = false;
  bool restart_requested = false;
  bool stop_requested = false;
  int32_t step = 0;
  int32_t total = 0;
  std::string message;
  int32_t sub_step = 0;
  int32_t sub_total = 0;
  std::string sub_message;
  std::string worker_version;
  std::string upstream_commit;
  int64_t finished_count = 0;
  AnalyzerOutcome last_done;
  int64_t presegmented = 0;
  std::string last_presegment;
  int64_t figures_count = 0;
  std::string last_figures;
  std::deque<std::string> recent;  // 실패 이유에 넣을 마지막 출력 줄
  std::FILE* log = nullptr;        // 분석 중인 요청의 analyzer_log.txt
  std::thread thread;

  void ClearSubStage() {
    sub_step = 0;
    sub_total = 0;
    sub_message.clear();
  }

  void Launch() {
    std::string error;
    child = ChildProcess::Start(launch, &error);
    recent.clear();
    worker_version.clear();
    upstream_commit.clear();
    step = 0;
    total = 0;
    ClearSubStage();
    last_activity = Clock::now();
    if (!child) {
      stage = Stage::kFailed;
      message = "관류 분석 프로그램을 띄우지 못했습니다: " + error;
      return;
    }
    stage = Stage::kAwaitReady;
    message = "분석 시작 중";
  }

  void OpenLog(const Pending& p) {
    std::error_code ec;
    std::filesystem::create_directories(PathFromUtf8(p.request.output_dir), ec);
    std::string path = p.request.output_dir;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path.push_back('/');
    log = OpenFileUtf8(path + "analyzer_log.txt", "ab");
    WriteLog("> " + p.line);
  }

  void WriteLog(const std::string& line) {
    if (log == nullptr) return;
    std::fwrite(line.data(), 1, line.size(), log);
    std::fputc('\n', log);
    std::fflush(log);
  }

  std::string RecentText() const {
    std::string text;
    for (const std::string& line : recent) {
      if (!text.empty()) text += "\n";
      text += line;
    }
    return text;
  }

  // 분석 중인 요청을 끝내고 결과를 남깁니다.
  void Finish(bool ok, std::string error, bool cancelled) {
    if (!current) return;
    AnalyzerOutcome out;
    out.name = current->request.name;
    out.recording_dir = current->request.recording_dir;
    out.summary_path = PerfusionSummaryPath(current->request.output_dir, current->request.name);
    out.elapsed_ms = MsSince(sent_at);
    out.cancelled = cancelled;
    out.worker_version = worker_version;
    out.upstream_commit = upstream_commit;
    if (ok && !FileExists(out.summary_path)) {
      ok = false;
      error = "관류 분석 프로그램이 성공을 알렸지만 요약 파일이 없습니다: " + out.summary_path;
    }
    if (ok) {
      // 요약에 남은 버전이 준비 줄보다 정확합니다(관류 분석 프로그램이 결과를 쓰며 남긴 값).
      if (const auto text = ReadText(out.summary_path)) {
        const WorkerStamp stamp = ReadWorkerStamp(*text);
        if (!stamp.worker_version.empty()) out.worker_version = stamp.worker_version;
        if (!stamp.upstream_commit.empty()) out.upstream_commit = stamp.upstream_commit;
      }
    }
    out.ok = ok;
    out.error = ok ? std::string() : error;
    if (!ok) WriteLog("! " + out.error);
    if (log != nullptr) {
      std::fclose(log);
      log = nullptr;
    }
    last_done = std::move(out);
    ++finished_count;
    current.reset();
    step = 0;
    total = 0;
    ClearSubStage();
  }

  // 잠금 안에서 부릅니다. 보낼 것이 있으면 보냅니다.
  void Dispatch() {
    if (!child || hold) return;
    if (stage == Stage::kPreloadDue) {
      child->WriteLine(BuildPreloadLine());
      stage = Stage::kAwaitPreload;
      message = "분석 모델 올리는 중";
      last_activity = Clock::now();
      return;
    }
    if (stage == Stage::kReady && !queue.empty()) {
      current = std::move(queue.front());
      queue.pop_front();
      OpenLog(*current);
      sent_at = Clock::now();
      last_activity = sent_at;
      step = 0;
      total = 0;
      ClearSubStage();
      message.clear();
      stage = Stage::kBusy;
      // 쓰지 못하면 프로세스가 끝난 것이므로 출력이 닫혀 HandleExit가 실패로 처리합니다.
      child->WriteLine(current->line);
    }
  }

  void HandleLine(const std::string& line) {
    recent.push_back(line);
    if (recent.size() > kRecentLines) recent.pop_front();
    if (stage == Stage::kBusy) WriteLog(line);
    const AnalyzerLine parsed = ParseAnalyzerLine(line);
    switch (parsed.kind) {
      case AnalyzerLineKind::kReady:
        if (stage != Stage::kAwaitReady) break;
        worker_version = parsed.worker_version;
        upstream_commit = parsed.upstream_commit;
        stage = Stage::kPreloadDue;
        message = "분석 모델 올리기 대기";
        last_activity = Clock::now();
        break;
      case AnalyzerLineKind::kPreloaded:
      case AnalyzerLineKind::kPreloadFailed:
        // 미리 올리기 실패는 분석할 때 다시 올리므로 요청을 막지 않습니다.
        if (stage != Stage::kAwaitPreload) break;
        stage = Stage::kReady;
        message.clear();
        break;
      case AnalyzerLineKind::kStage:
        if (stage != Stage::kBusy) break;
        step = parsed.step;
        total = parsed.total;
        message = parsed.message;
        ClearSubStage();
        last_activity = Clock::now();
        break;
      case AnalyzerLineKind::kSubStage:
        // 하위 단계도 진행의 증거이므로 시간 제한의 마지막 활동으로 셉니다.
        if (stage != Stage::kBusy) break;
        sub_step = parsed.step;
        sub_total = parsed.total;
        sub_message = parsed.message;
        last_activity = Clock::now();
        break;
      case AnalyzerLineKind::kDone:
        if (stage != Stage::kBusy) break;
        Finish(parsed.ok, parsed.error.empty() ? "관류 분석 프로그램이 이유 없이 실패를 알렸습니다" : parsed.error, false);
        stage = Stage::kReady;
        message.clear();
        break;
      case AnalyzerLineKind::kPresegmented:
        ++presegmented;
        last_presegment = parsed.message;
        break;
      case AnalyzerLineKind::kFigures:
        ++figures_count;
        last_figures = parsed.message;
        break;
      case AnalyzerLineKind::kIdleExit:
      case AnalyzerLineKind::kOther:
        break;
    }
  }

  void HandleExit(const std::optional<int32_t>& code) {
    child.reset();
    const std::string tail = RecentText();
    const std::string why = ExitText(code) + (tail.empty() ? std::string() : ", 마지막 출력:\n" + tail);
    const bool timed_out_now = timed_out;
    timed_out = false;
    if (stage == Stage::kBusy) {
      if (cancel_requested) {
        Finish(false, "취소했습니다", true);
      } else if (timed_out_now) {
        Finish(false, "진행 줄이 " + std::to_string(timing.stage_timeout_ms / 1000) + "초 동안 없어 분석을 중단했습니다",
               false);
      } else {
        Finish(false, "관류 분석 프로그램이 비정상 종료했습니다(" + why + ")", false);
      }
      cancel_requested = false;
      Launch();
      return;
    }
    cancel_requested = false;
    if (stage == Stage::kAwaitReady || stage == Stage::kAwaitPreload) {
      // 준비 전에 끝나면 다시 띄워도 같은 이유로 끝나므로 반복하지 않고 사용자에게 보입니다.
      stage = Stage::kFailed;
      message = timed_out_now ? "관류 분석 프로그램이 " + std::to_string(timing.stage_timeout_ms / 1000) + "초 안에 준비되지 않았습니다"
                              : "관류 분석 프로그램이 준비 전에 끝났습니다(" + why + ")";
      return;
    }
    // 대기 중에 끝난 경우(유휴 종료, 비정상 종료)는 다음 요청을 받을 수 있게 다시 띄웁니다.
    Launch();
  }

  void Run() {
    std::unique_lock<std::mutex> lock(mutex);
    while (!stop_requested) {
      if (!child) {
        if (restart_requested) {
          restart_requested = false;
          Launch();
        } else {
          wake.wait_for(lock, std::chrono::milliseconds(kIdleWaitMs));
        }
        continue;
      }
      Dispatch();
      const bool waiting = stage == Stage::kAwaitReady || stage == Stage::kAwaitPreload || stage == Stage::kBusy;
      if (waiting && !timed_out && MsSince(last_activity) > timing.stage_timeout_ms) {
        timed_out = true;
        child->Kill();
      }
      ChildProcess* process = child.get();
      lock.unlock();
      std::string line;
      const LineStatus status = process->ReadLine(&line, kReadPollMs);
      std::optional<int32_t> code;
      if (status == LineStatus::kClosed) code = process->Wait(timing.stop_wait_ms);
      lock.lock();
      if (status == LineStatus::kLine) HandleLine(line);
      if (status == LineStatus::kClosed && !stop_requested) HandleExit(code);
    }
    if (child) {
      // 입력을 닫으면 관류 분석 프로그램은 다음 요청을 읽다가 끝납니다. 분석 중이면 입력을 읽지 않으므로 기다리지 않고 강제 종료합니다.
      ChildProcess* process = child.get();
      const bool busy = stage == Stage::kBusy;
      lock.unlock();
      process->CloseInput();
      if (busy || !process->Wait(timing.stop_wait_ms)) process->Kill();
      process->Wait(timing.stop_wait_ms);
      lock.lock();
      child.reset();
    }
    if (log != nullptr) {
      std::fclose(log);
      log = nullptr;
    }
    stage = Stage::kStopped;
  }
};

Analyzer::Analyzer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<Analyzer> Analyzer::Start(const ProcessLaunch& launch, const AnalyzerTiming& timing,
                                          std::string* error) {
  auto impl = std::make_unique<Impl>();
  impl->launch = launch;
  impl->timing = timing;
  impl->Launch();
  if (impl->stage == Stage::kFailed) {
    if (error != nullptr) *error = impl->message;
    return nullptr;
  }
  Impl* raw = impl.get();
  raw->thread = std::thread([raw] { raw->Run(); });
  return std::unique_ptr<Analyzer>(new Analyzer(std::move(impl)));
}

Analyzer::~Analyzer() { Stop(); }

bool Analyzer::Submit(const PerfusionRequest& request, std::string* error) {
  std::string why;
  auto line = BuildPerfusionRequestLine(request, &why);
  if (line && (request.output_dir.empty() || request.name.empty())) {
    line.reset();
    why = "요청에 결과 폴더와 이름이 있어야 합니다";
  }
  if (!line) {
    if (error != nullptr) *error = why;
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->stop_requested) {
    if (error != nullptr) *error = "관류 분석 프로그램을 멈췄습니다";
    return false;
  }
  impl_->queue.push_back({request, *line});
  if (impl_->stage == Stage::kFailed) impl_->restart_requested = true;
  impl_->wake.notify_all();
  return true;
}

bool Analyzer::Cancel() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->stage != Stage::kBusy || !impl_->child) return false;
  impl_->cancel_requested = true;
  impl_->child->Kill();
  return true;
}

void Analyzer::SetHold(bool hold) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->hold = hold;
  impl_->wake.notify_all();
}

bool Analyzer::Presegment(const std::string& left_video, const std::string& right_video, std::string* error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const Stage stage = impl_->stage;
  if (impl_->stop_requested || !impl_->child || (stage != Stage::kReady && stage != Stage::kBusy)) {
    if (error != nullptr) *error = "관류 분석 프로그램이 모델을 올린 뒤가 아닙니다";
    return false;
  }
  // 쓰지 못하면 프로세스가 끝난 것이므로 출력이 닫혀 내부 스레드가 처리합니다.
  impl_->child->WriteLine(BuildPresegmentLine(left_video, right_video));
  return true;
}

void Analyzer::Restart() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->stage != Stage::kFailed) return;
  impl_->restart_requested = true;
  impl_->wake.notify_all();
}

void Analyzer::Stop() {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->stop_requested) {
      impl_->stop_requested = true;
      impl_->Finish(false, "관류 분석 프로그램을 멈춰 취소했습니다", true);
      impl_->queue.clear();
      impl_->wake.notify_all();
    }
  }
  if (impl_->thread.joinable()) impl_->thread.join();
}

AnalyzerStatus Analyzer::Status() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const Impl& m = *impl_;
  AnalyzerStatus s;
  switch (m.stage) {
    case Stage::kAwaitReady:
    case Stage::kPreloadDue:
    case Stage::kAwaitPreload: s.state = AnalyzerState::kStarting; break;
    case Stage::kReady: s.state = AnalyzerState::kIdle; break;
    case Stage::kBusy: s.state = AnalyzerState::kBusy; break;
    case Stage::kFailed: s.state = AnalyzerState::kFailed; break;
    case Stage::kStopped: s.state = AnalyzerState::kStopped; break;
  }
  if (m.stop_requested) s.state = AnalyzerState::kStopped;
  s.step = m.step;
  s.total = m.total;
  s.message = m.message;
  s.sub_step = m.sub_step;
  s.sub_total = m.sub_total;
  s.sub_message = m.sub_message;
  s.worker_version = m.worker_version;
  s.upstream_commit = m.upstream_commit;
  if (m.current) {
    s.current_name = m.current->request.name;
    s.elapsed_ms = MsSince(m.sent_at);
  }
  s.queued = static_cast<int32_t>(m.queue.size());
  s.held = m.hold;
  s.finished_count = m.finished_count;
  s.last_done = m.last_done;
  s.presegmented = m.presegmented;
  s.last_presegment = m.last_presegment;
  s.figures_count = m.figures_count;
  s.last_figures = m.last_figures;
  return s;
}

ProcessLaunch AnalyzerLaunch(const std::string& app_dir, const std::string& python_override,
                             const std::string& script_override) {
  std::string base = app_dir;
  while (!base.empty() && (base.back() == '/' || base.back() == '\\')) base.pop_back();
#ifdef _WIN32
  const std::string bundled_python = base + "/python/python.exe";
#else
  const std::string bundled_python = base + "/python/bin/python3";
#endif
  ProcessLaunch launch;
  launch.program = python_override.empty() ? bundled_python : python_override;
  const std::string script = script_override.empty() ? base + "/analysis/dfr_worker.py" : script_override;
  // 0이면 관류 분석 프로그램이 스스로 끝나지 않습니다. 앱이 닫힐 때까지 띄워 두어 촬영마다 모델을 다시 올리지 않습니다.
  launch.arguments = {script, "--idle-exit", "0"};
  const size_t cut = script.find_last_of("/\\");
  launch.working_dir = cut == std::string::npos ? std::string() : script.substr(0, cut);
  launch.extra_env = {{"PYTHONUNBUFFERED", "1"}, {"PYTHONUTF8", "1"}, {"PYTHONIOENCODING", "utf-8"}};
  launch.low_priority = true;
  return launch;
}

std::vector<AnalyzerVariant> FindAnalyzerVariants(const std::string& analysis_dir, std::string* error) {
  namespace fs = std::filesystem;
  std::vector<AnalyzerVariant> out;
  std::string problems;
  const auto note = [&](const std::string& why) { problems += (problems.empty() ? "" : "; ") + why; };
  std::error_code ec;
  const fs::path dir = PathFromUtf8(analysis_dir) / "variants";
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_directory(ec)) continue;
    const fs::path file = it->path() / "VARIANT.json";
    if (!fs::exists(file, ec)) continue;
    const std::u8string folder_u8 = it->path().filename().u8string();
    const std::string folder(folder_u8.begin(), folder_u8.end());
    std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    const std::string json = text.str();
    const auto at = [&](const std::vector<std::string>& path) {
      std::string value;
      return JsonReader(json).FindString(path, &value) ? value : std::string();
    };
    AnalyzerVariant v;
    v.id = at({"id"});
    if (v.id.empty() || v.id != folder) {
      note("판 정보를 읽지 못했거나 id가 폴더 이름과 다릅니다: variants/" + folder + "/VARIANT.json");
      continue;
    }
    v.label = at({"label"});
    if (v.label.empty()) v.label = v.id;
    v.summary = at({"summary"});
    v.upstream_commit = at({"upstream", "commit"});
    v.upstream_date = at({"upstream", "date"});
    v.release_tag = at({"release", "tag"});
    for (auto [role, asset] : {std::pair<const char*, AnalyzerVariant::Asset*>{"classifier", &v.classifier},
                               std::pair<const char*, AnalyzerVariant::Asset*>{"template", &v.pulse_template}}) {
      asset->name = at({"release", role, "name"});
      asset->sha256 = at({"release", role, "sha256"});
      asset->uploaded = at({"release", role, "uploaded"});
    }
    out.push_back(std::move(v));
  }
  std::sort(out.begin(), out.end(), [](const AnalyzerVariant& a, const AnalyzerVariant& b) { return a.id < b.id; });
  if (out.empty()) note("분석 판이 없습니다: " + analysis_dir + "/variants");
  if (error != nullptr) *error = problems;
  return out;
}

ProcessLaunch AnalyzerVariantLaunch(ProcessLaunch launch, const std::string& variant_id) {
  launch.arguments.push_back("--variant");
  launch.arguments.push_back(variant_id);
  return launch;
}

std::optional<std::string> AnalyzerFilesProblem(const ProcessLaunch& launch) {
  const auto check = [](const std::string& what, const std::string& path) -> std::optional<std::string> {
    std::error_code ec;
    const auto p = PathFromUtf8(path);
    if (path.empty() || !std::filesystem::is_regular_file(p, ec)) return what + " 없음: " + path;
    if (std::filesystem::file_size(p, ec) == 0 || ec) return what + " 크기 0: " + path;
    return std::nullopt;
  };
  if (auto problem = check("파이썬", launch.program)) return problem;
  if (launch.arguments.empty()) return std::string("분석 스크립트 경로가 없습니다");
  return check("분석 스크립트", launch.arguments.front());
}

}  // namespace hwlab::platform
