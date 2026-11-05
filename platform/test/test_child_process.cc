#include "hwlab/platform/child_process.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "hwlab/platform/perfusion_analysis.h"

namespace {

using hwlab::platform::AnalyzerLine;
using hwlab::platform::AnalyzerLineKind;
using hwlab::platform::ChildProcess;
using hwlab::platform::LineStatus;
using hwlab::platform::ParseAnalyzerLine;
using hwlab::platform::ProcessLaunch;
using Clock = std::chrono::steady_clock;

// Windows에서 std::string 경로는 코드 페이지로 읽히므로 UTF-8은 u8string으로 오갑니다.
std::filesystem::path FromUtf8(const std::string& text) {
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string ToUtf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

std::unique_ptr<ChildProcess> StartFake(const std::vector<std::string>& args, ProcessLaunch launch = {}) {
  if (launch.program.empty()) launch.program = HWLAB_FAKE_ANALYZER;
  launch.arguments = args;
  std::string error;
  auto process = ChildProcess::Start(launch, &error);
  EXPECT_NE(process, nullptr) << error;
  return process;
}

std::vector<std::string> ReadUntilClosed(ChildProcess& process) {
  std::vector<std::string> lines;
  std::string line;
  const auto deadline = Clock::now() + std::chrono::seconds(10);
  while (Clock::now() < deadline) {
    const LineStatus status = process.ReadLine(&line, 1000);
    if (status == LineStatus::kClosed) break;
    if (status == LineStatus::kLine) lines.push_back(line);
  }
  return lines;
}

// 원하는 종류의 관류 분석 프로그램 줄이 올 때까지 읽고, 그 사이의 줄을 함께 돌려줍니다.
AnalyzerLine ReadUntil(ChildProcess& process, AnalyzerLineKind kind, std::vector<std::string>* seen) {
  std::string line;
  const auto deadline = Clock::now() + std::chrono::seconds(10);
  while (Clock::now() < deadline) {
    const LineStatus status = process.ReadLine(&line, 1000);
    if (status == LineStatus::kClosed) break;
    if (status != LineStatus::kLine) continue;
    if (seen != nullptr) seen->push_back(line);
    const AnalyzerLine parsed = ParseAnalyzerLine(line);
    if (parsed.kind == kind) return parsed;
  }
  ADD_FAILURE() << "기다린 줄이 오지 않았습니다";
  return {};
}

std::filesystem::path TempDir(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_child_process" / FromUtf8(name);
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

// 진행 줄을 순서대로 읽고, 들여 쓴 하위 단계는 진행 단계로 세지 않습니다.
TEST(ChildProcess, ReadsProgressLinesInOrder) {
  auto process = StartFake({"progress"});
  ASSERT_NE(process, nullptr);
  const std::vector<std::string> lines = ReadUntilClosed(*process);
  std::vector<int32_t> steps;
  std::vector<std::string> messages;
  for (const std::string& line : lines) {
    const AnalyzerLine parsed = ParseAnalyzerLine(line);
    if (parsed.kind != AnalyzerLineKind::kStage) continue;
    steps.push_back(parsed.step);
    messages.push_back(parsed.message);
    EXPECT_EQ(parsed.total, 5);
  }
  EXPECT_EQ(steps, (std::vector<int32_t>{0, 1, 2, 5}));
  ASSERT_EQ(messages.size(), 4u);
  EXPECT_EQ(messages[1], "Reading videos");
  EXPECT_EQ(lines.size(), 5u);
  EXPECT_EQ(process->Wait(5000), 0);
}

// 상주 관류 분석 프로그램에 미리 올리기와 요청을 보내고, 요청 줄이 바이트 그대로 전달되며 완료 줄로 성공과 실패를 받습니다.
TEST(ChildProcess, ExchangesRequestsWithWorker) {
  auto process = StartFake({"worker", "fake-1"});
  ASSERT_NE(process, nullptr);
  const AnalyzerLine ready = ReadUntil(*process, AnalyzerLineKind::kReady, nullptr);
  EXPECT_EQ(ready.worker_version, "fake-1");
  EXPECT_EQ(ready.upstream_commit, "5e5178d");

  ASSERT_TRUE(process->WriteLine(hwlab::platform::BuildPreloadLine()));
  ReadUntil(*process, AnalyzerLineKind::kPreloaded, nullptr);

  hwlab::platform::PerfusionRequest request;
  request.recording_dir = "C:\\촬영\\20261019\\홍길동_19701015\\40\\101500";
  request.output_dir = request.recording_dir + "\\perfusion";
  request.name = "20261019_홍길동_19701015_40_101500";
  request.left_video = request.recording_dir + "\\101500_left.mkv";
  request.right_video = request.recording_dir + "\\101500_right.mkv";
  request.left_frames = 900;
  request.right_frames = 900;
  const std::string sent = hwlab::platform::BuildPerfusionRequestLine(request).value();
  ASSERT_TRUE(process->WriteLine(sent));
  std::vector<std::string> seen;
  const AnalyzerLine done = ReadUntil(*process, AnalyzerLineKind::kDone, &seen);
  EXPECT_TRUE(done.ok);
  EXPECT_EQ(done.error, "");
  ASSERT_FALSE(seen.empty());
  EXPECT_EQ(seen.front(), "[fake] request " + sent);
  int32_t stages = 0;
  for (const std::string& line : seen) stages += ParseAnalyzerLine(line).kind == AnalyzerLineKind::kStage ? 1 : 0;
  EXPECT_EQ(stages, 3);

  request.name = "fail";
  ASSERT_TRUE(process->WriteLine(hwlab::platform::BuildPerfusionRequestLine(request).value()));
  const AnalyzerLine failed = ReadUntil(*process, AnalyzerLineKind::kDone, nullptr);
  EXPECT_FALSE(failed.ok);
  EXPECT_EQ(failed.error, "left.avi 가 없습니다: C:\\촬영\\left.avi\n두 번째 줄");

  // 입력을 닫으면 관류 분석 프로그램이 스스로 0으로 끝납니다(앱이 닫힐 때의 경로).
  process->CloseInput();
  EXPECT_EQ(process->Wait(5000), 0);
  EXPECT_FALSE(process->WriteLine("{}"));
}

// 0이 아닌 종료 코드를 그대로 받고, 표준 오류의 줄도 같은 스트림으로 읽습니다.
TEST(ChildProcess, ReportsNonZeroExitCode) {
  auto process = StartFake({"exit", "3"});
  ASSERT_NE(process, nullptr);
  const std::vector<std::string> lines = ReadUntilClosed(*process);
  EXPECT_EQ(lines, (std::vector<std::string>{"exiting", "Traceback (most recent call last)"}));
  EXPECT_EQ(process->Wait(5000), 3);
}

// 응답 없는 관류 분석 프로그램은 제한 시간이 지나면 강제로 끝내고, 종료 코드는 kKilledExitCode입니다.
TEST(ChildProcess, KillsHungProcessAfterTimeout) {
  auto process = StartFake({"hang"});
  ASSERT_NE(process, nullptr);
  std::string line;
  ASSERT_EQ(process->ReadLine(&line, 5000), LineStatus::kLine);
  EXPECT_EQ(ParseAnalyzerLine(line).kind, AnalyzerLineKind::kStage);

  const auto before = Clock::now();
  EXPECT_EQ(process->ReadLine(&line, 300), LineStatus::kTimeout);
  EXPECT_EQ(process->Wait(300), std::nullopt);
  // Windows 대기 단위가 약 15ms라 하한에 여유를 둡니다.
  EXPECT_GE(Clock::now() - before, std::chrono::milliseconds(500));

  process->Kill();
  EXPECT_EQ(process->Wait(5000), hwlab::platform::kKilledExitCode);
  EXPECT_LT(Clock::now() - before, std::chrono::seconds(5));
  EXPECT_EQ(process->ReadLine(&line, 5000), LineStatus::kClosed);
  process->Kill();  // 이미 끝난 프로세스에 다시 불러도 아무 일도 없습니다.
}

// 공백, 따옴표, 역슬래시, 빈 인자, 한글 경로 인자가 바이트 그대로 자식에 도착합니다.
TEST(ChildProcess, PassesQuotedAndKoreanArguments) {
  const std::vector<std::string> values = {
      "C:\\촬영 폴더\\홍길동_19701015\\",
      "따옴표 \"안\" 글자",
      "",
      "a\\\\\"b",
      "탭\t있음",
      "plain",
      "끝 역슬래시 둘\\\\",
  };
  std::vector<std::string> args = {"args"};
  args.insert(args.end(), values.begin(), values.end());
  auto process = StartFake(args);
  ASSERT_NE(process, nullptr);
  const std::vector<std::string> lines = ReadUntilClosed(*process);
  ASSERT_EQ(lines.size(), values.size());
  for (size_t i = 0; i < values.size(); ++i) EXPECT_EQ(lines[i], "arg:" + values[i]);
  EXPECT_EQ(process->Wait(5000), 0);
}

// 한글 폴더에 있는 실행 파일을 한글 작업 폴더에서 띄우고, 더한 환경 변수가 자식에 보입니다.
TEST(ChildProcess, RunsFromKoreanFolderWithWorkingDirAndEnv) {
  const auto bin_dir = TempDir("관류 분석 프로그램 폴더 한글");
  const auto work_dir = TempDir("작업 폴더 홍길동");
  const std::filesystem::path source(HWLAB_FAKE_ANALYZER);
  const auto copied = bin_dir / source.filename();
  std::filesystem::copy_file(source, copied, std::filesystem::copy_options::overwrite_existing);

  ProcessLaunch launch;
  launch.program = ToUtf8(copied);
  launch.working_dir = ToUtf8(work_dir);
  auto cwd = StartFake({"cwd"}, launch);
  ASSERT_NE(cwd, nullptr);
  const std::vector<std::string> cwd_lines = ReadUntilClosed(*cwd);
  ASSERT_EQ(cwd_lines.size(), 1u);
  EXPECT_TRUE(std::filesystem::equivalent(FromUtf8(cwd_lines[0]), work_dir)) << cwd_lines[0];
  EXPECT_EQ(cwd->Wait(5000), 0);

  launch.working_dir.clear();
  launch.extra_env = {{"HWLAB_TEST_VALUE", "값 한글 C:\\경로"}, {"PYTHONUTF8", "1"}};
  auto env = StartFake({"env", "HWLAB_TEST_VALUE"}, launch);
  ASSERT_NE(env, nullptr);
  EXPECT_EQ(ReadUntilClosed(*env), (std::vector<std::string>{"값 한글 C:\\경로"}));
  EXPECT_EQ(env->Wait(5000), 0);

  cwd.reset();
  env.reset();
  std::filesystem::remove_all(bin_dir);
  std::filesystem::remove_all(work_dir);
}

// 호출자가 줄을 읽지 않아도 자식이 출력 파이프에서 막히지 않고 끝납니다.
TEST(ChildProcess, DrainsOutputWhileCallerDoesNotRead) {
  auto process = StartFake({"flood", "4000000"});
  ASSERT_NE(process, nullptr);
  EXPECT_EQ(process->Wait(20000), 0);
  const std::vector<std::string> lines = ReadUntilClosed(*process);
  ASSERT_FALSE(lines.empty());
  EXPECT_EQ(lines.back(), "flood-end");
  EXPECT_EQ(lines.size(), 40001u);
}

// 표준 출력과 표준 오류를 하나의 스트림으로 받습니다.
TEST(ChildProcess, MergesStandardError) {
  auto process = StartFake({"stderr"});
  ASSERT_NE(process, nullptr);
  const std::vector<std::string> lines = ReadUntilClosed(*process);
  EXPECT_EQ(lines, (std::vector<std::string>{"to-stdout", "to-stderr"}));
  EXPECT_EQ(process->Wait(5000), 0);
}

// 없는 실행 파일과 없는 작업 폴더는 띄우지 않고 이유를 돌려줍니다.
TEST(ChildProcess, FailsToStartMissingProgramOrFolder) {
  ProcessLaunch launch;
  launch.program = ToUtf8(std::filesystem::temp_directory_path() / "hwlab_no_such_analyzer");
  std::string error;
  EXPECT_EQ(ChildProcess::Start(launch, &error), nullptr);
  EXPECT_FALSE(error.empty());

  launch.program = HWLAB_FAKE_ANALYZER;
  launch.arguments = {"cwd"};
  launch.working_dir = ToUtf8(std::filesystem::temp_directory_path() / FromUtf8("hwlab_no_such_folder_없음"));
  error.clear();
  EXPECT_EQ(ChildProcess::Start(launch, &error), nullptr);
  EXPECT_FALSE(error.empty());
}

// 끝난 자식에 쓰면 false를 돌려주고 앱이 SIGPIPE로 끝나지 않습니다.
TEST(ChildProcess, WriteAfterExitFails) {
  auto process = StartFake({"progress"});
  ASSERT_NE(process, nullptr);
  ReadUntilClosed(*process);
  ASSERT_EQ(process->Wait(5000), 0);
  bool ok = true;
  // 파이프 버퍼에 들어가는 첫 쓰기는 성공할 수 있어 여러 번 씁니다.
  for (int i = 0; i < 100 && ok; ++i) ok = process->WriteLine(std::string(4096, 'y'));
  EXPECT_FALSE(ok);
}

// 낮은 우선순위로 띄우면 자식의 우선순위가 보통보다 낮습니다(Windows는 우선순위 클래스, 그 밖은 nice 값).
TEST(ChildProcess, StartsWithLowPriority) {
  auto normal = StartFake({"priority"});
  ASSERT_NE(normal, nullptr);
  const std::vector<std::string> normal_lines = ReadUntilClosed(*normal);
  ProcessLaunch launch;
  launch.low_priority = true;
  auto low = StartFake({"priority"}, launch);
  ASSERT_NE(low, nullptr);
  const std::vector<std::string> low_lines = ReadUntilClosed(*low);
  ASSERT_EQ(normal_lines.size(), 1u);
  ASSERT_EQ(low_lines.size(), 1u);
#ifdef _WIN32
  EXPECT_EQ(normal_lines[0], "normal");
  EXPECT_EQ(low_lines[0], "below_normal");
#else
  // 시험을 돌리는 프로세스의 nice 값이 이미 높을 수 있어 상한 19로 자른 값과 비교합니다.
  const int normal_nice = std::stoi(normal_lines[0]);
  EXPECT_EQ(std::stoi(low_lines[0]), std::min(normal_nice + hwlab::platform::kLowPriorityNice, 19));
#endif
  EXPECT_EQ(normal->Wait(5000), 0);
  EXPECT_EQ(low->Wait(5000), 0);
}

// 소멸자가 응답 없는 자식을 끝내고 거둡니다.
TEST(ChildProcess, DestructorKillsRunningChild) {
  const auto before = Clock::now();
  {
    auto process = StartFake({"hang"});
    ASSERT_NE(process, nullptr);
    std::string line;
    ASSERT_EQ(process->ReadLine(&line, 5000), LineStatus::kLine);
  }
  EXPECT_LT(Clock::now() - before, std::chrono::seconds(10));
}

}  // namespace
