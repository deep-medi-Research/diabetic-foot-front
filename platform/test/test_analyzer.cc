#include "hwlab/platform/analyzer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "hwlab/platform/perfusion_analysis.h"
#include "hwlab/record/sidecar.h"

namespace {

using hwlab::platform::Analyzer;
using hwlab::platform::AnalyzerLaunch;
using hwlab::platform::AnalyzerOutcome;
using hwlab::platform::AnalyzerState;
using hwlab::platform::AnalyzerStatus;
using hwlab::platform::AnalyzerTiming;
using hwlab::platform::PerfusionRequest;
using hwlab::platform::ProcessLaunch;
using Clock = std::chrono::steady_clock;

std::filesystem::path FromUtf8(const std::string& text) {
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string ToUtf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

std::filesystem::path TempDir(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_analyzer" / FromUtf8(name);
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

void WriteFile(const std::filesystem::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
}

ProcessLaunch FakeWorker() {
  ProcessLaunch launch;
  launch.program = HWLAB_FAKE_ANALYZER;
  launch.arguments = {"worker"};
  return launch;
}

std::unique_ptr<Analyzer> StartAnalyzer(const ProcessLaunch& launch, AnalyzerTiming timing = {}) {
  std::string error;
  auto analyzer = Analyzer::Start(launch, timing, &error);
  EXPECT_NE(analyzer, nullptr) << error;
  return analyzer;
}

PerfusionRequest RequestNamed(const std::filesystem::path& dir, const std::string& name) {
  PerfusionRequest request;
  request.recording_dir = ToUtf8(dir);
  request.output_dir = ToUtf8(dir / "perfusion");
  request.name = name;
  return request;
}

// 조건이 참이 될 때까지 상태를 읽습니다. 시간 안에 참이 되지 않으면 마지막 상태로 실패를 남깁니다.
AnalyzerStatus WaitFor(const Analyzer& analyzer, const std::function<bool(const AnalyzerStatus&)>& done,
                       int64_t timeout_ms = 20'000) {
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  AnalyzerStatus status = analyzer.Status();
  while (!done(status)) {
    if (Clock::now() > deadline) {
      ADD_FAILURE() << "시간 안에 기다린 상태가 되지 않았습니다. state=" << static_cast<int>(status.state)
                    << " message=" << status.message << " finished=" << status.finished_count;
      return status;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    status = analyzer.Status();
  }
  return status;
}

AnalyzerOutcome WaitFinished(const Analyzer& analyzer, int64_t count, int64_t timeout_ms = 20'000) {
  return WaitFor(analyzer, [&](const AnalyzerStatus& s) { return s.finished_count >= count; }, timeout_ms).last_done;
}

// 성공한 요청은 요약 경로와 요약에 남은 관류 분석 프로그램 버전을 결과에 두고, 출력 줄을 analyzer_log.txt에 남깁니다.
TEST(Analyzer, SucceedsAndKeepsSummaryPath) {
  const auto dir = TempDir("ok");
  auto analyzer = StartAnalyzer(FakeWorker());
  ASSERT_NE(analyzer, nullptr);
  WaitFor(*analyzer, [](const AnalyzerStatus& s) { return s.state == AnalyzerState::kIdle; });
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "ok")));
  const AnalyzerOutcome done = WaitFinished(*analyzer, 1);
  EXPECT_TRUE(done.ok) << done.error;
  EXPECT_FALSE(done.cancelled);
  EXPECT_EQ(done.name, "ok");
  EXPECT_EQ(done.summary_path, ToUtf8(dir / "perfusion") + "/ok_summary.json");
  EXPECT_TRUE(std::filesystem::exists(FromUtf8(done.summary_path)));
  EXPECT_EQ(done.worker_version, "fake-1");
  EXPECT_EQ(done.upstream_commit, "5e5178d");
  const std::string log = ReadFile(dir / "perfusion" / "analyzer_log.txt");
  EXPECT_NE(log.find("[1/5] Reading videos"), std::string::npos) << log;
  EXPECT_NE(log.find("\"name\":\"ok\""), std::string::npos) << log;
  EXPECT_EQ(analyzer->Status().state, AnalyzerState::kIdle);
}

// 관류 분석 프로그램이 실패를 알리면 그 오류 문장을 그대로 결과에 둡니다.
TEST(Analyzer, ReportsWorkerFailure) {
  const auto dir = TempDir("fail");
  auto analyzer = StartAnalyzer(FakeWorker());
  ASSERT_NE(analyzer, nullptr);
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "fail")));
  const AnalyzerOutcome done = WaitFinished(*analyzer, 1);
  EXPECT_FALSE(done.ok);
  EXPECT_FALSE(done.cancelled);
  EXPECT_NE(done.error.find("left.avi 가 없습니다"), std::string::npos) << done.error;
  EXPECT_NE(ReadFile(dir / "perfusion" / "analyzer_log.txt").find("! left.avi"), std::string::npos);
}

// 성공 줄이 와도 요약 파일이 없으면 실패입니다.
TEST(Analyzer, TreatsMissingSummaryAsFailure) {
  const auto dir = TempDir("nosummary");
  auto analyzer = StartAnalyzer(FakeWorker());
  ASSERT_NE(analyzer, nullptr);
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "nosummary")));
  const AnalyzerOutcome done = WaitFinished(*analyzer, 1);
  EXPECT_FALSE(done.ok);
  EXPECT_NE(done.error.find("요약 파일이 없습니다"), std::string::npos) << done.error;
}

// 진행 줄이 제한 시간 동안 없으면 강제 종료해 실패로 두고, 다시 띄운 관류 분석 프로그램이 다음 요청을 받습니다.
TEST(Analyzer, TimesOutWithoutProgressAndRestarts) {
  const auto dir = TempDir("hang");
  AnalyzerTiming timing;
  timing.stage_timeout_ms = 1'000;
  auto analyzer = StartAnalyzer(FakeWorker(), timing);
  ASSERT_NE(analyzer, nullptr);
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "hang")));
  const AnalyzerStatus busy = WaitFor(*analyzer, [](const AnalyzerStatus& s) { return s.step == 1; });
  EXPECT_EQ(busy.state, AnalyzerState::kBusy);
  EXPECT_EQ(busy.total, 5);
  EXPECT_EQ(busy.current_name, "hang");
  // 들여 쓴 하위 단계 줄은 하위 단계 값만 바꾸고 진행 단계 번호는 그대로 둡니다.
  const AnalyzerStatus sub = WaitFor(*analyzer, [](const AnalyzerStatus& s) { return s.sub_step == 1; });
  EXPECT_EQ(sub.step, 1);
  EXPECT_EQ(sub.sub_total, 7);
  EXPECT_EQ(sub.sub_message, "thermal sub-step");
  const AnalyzerOutcome done = WaitFinished(*analyzer, 1);
  EXPECT_FALSE(done.ok);
  EXPECT_FALSE(done.cancelled);
  EXPECT_NE(done.error.find("진행 줄이 1초 동안 없어"), std::string::npos) << done.error;
  EXPECT_GE(done.elapsed_ms, 1'000);
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "ok")));
  const AnalyzerOutcome next = WaitFinished(*analyzer, 2);
  EXPECT_TRUE(next.ok) << next.error;
}

// 취소하면 분석 중인 요청이 취소로 끝나고, 다시 띄운 관류 분석 프로그램이 모델을 미리 올린 뒤 다음 요청을 받습니다.
TEST(Analyzer, CancelKillsAndRestarts) {
  const auto dir = TempDir("cancel");
  auto analyzer = StartAnalyzer(FakeWorker());
  ASSERT_NE(analyzer, nullptr);
  EXPECT_FALSE(analyzer->Cancel());
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "hang")));
  WaitFor(*analyzer, [](const AnalyzerStatus& s) { return s.step == 1; });
  EXPECT_TRUE(analyzer->Cancel());
  const AnalyzerOutcome done = WaitFinished(*analyzer, 1, 5'000);
  EXPECT_FALSE(done.ok);
  EXPECT_TRUE(done.cancelled);
  EXPECT_EQ(done.name, "hang");
  WaitFor(*analyzer, [](const AnalyzerStatus& s) { return s.state == AnalyzerState::kIdle; });
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "ok")));
  EXPECT_TRUE(WaitFinished(*analyzer, 2).ok);
}

// 두 요청은 먼저 넣은 순서로 한 건씩 분석하고, 분석 중에는 다른 요청을 대기열에 둡니다.
TEST(Analyzer, RunsQueuedRequestsInOrder) {
  const auto dir = TempDir("queue");
  auto analyzer = StartAnalyzer(FakeWorker());
  ASSERT_NE(analyzer, nullptr);
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "slow_a")));
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "slow_b")));
  const AnalyzerStatus first =
      WaitFor(*analyzer, [](const AnalyzerStatus& s) { return s.state == AnalyzerState::kBusy; });
  EXPECT_EQ(first.current_name, "slow_a");
  EXPECT_EQ(first.queued, 1);
  const AnalyzerOutcome a = WaitFinished(*analyzer, 1);
  EXPECT_EQ(a.name, "slow_a");
  EXPECT_TRUE(a.ok) << a.error;
  const AnalyzerOutcome b = WaitFinished(*analyzer, 2);
  EXPECT_EQ(b.name, "slow_b");
  EXPECT_TRUE(b.ok) << b.error;
  EXPECT_EQ(analyzer->Status().queued, 0);
}

// 분석 중 관류 분석 프로그램이 비정상 종료하면 그 요청을 종료 코드와 마지막 출력으로 실패 처리하고 다시 띄웁니다.
TEST(Analyzer, RestartsAfterWorkerCrash) {
  const auto dir = TempDir("crash");
  auto analyzer = StartAnalyzer(FakeWorker());
  ASSERT_NE(analyzer, nullptr);
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "crash")));
  const AnalyzerOutcome crashed = WaitFinished(*analyzer, 1);
  EXPECT_FALSE(crashed.ok);
  EXPECT_FALSE(crashed.cancelled);
  EXPECT_NE(crashed.error.find("비정상 종료"), std::string::npos) << crashed.error;
  EXPECT_NE(crashed.error.find("종료 코드 3"), std::string::npos) << crashed.error;
  EXPECT_NE(crashed.error.find("MemoryError"), std::string::npos) << crashed.error;
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "ok")));
  const AnalyzerOutcome next = WaitFinished(*analyzer, 2);
  EXPECT_EQ(next.name, "ok");
  EXPECT_TRUE(next.ok) << next.error;
}

// 보류 중에는 요청을 보내지 않고, 보류를 풀면 보냅니다.
TEST(Analyzer, HoldDefersDispatch) {
  const auto dir = TempDir("hold");
  auto analyzer = StartAnalyzer(FakeWorker());
  ASSERT_NE(analyzer, nullptr);
  WaitFor(*analyzer, [](const AnalyzerStatus& s) { return s.state == AnalyzerState::kIdle; });
  analyzer->SetHold(true);
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "ok")));
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  const AnalyzerStatus held = analyzer->Status();
  EXPECT_EQ(held.state, AnalyzerState::kIdle);
  EXPECT_EQ(held.queued, 1);
  EXPECT_TRUE(held.held);
  EXPECT_EQ(held.finished_count, 0);
  analyzer->SetHold(false);
  EXPECT_TRUE(WaitFinished(*analyzer, 1).ok);
}

// 녹화 중 첫 장 미리 분할은 보류 중에도 바로 보내고, 대기열의 분석 요청은 그대로 기다립니다.
TEST(Analyzer, PresegmentBypassesHold) {
  const auto dir = TempDir("presegment");
  auto analyzer = StartAnalyzer(FakeWorker());
  ASSERT_NE(analyzer, nullptr);
  WaitFor(*analyzer, [](const AnalyzerStatus& s) { return s.state == AnalyzerState::kIdle; });
  analyzer->SetHold(true);
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "ok")));
  std::string error;
  ASSERT_TRUE(analyzer->Presegment("C:/촬영/a_left.mkv", "C:/촬영/a_right.mkv", &error)) << error;
  const AnalyzerStatus status = WaitFor(*analyzer, [](const AnalyzerStatus& s) { return s.presegmented >= 2; });
  EXPECT_EQ(status.last_presegment, "{\"video\": \"right\", \"ok\": true}");
  EXPECT_EQ(status.queued, 1);
  EXPECT_EQ(status.finished_count, 0);
  analyzer->SetHold(false);
  EXPECT_TRUE(WaitFinished(*analyzer, 1).ok);
}

// 그림을 뒤로 미룬 요청은 done 뒤의 figures 줄을 따로 셉니다. done은 그대로 한 건의 끝입니다.
TEST(Analyzer, CountsFiguresAfterDone) {
  const auto dir = TempDir("figures");
  auto analyzer = StartAnalyzer(FakeWorker());
  ASSERT_NE(analyzer, nullptr);
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "ok")));
  EXPECT_TRUE(WaitFinished(*analyzer, 1).ok);
  const AnalyzerStatus status = WaitFor(*analyzer, [](const AnalyzerStatus& s) { return s.figures_count >= 1; });
  EXPECT_EQ(status.last_figures, "{\"ok\": true, \"seconds\": 1.5}");
  EXPECT_EQ(status.state, AnalyzerState::kIdle);
}

// 모델을 올리기 전에는 미리 분할을 보내지 않습니다. 녹화 중에 모델 파일을 읽게 되기 때문입니다.
TEST(Analyzer, PresegmentNeedsLoadedModel) {
  ProcessLaunch launch;
  launch.program = HWLAB_FAKE_ANALYZER;
  launch.arguments = {"hang"};
  auto analyzer = StartAnalyzer(launch);
  ASSERT_NE(analyzer, nullptr);
  std::string error;
  EXPECT_FALSE(analyzer->Presegment("a_left.mkv", "a_right.mkv", &error));
  EXPECT_FALSE(error.empty());
}

// 준비 줄 전에 끝나는 관류 분석 프로그램은 다시 띄우지 않고 실패 상태로 이유를 보입니다. 요청을 넣으면 다시 띄웁니다.
TEST(Analyzer, FailsWhenWorkerExitsBeforeReady) {
  ProcessLaunch launch;
  launch.program = HWLAB_FAKE_ANALYZER;
  launch.arguments = {"exit", "1"};
  auto analyzer = StartAnalyzer(launch);
  ASSERT_NE(analyzer, nullptr);
  const AnalyzerStatus failed =
      WaitFor(*analyzer, [](const AnalyzerStatus& s) { return s.state == AnalyzerState::kFailed; });
  EXPECT_NE(failed.message.find("준비 전에 끝났습니다"), std::string::npos) << failed.message;
  EXPECT_NE(failed.message.find("종료 코드 1"), std::string::npos) << failed.message;
  EXPECT_NE(failed.message.find("Traceback"), std::string::npos) << failed.message;
  const auto dir = TempDir("before_ready");
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "ok")));
  // 같은 명령줄이므로 다시 준비 전에 끝나고, 요청은 대기열에 남습니다.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const AnalyzerStatus again =
      WaitFor(*analyzer, [](const AnalyzerStatus& s) { return s.state == AnalyzerState::kFailed; });
  EXPECT_EQ(again.queued, 1);
}

// 실행 파일이 없으면 Start가 이유와 함께 실패합니다.
TEST(Analyzer, StartFailsWithoutExecutable) {
  ProcessLaunch launch;
  launch.program = ToUtf8(TempDir("missing") / "no_python.exe");
  std::string error;
  EXPECT_EQ(Analyzer::Start(launch, &error), nullptr);
  EXPECT_NE(error.find("관류 분석 프로그램을 띄우지 못했습니다"), std::string::npos) << error;
}

// Stop은 분석 중인 요청을 취소로 끝내고 대기열을 비우며, 그 뒤 요청은 받지 않습니다.
TEST(Analyzer, StopCancelsWork) {
  const auto dir = TempDir("stop");
  auto analyzer = StartAnalyzer(FakeWorker());
  ASSERT_NE(analyzer, nullptr);
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "hang")));
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "ok")));
  WaitFor(*analyzer, [](const AnalyzerStatus& s) { return s.step == 1; });
  const auto begin = Clock::now();
  analyzer->Stop();
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin).count(), 2'000);
  const AnalyzerStatus stopped = analyzer->Status();
  EXPECT_EQ(stopped.state, AnalyzerState::kStopped);
  EXPECT_EQ(stopped.queued, 0);
  EXPECT_TRUE(stopped.last_done.cancelled);
  EXPECT_FALSE(analyzer->Submit(RequestNamed(dir, "ok")));
}

// 배포 폴더의 파이썬과 스크립트를 낮은 우선순위, 유휴 종료 없음, UTF-8 출력으로 띄웁니다.
TEST(AnalyzerLaunch, UsesBundledPythonAndScript) {
  const ProcessLaunch launch = AnalyzerLaunch("C:/앱/", "", "");
#ifdef _WIN32
  EXPECT_EQ(launch.program, "C:/앱/python/python.exe");
#else
  EXPECT_EQ(launch.program, "C:/앱/python/bin/python3");
#endif
  ASSERT_EQ(launch.arguments.size(), 3u);
  EXPECT_EQ(launch.arguments[0], "C:/앱/analysis/dfr_worker.py");
  EXPECT_EQ(launch.arguments[1], "--idle-exit");
  EXPECT_EQ(launch.arguments[2], "0");
  EXPECT_EQ(launch.working_dir, "C:/앱/analysis");
  EXPECT_TRUE(launch.low_priority);
  const std::vector<std::pair<std::string, std::string>> env = {
      {"PYTHONUNBUFFERED", "1"}, {"PYTHONUTF8", "1"}, {"PYTHONIOENCODING", "utf-8"}};
  EXPECT_EQ(launch.extra_env, env);
}

// 개발용으로 준 파이썬과 스크립트를 그대로 씁니다. PATH에서 찾지 않습니다.
TEST(AnalyzerLaunch, UsesOverrides) {
  const ProcessLaunch launch = AnalyzerLaunch("/app", "/venv/bin/python", "/src/analysis/dfr_worker.py");
  EXPECT_EQ(launch.program, "/venv/bin/python");
  EXPECT_EQ(launch.arguments[0], "/src/analysis/dfr_worker.py");
  EXPECT_EQ(launch.working_dir, "/src/analysis");
}

std::string PathList(const std::vector<std::filesystem::path>& folders) {
#ifdef _WIN32
  constexpr char kSeparator = ';';
#else
  constexpr char kSeparator = ':';
#endif
  std::string list;
  for (const auto& folder : folders) list += (list.empty() ? "" : std::string(1, kSeparator)) + ToUtf8(folder);
  return list;
}

#ifdef _WIN32
const char* const kPythonFile = "python3.exe";
#else
const char* const kPythonFile = "python3";
#endif

void WriteExecutable(const std::filesystem::path& path) {
  WriteFile(path, "x");
#ifndef _WIN32
  std::filesystem::permissions(path, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
#endif
}

// 스크립트의 상대 경로는 앱이 시작한 폴더를 앞에 붙여 절대 경로로 만듭니다. 분석 프로그램의 작업 폴더가 스크립트 폴더라서입니다.
TEST(AnalyzerOverrides, MakesScriptAbsoluteAgainstStartFolder) {
  using hwlab::platform::ResolveAnalyzerOverrides;
  const auto dir = TempDir("script_abs");
  const std::string cwd = ToUtf8(dir);
  EXPECT_EQ(ResolveAnalyzerOverrides("", "tools/fake_analysis/dfr_worker.py", cwd, "").script,
            cwd + "/tools/fake_analysis/dfr_worker.py");
  EXPECT_EQ(ResolveAnalyzerOverrides("", "./tools/../tools/dfr_worker.py", cwd, "").script, cwd + "/tools/dfr_worker.py");
  const std::string absolute = ToUtf8(dir / "elsewhere" / "dfr_worker.py");
  EXPECT_EQ(ResolveAnalyzerOverrides("", absolute, cwd, "").script, FromUtf8(absolute).generic_string());

  const ProcessLaunch launch = AnalyzerLaunch("/app", "", ResolveAnalyzerOverrides("", "tools/w.py", cwd, "").script);
  EXPECT_EQ(launch.working_dir, cwd + "/tools");
  EXPECT_EQ(launch.arguments[0], cwd + "/tools/w.py");
}

// 값을 주지 않으면 비워 두어 배포 폴더의 파이썬과 스크립트를 씁니다.
TEST(AnalyzerOverrides, KeepsEmptyValuesEmpty) {
  const auto resolved = hwlab::platform::ResolveAnalyzerOverrides("", "", "/start", "/usr/bin");
  EXPECT_EQ(resolved.python, "");
  EXPECT_EQ(resolved.script, "");
}

// 경로 구분자가 없는 파이썬 이름은 PATH 앞쪽 폴더부터 찾아 처음 나온 실행 파일의 절대 경로로 바꿉니다.
TEST(AnalyzerOverrides, FindsBarePythonNameOnPath) {
  const auto dir = TempDir("python_path");
  std::filesystem::create_directories(dir / "first");
  std::filesystem::create_directories(dir / "second");
  std::filesystem::create_directories(dir / "third");
  std::filesystem::create_directories(dir / "first" / kPythonFile);  // 같은 이름의 폴더는 건너뜁니다
  WriteExecutable(dir / "second" / kPythonFile);
  WriteExecutable(dir / "third" / kPythonFile);
  const std::string path_env = PathList({dir / "missing", dir / "first", dir / "second", dir / "third"});
  const auto resolved = hwlab::platform::ResolveAnalyzerOverrides("python3", "", "/start", path_env);
  EXPECT_EQ(resolved.python, (dir / "second" / kPythonFile).generic_string());
}

// 이름을 찾지 못하면 이름 그대로 두고, AnalyzerFilesProblem이 그 이름을 넣어 알립니다.
TEST(AnalyzerOverrides, ReportsBarePythonNameNotOnPath) {
  const auto dir = TempDir("python_missing");
  const auto resolved = hwlab::platform::ResolveAnalyzerOverrides("python3", "", "/start", PathList({dir, dir / "x"}));
  EXPECT_EQ(resolved.python, "python3");
  WriteFile(dir / "dfr_worker.py", "print()");
  const auto problem = hwlab::platform::AnalyzerFilesProblem(
      AnalyzerLaunch("/app", resolved.python, ToUtf8(dir / "dfr_worker.py")));
  ASSERT_TRUE(problem.has_value());
  EXPECT_NE(problem->find("파이썬 없음: python3"), std::string::npos) << *problem;
  EXPECT_NE(problem->find("PATH"), std::string::npos) << *problem;
}

// PATH의 상대 폴더와 구분자가 있는 상대 파이썬 경로는 시작 폴더 기준입니다.
TEST(AnalyzerOverrides, ResolvesRelativePythonAgainstStartFolder) {
  const auto dir = TempDir("python_relative");
  std::filesystem::create_directories(dir / "bin");
  std::filesystem::create_directories(dir / ".venv" / "bin");
  WriteExecutable(dir / "bin" / kPythonFile);
  const std::string cwd = ToUtf8(dir);
  const auto by_path = hwlab::platform::ResolveAnalyzerOverrides("python3", "", cwd, "bin");
  EXPECT_EQ(by_path.python, cwd + "/bin/" + kPythonFile);
  const auto by_separator = hwlab::platform::ResolveAnalyzerOverrides(".venv/bin/python", "", cwd, "");
  EXPECT_EQ(by_separator.python, cwd + "/.venv/bin/python");
}

#ifndef _WIN32
// 실행 권한이 없는 파일은 PATH에서 건너뜁니다.
TEST(AnalyzerOverrides, SkipsNonExecutableFileOnPath) {
  const auto dir = TempDir("python_not_exec");
  std::filesystem::create_directories(dir / "plain");
  std::filesystem::create_directories(dir / "real");
  WriteFile(dir / "plain" / "python3", "x");
  WriteExecutable(dir / "real" / "python3");
  const auto resolved =
      hwlab::platform::ResolveAnalyzerOverrides("python3", "", "/start", PathList({dir / "plain", dir / "real"}));
  EXPECT_NE(resolved.python.find("/real/python3"), std::string::npos) << resolved.python;
}
#endif

// 파이썬이나 스크립트가 없거나 비어 있으면 그 경로를 이유로 돌려줍니다.
TEST(AnalyzerLaunch, ReportsMissingFiles) {
  const auto dir = TempDir("files");
  const std::string python = ToUtf8(dir / "python.exe");
  const std::string script = ToUtf8(dir / "dfr_worker.py");
  auto problem = hwlab::platform::AnalyzerFilesProblem(AnalyzerLaunch(ToUtf8(dir), python, script));
  ASSERT_TRUE(problem.has_value());
  EXPECT_NE(problem->find("파이썬 없음"), std::string::npos) << *problem;
  WriteFile(dir / "python.exe", "x");
  WriteFile(dir / "dfr_worker.py", "");
  problem = hwlab::platform::AnalyzerFilesProblem(AnalyzerLaunch(ToUtf8(dir), python, script));
  ASSERT_TRUE(problem.has_value());
  EXPECT_NE(problem->find("분석 스크립트 크기 0"), std::string::npos) << *problem;
  WriteFile(dir / "dfr_worker.py", "print()");
  EXPECT_FALSE(hwlab::platform::AnalyzerFilesProblem(AnalyzerLaunch(ToUtf8(dir), python, script)).has_value());
}

// variants/<판>/VARIANT.json을 판 이름 순으로 읽고, id가 폴더 이름과 다르거나 없는 판은 빼고 이유를 남깁니다.
TEST(AnalyzerVariants, ReadsVariantFolders) {
  const auto dir = TempDir("variants");
  std::filesystem::create_directories(dir / "variants" / "v7");
  std::filesystem::create_directories(dir / "variants" / "v6");
  std::filesystem::create_directories(dir / "variants" / "bad");
  std::filesystem::create_directories(dir / "variants" / "empty");
  WriteFile(dir / "variants" / "v7" / "VARIANT.json", R"({"id": "v7", "label": "v7", "summary": "규칙 판정, 템플릿 v7",
    "upstream": {"repo": "r", "commit": "1f47dea", "date": "2026-10-28"},
    "release": {"tag": "models-v2",
      "classifier": {"name": "classifier.json", "file": "f", "uploaded": "2026-10-28", "sha256": "0a51"},
      "template": {"name": "template.onnx", "file": "g", "uploaded": "2026-10-28", "sha256": "aee1"}}})");
  WriteFile(dir / "variants" / "v6" / "VARIANT.json", R"({"id": "v6", "upstream": {"commit": "590da2d"}})");
  WriteFile(dir / "variants" / "bad" / "VARIANT.json", R"({"id": "other"})");
  std::string error;
  const auto variants = hwlab::platform::FindAnalyzerVariants(ToUtf8(dir), &error);
  ASSERT_EQ(variants.size(), 2u);
  EXPECT_EQ(variants[0].id, "v6");
  EXPECT_EQ(variants[0].label, "v6");  // label이 없으면 id
  EXPECT_EQ(variants[0].upstream_commit, "590da2d");
  EXPECT_EQ(variants[0].classifier.name, "");
  const auto& v7 = variants[1];
  EXPECT_EQ(v7.summary, "규칙 판정, 템플릿 v7");
  EXPECT_EQ(v7.upstream_date, "2026-10-28");
  EXPECT_EQ(v7.release_tag, "models-v2");
  EXPECT_EQ(v7.classifier.name, "classifier.json");
  EXPECT_EQ(v7.classifier.sha256, "0a51");
  EXPECT_EQ(v7.pulse_template.uploaded, "2026-10-28");
  EXPECT_NE(error.find("variants/bad/VARIANT.json"), std::string::npos) << error;

  const auto none = hwlab::platform::FindAnalyzerVariants(ToUtf8(dir / "nothing"), &error);
  EXPECT_TRUE(none.empty());
  EXPECT_NE(error.find("분석 판이 없습니다"), std::string::npos) << error;
}

// 판 하나를 올리는 실행 정보는 기본 인자 끝에 --variant를 더합니다.
TEST(AnalyzerVariants, AddsVariantArgument) {
  const ProcessLaunch launch =
      hwlab::platform::AnalyzerVariantLaunch(AnalyzerLaunch("/app", "", ""), "v7");
  ASSERT_EQ(launch.arguments.size(), 5u);
  EXPECT_EQ(launch.arguments[3], "--variant");
  EXPECT_EQ(launch.arguments[4], "v7");
  EXPECT_EQ(launch.working_dir, "/app/analysis");
}

// AnalyzerLaunch가 만든 명령줄(스크립트 경로, --idle-exit 0)로 띄운 관류 분석 프로그램과 요청을 주고받습니다.
TEST(AnalyzerLaunch, RunsFakeWorkerThroughLaunch) {
  const auto dir = TempDir("launch");
  WriteFile(dir / "fake_worker.py", "# fake");
  const ProcessLaunch launch = AnalyzerLaunch(ToUtf8(dir), HWLAB_FAKE_ANALYZER, ToUtf8(dir / "fake_worker.py"));
  ASSERT_FALSE(hwlab::platform::AnalyzerFilesProblem(launch).has_value());
  auto analyzer = StartAnalyzer(launch);
  ASSERT_NE(analyzer, nullptr);
  ASSERT_TRUE(analyzer->Submit(RequestNamed(dir, "ok")));
  EXPECT_TRUE(WaitFinished(*analyzer, 1).ok);
}

std::string Sidecar(const std::string& foot, int64_t written) {
  return "{\"schema_version\": 3, \"stream\": \"rgb\", \"foot\": \"" + foot +
         "\", \"format\": {\"width\": 640, \"height\": 1080}, \"crop\": {\"x\": 0, \"y\": 0, \"width\": 640, "
         "\"height\": 1080}, \"frames\": {\"written\": " +
         std::to_string(written) + ", \"written_in_rgb_window\": " + std::to_string(written) +
         ", \"missing\": 0, \"dropped\": 0}}";
}

// 촬영 폴더의 매니페스트와 사이드카로 요청(영상 경로, 장 수, 결과 폴더, 촬영 폴더 이름)을 만듭니다.
TEST(PerfusionRequestForShot, ReadsManifestAndSidecars) {
  const auto dir = TempDir("홍길동_19551103") / "102528_2";
  std::filesystem::create_directories(dir);
  WriteFile(dir / "102528_2_shot.json",
            "{\"schema_version\": 3, \"files\": {\"left_video\": \"102528_2_left.mkv\", \"left_sidecar\": "
            "\"102528_2_left.json\", \"right_video\": \"102528_2_right.mkv\", \"right_sidecar\": "
            "\"102528_2_right.json\", \"manifest\": \"102528_2_shot.json\"}}");
  WriteFile(dir / "102528_2_left.json", Sidecar("left", 900));
  WriteFile(dir / "102528_2_right.json", Sidecar("right", 899));
  const std::string folder = ToUtf8(dir);
  std::string error;
  const auto request = hwlab::platform::PerfusionRequestForShot(folder + "/", &error);
  ASSERT_TRUE(request.has_value()) << error;
  EXPECT_EQ(request->recording_dir, folder);
  EXPECT_EQ(request->left_video, folder + "/102528_2_left.mkv");
  EXPECT_EQ(request->right_video, folder + "/102528_2_right.mkv");
  EXPECT_EQ(request->left_frames, 900);
  EXPECT_EQ(request->right_frames, 899);
  EXPECT_EQ(request->output_dir, folder + "/perfusion");
  EXPECT_EQ(request->name, "102528_2");
}

// 지금 앱이 쓰는 사이드카(schema 7, 조명 기록 포함)로도 요청의 장 수를 읽습니다.
TEST(PerfusionRequestForShot, ReadsCurrentSidecarSchemaWithLight) {
  const auto dir = TempDir("schema4") / "102528";
  std::filesystem::create_directories(dir);
  WriteFile(dir / "102528_shot.json",
            "{\"files\": {\"left_video\": \"102528_left.mkv\", \"left_sidecar\": \"102528_left.json\", "
            "\"right_video\": \"102528_right.mkv\", \"right_sidecar\": \"102528_right.json\"}}");
  hwlab::record::Sidecar sidecar;
  sidecar.format = {hwlab::PixelFormat::kUyvy, 640, 1080, 30};
  sidecar.crop = {0, 0, 640, 1080};
  hwlab::record::LightRecord light;
  light.source = hwlab::record::LightSource::kApp;
  light.percent = 50;
  light.flicker = hwlab::pixel::Flicker{.frame_count = 900};
  sidecar.light = light;
  sidecar.foot = hwlab::record::FootSide::kLeft;
  sidecar.written_frames = 900;
  WriteFile(dir / "102528_left.json", hwlab::record::BuildSidecarJson(sidecar));
  sidecar.foot = hwlab::record::FootSide::kRight;
  sidecar.written_frames = 898;
  sidecar.light->source = hwlab::record::LightSource::kExternal;
  sidecar.light->percent.reset();
  WriteFile(dir / "102528_right.json", hwlab::record::BuildSidecarJson(sidecar));
  std::string error;
  const auto request = hwlab::platform::PerfusionRequestForShot(ToUtf8(dir), &error);
  ASSERT_TRUE(request.has_value()) << error;
  EXPECT_EQ(request->left_frames, 900);
  EXPECT_EQ(request->right_frames, 898);
}

// 매니페스트나 사이드카를 읽지 못하면 요청을 만들지 않고 이유를 남깁니다.
TEST(PerfusionRequestForShot, FailsWithoutManifestOrSidecar) {
  const auto dir = TempDir("broken") / "093000";
  std::filesystem::create_directories(dir);
  std::string error;
  EXPECT_FALSE(hwlab::platform::PerfusionRequestForShot(ToUtf8(dir), &error).has_value());
  EXPECT_NE(error.find("매니페스트를 열지 못했습니다"), std::string::npos) << error;
  WriteFile(dir / "093000_shot.json",
            "{\"files\": {\"left_video\": \"a.mkv\", \"left_sidecar\": \"a.json\", \"right_video\": \"b.mkv\", "
            "\"right_sidecar\": \"b.json\"}}");
  WriteFile(dir / "a.json", Sidecar("left", 900));
  EXPECT_FALSE(hwlab::platform::PerfusionRequestForShot(ToUtf8(dir), &error).has_value());
  EXPECT_NE(error.find("사이드카를 읽지 못했습니다"), std::string::npos) << error;
  EXPECT_NE(error.find("b.json"), std::string::npos) << error;
}

}  // namespace
