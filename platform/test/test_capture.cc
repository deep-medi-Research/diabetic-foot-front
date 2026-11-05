#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "hwlab/platform/capture.h"
#include "hwlab/platform/fake_camera.h"
#include "hwlab/platform/video_reader.h"

#ifdef HWLAB_HAS_FFV1
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#endif

namespace {

using namespace std::chrono_literals;
using hwlab::CameraState;
using hwlab::SettingMode;
using hwlab::flow::CaptureSpec;
using hwlab::flow::CommandKind;
using hwlab::flow::EndReason;
using hwlab::flow::EventKind;
using hwlab::platform::Capture;
using hwlab::platform::FakeCameraScript;
using hwlab::platform::FakeRgbCamera;

// 실험 노트북 카메라에서 읽은 값과 같은 초기 상태입니다.
const CameraState kInitial{false, -5, 4, false, 4600, 60, true};

CaptureSpec SmallSpec(SettingMode mode) {
  CaptureSpec spec;
  spec.device = "Fake";
  spec.width = 64;
  spec.height = 32;
  spec.settings = {mode, -6, 8, 4600, 30};
  spec.settle = {4, 3, 3, 10};
  spec.target_frames = 60;
  spec.clip_level = 250;
  // 목표보다 크게 잡아 쓰기 스레드가 잠깐 멈춰도 저장 누락이 생기지 않게 합니다. 누락은 따로 시험합니다.
  spec.buffer_frames = 64;
  spec.codec = hwlab::flow::VideoCodec::kUncompressed;
  return spec;
}

struct Paths {
  std::string video;
  std::string sidecar;
};

Paths TempPaths(const char* name) {
  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_capture";
  std::filesystem::create_directories(dir);
  return {(dir / (std::string(name) + ".avi")).string(), (dir / (std::string(name) + ".json")).string()};
}

std::string ReadText(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

// 미리보기 몇 장 뒤 촬영 버튼을 누르고 끝날 때까지 기다립니다.
void RunToEnd(Capture& capture, int preview_frames, const Paths& paths) {
  while (capture.Status().received < preview_frames) std::this_thread::sleep_for(1ms);
  capture.StartRecording(paths.video, paths.sidecar);
  ASSERT_TRUE(capture.WaitFinished(10s));
}

// 직접 지정 선언대로 설정하고, 목표 프레임 수만큼 쓰고, 사이드카를 남기고, 실행 전 설정으로 되돌립니다.
TEST(Capture, ManualRecordsTargetFramesAndRestores) {
  FakeRgbCamera camera({.initial = kInitial});
  const Paths paths = TempPaths("manual");
  Capture capture(camera, SmallSpec(SettingMode::kManual));
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;

  RunToEnd(capture, 20, paths);
  const auto status = capture.Status();
  const auto restore = capture.Stop();

  EXPECT_EQ(status.written, 60);
  EXPECT_EQ(status.dropped, 0);
  EXPECT_EQ(status.missing, 0);
  EXPECT_EQ(status.end_reason, EndReason::kCompleted);
  EXPECT_TRUE(status.error.empty()) << status.error;
  EXPECT_GT(std::filesystem::file_size(paths.video), 60u * 64 * 32 * 2);
  const std::string json = ReadText(paths.sidecar);
  EXPECT_NE(json.find("\"written\": 60"), std::string::npos);
  EXPECT_NE(json.find("\"exposure_step\": -6"), std::string::npos);
  EXPECT_TRUE(restore.mismatches.empty());
  EXPECT_EQ(camera.State(), kInitial);
  // 프레임이 오는 카메라는 스트림을 닫지 않고 끝냅니다. 닫으면 설정 되돌림이 실패할 수 있습니다.
  EXPECT_EQ(camera.Interrupts(), 0);
}

// Stop을 다른 스레드에서 부르고 limit 안에 끝나는지 봅니다. 끝나지 않으면 카메라를 깨워 테스트가 멈추지 않게 합니다.
bool StopsWithin(Capture& capture, FakeRgbCamera& camera, std::chrono::milliseconds limit,
                 hwlab::platform::ReconcileResult* restore, std::vector<std::string>* steps) {
  auto done = std::async(std::launch::async, [&] {
    *restore = capture.Stop([steps](const char* step) { steps->push_back(step); });
  });
  if (done.wait_for(limit) == std::future_status::ready) return true;
  camera.Interrupt();
  done.wait();
  return false;
}

// 카메라가 프레임을 보내지 않으면 ReadFrame이 돌아오지 않습니다. Stop은 스트림을 닫아 수신을 끝내고 설정을 되돌립니다
// (2026-10-27 노트북: 종료가 끝나지 않고 프로세스가 장치를 잡은 채 남음).
TEST(Capture, StopEndsWhenCameraStopsSendingFrames) {
  FakeCameraScript script{.initial = kInitial};
  script.stall_after = 20;
  FakeRgbCamera camera(script);
  Capture capture(camera, SmallSpec(SettingMode::kManual));
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  while (capture.Status().received < 20) std::this_thread::sleep_for(1ms);

  hwlab::platform::ReconcileResult restore;
  std::vector<std::string> steps;
  const auto begin = std::chrono::steady_clock::now();
  ASSERT_TRUE(StopsWithin(capture, camera, 10s, &restore, &steps));

  EXPECT_LT(std::chrono::steady_clock::now() - begin, 5s);
  EXPECT_EQ(camera.Interrupts(), 1);
  EXPECT_NE(std::find(steps.begin(), steps.end(), "RGB 수신 멈춤, 카메라 스트림을 닫음"), steps.end());
  EXPECT_EQ(steps.back(), "카메라 설정 되돌림");
  EXPECT_TRUE(restore.failed.empty());
  EXPECT_EQ(camera.State(), kInitial);
}

// 녹화 중에 프레임이 끊긴 채 종료해도 그때까지 쓴 파일과 사이드카를 닫습니다.
TEST(Capture, StopDuringStalledRecordingClosesFiles) {
  FakeCameraScript script{.initial = kInitial};
  script.stall_after = 40;
  FakeRgbCamera camera(script);
  const Paths paths = TempPaths("stall_recording");
  auto spec = SmallSpec(SettingMode::kManual);
  spec.target_frames = 100000;
  Capture capture(camera, spec);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  while (capture.Status().received < 10) std::this_thread::sleep_for(1ms);
  capture.StartRecording(paths.video, paths.sidecar);
  while (capture.Status().received < 40) std::this_thread::sleep_for(1ms);

  hwlab::platform::ReconcileResult restore;
  std::vector<std::string> steps;
  ASSERT_TRUE(StopsWithin(capture, camera, 10s, &restore, &steps));

  const auto status = capture.Status();
  EXPECT_GT(status.written, 0);
  EXPECT_NE(ReadText(paths.sidecar).find("\"written\": " + std::to_string(status.written)), std::string::npos);
  EXPECT_EQ(camera.State(), kInitial);
}

// 실행 전 자동 모드였던 노출과 색온도는 모드와 저장값을 모두 되돌립니다(2026-10-19 실기: 저장값이 녹화 값으로 남았음).
TEST(Capture, RestoresStoredValuesOfAutoMode) {
  const CameraState initial{true, -7, 4, true, 4000, 60, false};
  FakeRgbCamera camera({.initial = initial});
  const Paths paths = TempPaths("restore_auto");
  Capture capture(camera, SmallSpec(SettingMode::kManual));
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;

  RunToEnd(capture, 20, paths);
  const auto restore = capture.Stop();

  EXPECT_TRUE(restore.failed.empty());
  EXPECT_TRUE(restore.mismatches.empty());
  EXPECT_EQ(camera.State(), initial);
  EXPECT_EQ(restore.readback, initial);
}

// USB 재연결 직후처럼 실행 전 fps를 0으로 읽었으면 fps는 되돌리지 않고 값 모름 항목으로 알립니다.
TEST(Capture, RestoreLeavesFrameRateWhenSavedZero) {
  CameraState initial = kInitial;
  initial.sdk_frame_rate = 0;
  FakeRgbCamera camera({.initial = initial});
  const Paths paths = TempPaths("restore_fps0");
  Capture capture(camera, SmallSpec(SettingMode::kManual));
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;

  RunToEnd(capture, 20, paths);
  const auto restore = capture.Stop();

  EXPECT_TRUE(restore.failed.empty());
  EXPECT_TRUE(restore.mismatches.empty());
  EXPECT_EQ(restore.unknown_saved, std::vector<hwlab::flow::SettingField>{hwlab::flow::SettingField::kSdkFrameRate});
  for (const auto& command : restore.executed) EXPECT_NE(command.kind, CommandKind::kSdkFrameRate);
  EXPECT_EQ(camera.State().sdk_frame_rate, 30);
}

// 카메라가 빠뜨린 프레임은 카메라 단 누락으로 한 번만 셉니다.
TEST(Capture, CountsCameraGapOnce) {
  FakeCameraScript script{.initial = kInitial};
  // 녹화 시작 시점은 OS의 대기 시간 단위에 따라 늦어질 수 있어(Windows 약 15ms), 빠지는 자리를 녹화 범위 한가운데에 둡니다.
  script.skips[150] = 2;
  FakeRgbCamera camera(script);
  const Paths paths = TempPaths("gap");
  auto spec = SmallSpec(SettingMode::kManual);
  spec.target_frames = 300;
  spec.buffer_frames = 310;
  Capture capture(camera, spec);
  std::string error;
  ASSERT_TRUE(capture.Start(&error));

  RunToEnd(capture, 20, paths);
  const auto status = capture.Status();
  capture.Stop();

  EXPECT_EQ(status.missing, 2);
  EXPECT_EQ(status.dropped, 0);
  EXPECT_EQ(status.written, 300);
}

// 자동 맞춤은 미리보기에서 잠금을 풀고, 촬영 버튼 뒤에 잠근 상태로 녹화합니다.
TEST(Capture, AutoLocksBeforeRecording) {
  FakeRgbCamera camera({.initial = kInitial});
  const Paths paths = TempPaths("auto");
  Capture capture(camera, SmallSpec(SettingMode::kAuto));
  std::string error;
  ASSERT_TRUE(capture.Start(&error));

  RunToEnd(capture, 20, paths);
  capture.Stop();

  const auto commands = camera.ExecutedCommands();
  int unlock_at = -1, lock_at = -1;
  for (int i = 0; i < static_cast<int>(commands.size()); ++i) {
    if (commands[i].kind == CommandKind::kUnlock && unlock_at < 0) unlock_at = i;
    if (commands[i].kind == CommandKind::kLock && lock_at < 0) lock_at = i;
  }
  ASSERT_GE(unlock_at, 0);
  EXPECT_GT(lock_at, unlock_at);
  const std::string json = ReadText(paths.sidecar);
  EXPECT_NE(json.find("\"mode\": \"auto\""), std::string::npos);
  EXPECT_NE(json.find("\"locked\": true"), std::string::npos);
  EXPECT_NE(json.find("\"exposure_step\": null"), std::string::npos);
}

// 사이드카 per_frame의 첫 프레임 번호입니다.
int64_t FirstRecordedIndex(const std::string& json) {
  const size_t per_frame = json.find("\"per_frame\"");
  const size_t at = per_frame == std::string::npos ? per_frame : json.find("\"index\": [", per_frame);
  if (at == std::string::npos) return -1;
  return std::stoll(json.substr(at + std::string("\"index\": [").size()));
}

// PPG 안정 대기를 주면 촬영 버튼 뒤 그 시간만큼의 RGB 프레임(선언 fps로 셈)을 받은 뒤에 녹화를 시작합니다.
TEST(Capture, WaitsForPpgSettleBeforeRecording) {
  FakeRgbCamera camera({.initial = kInitial});
  const Paths paths = TempPaths("ppg_settle");
  Capture capture(camera, SmallSpec(SettingMode::kManual));
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  while (capture.Status().received < 20) std::this_thread::sleep_for(1ms);

  hwlab::platform::RecordingPaths recording;
  recording.video = paths.video;
  recording.sidecar = paths.sidecar;
  recording.ppg_settle_us = 10'000'000;  // 30fps에서 300프레임
  const int64_t pressed_at = capture.Status().received;
  capture.StartRecording(recording);
  ASSERT_TRUE(capture.WaitFinished(10s));
  const auto status = capture.Status();
  capture.Stop();

  EXPECT_EQ(status.written, 60);
  EXPECT_EQ(status.end_reason, EndReason::kCompleted);
  const int64_t first = FirstRecordedIndex(ReadText(paths.sidecar));
  ASSERT_GE(first, 0);
  // 가짜 카메라는 프레임을 빨리 내보내므로, 버튼 직전에 읽은 받은 프레임 수와 실제 버튼 사이에 수십 장이 더 들어올 수 있습니다
  EXPECT_GE(first - pressed_at, 300 - 1);
  EXPECT_LE(first - pressed_at, 300 + 150);
}

// PPG 안정 대기가 0이면 직접 지정 촬영은 기다리지 않고 녹화를 시작합니다.
TEST(Capture, RecordsWithoutPpgSettleWhenZero) {
  FakeRgbCamera camera({.initial = kInitial});
  const Paths paths = TempPaths("ppg_settle_zero");
  Capture capture(camera, SmallSpec(SettingMode::kManual));
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  while (capture.Status().received < 20) std::this_thread::sleep_for(1ms);

  const int64_t pressed_at = capture.Status().received;
  capture.StartRecording(paths.video, paths.sidecar);
  ASSERT_TRUE(capture.WaitFinished(10s));
  capture.Stop();

  const int64_t first = FirstRecordedIndex(ReadText(paths.sidecar));
  ASSERT_GE(first, 0);
  EXPECT_LT(first - pressed_at, 150);
}

// 녹화 중 연결이 끊기면 그때까지 쓴 파일과 사이드카를 닫고 이유를 남깁니다.
TEST(Capture, DisconnectEndsRecording) {
  FakeCameraScript script{.initial = kInitial};
  script.disconnect_after = 50;
  FakeRgbCamera camera(script);
  const Paths paths = TempPaths("disconnect");
  Capture capture(camera, SmallSpec(SettingMode::kManual));
  std::string error;
  ASSERT_TRUE(capture.Start(&error));

  RunToEnd(capture, 5, paths);
  const auto status = capture.Status();
  capture.Stop();

  EXPECT_EQ(status.end_reason, EndReason::kDisconnected);
  EXPECT_GT(status.written, 0);
  EXPECT_LT(status.written, 60);
  EXPECT_NE(ReadText(paths.sidecar).find("\"written\": " + std::to_string(status.written)), std::string::npos);
}

// 끊긴 카메라가 다시 연결되면 공장 기본값으로 돌아간 설정을 선언대로 다시 맞추고 미리보기로 돌아갑니다.
TEST(Capture, ReconnectReappliesSettings) {
  FakeCameraScript script{.initial = kInitial};
  script.disconnect_after = 30;
  script.reopen_failures = 2;  // 두 번 실패한 뒤(1초) 다시 연결됩니다
  script.factory = {true, std::nullopt, 1, true, 4500, 60, false};
  FakeRgbCamera camera(script);
  Capture capture(camera, SmallSpec(SettingMode::kManual));
  std::string error;
  ASSERT_TRUE(capture.Start(&error));

  while (capture.Status().reconnects == 0) std::this_thread::sleep_for(1ms);
  while (camera.State().gain != 8) std::this_thread::sleep_for(1ms);
  while (capture.Status().received < 40) std::this_thread::sleep_for(1ms);
  const auto status = capture.Status();
  // Stop은 실행 전 설정으로 되돌리므로 그 전에 읽습니다.
  const CameraState state = camera.State();
  capture.Stop();

  EXPECT_EQ(status.reconnects, 1);
  EXPECT_EQ(status.phase, hwlab::flow::Phase::kPreview);
  EXPECT_EQ(state.exposure_auto, false);
  EXPECT_EQ(state.exposure_step, -6);
  EXPECT_EQ(state.sdk_frame_rate, 30);
  EXPECT_EQ(state.locked, true);
}

// 녹화 중 연결이 끊긴 뒤 다시 연결되면 다음 촬영을 할 수 있습니다.
TEST(Capture, RecordsAgainAfterReconnect) {
  FakeCameraScript script{.initial = kInitial};
  script.disconnect_after = 50;
  script.reopen_failures = 0;
  script.factory = {true, std::nullopt, 1, true, 4500, 60, false};
  FakeRgbCamera camera(script);
  const Paths first = TempPaths("reconnect_first");
  const Paths second = TempPaths("reconnect_second");
  Capture capture(camera, SmallSpec(SettingMode::kManual));
  std::string error;
  ASSERT_TRUE(capture.Start(&error));

  RunToEnd(capture, 5, first);
  EXPECT_EQ(capture.Status().end_reason, EndReason::kDisconnected);
  while (capture.Status().reconnects == 0 || capture.Status().phase != hwlab::flow::Phase::kPreview) {
    std::this_thread::sleep_for(1ms);
  }
  const auto before = capture.Status().received;
  while (capture.Status().received < before + 5) std::this_thread::sleep_for(1ms);
  RunToEnd(capture, 0, second);
  const auto status = capture.Status();
  capture.Stop();

  EXPECT_EQ(status.end_reason, EndReason::kCompleted);
  EXPECT_EQ(status.written, 60);
  EXPECT_NE(ReadText(second.sidecar).find("\"max_queue_frames\""), std::string::npos);
  EXPECT_NE(ReadText(second.sidecar).find("\"file_bytes\": " + std::to_string(std::filesystem::file_size(second.video))),
            std::string::npos);
}

// 카메라가 fps를 절반으로 낮춘 상태면 녹화를 시작하지 않고 이유를 남깁니다(2026-10-16 실기 15fps).
TEST(Capture, RefusesRecordingWhenFrameRateDiffers) {
  FakeCameraScript script{.initial = kInitial};
  script.device_period_us = 66751;
  FakeRgbCamera camera(script);
  const Paths paths = TempPaths("half_fps");
  std::filesystem::remove(paths.video);
  std::filesystem::remove(paths.sidecar);
  Capture capture(camera, SmallSpec(SettingMode::kAuto));
  std::string error;
  ASSERT_TRUE(capture.Start(&error));

  RunToEnd(capture, 20, paths);
  const auto status = capture.Status();
  capture.Stop();

  EXPECT_EQ(status.written, 0);
  EXPECT_NE(status.error.find("15.0fps"), std::string::npos) << status.error;
  EXPECT_FALSE(std::filesystem::exists(paths.video));
}

// 녹화 중 중지는 파일과 사이드카를 닫습니다.
TEST(Capture, StopDuringRecordingClosesFiles) {
  FakeRgbCamera camera({.initial = kInitial});
  const Paths paths = TempPaths("stop");
  auto spec = SmallSpec(SettingMode::kManual);
  spec.target_frames = 100000;
  Capture capture(camera, spec);
  std::string error;
  ASSERT_TRUE(capture.Start(&error));
  while (capture.Status().received < 10) std::this_thread::sleep_for(1ms);
  capture.StartRecording(paths.video, paths.sidecar);
  while (capture.Status().written < 30) std::this_thread::sleep_for(1ms);

  capture.Stop();

  EXPECT_EQ(capture.Status().end_reason, EndReason::kStopped);
  EXPECT_NE(ReadText(paths.sidecar).find("\"target\": 100000"), std::string::npos);
}

// 버퍼가 모자라 저장 누락이 생기면 그만큼 더 받아 파일에 목표 프레임 수를 채웁니다(표 2: 파일에 쓴 프레임만 셈).
TEST(Capture, DroppedFramesAreReplacedToReachTarget) {
  FakeCameraScript script{.initial = kInitial};
  script.wall_period_us = 0;
  FakeRgbCamera camera(script);
  auto spec = SmallSpec(SettingMode::kManual);
  spec.buffer_frames = 1;
  spec.target_frames = 300;
  const Paths paths = TempPaths("dropped");
  Capture capture(camera, spec);
  std::string error;
  ASSERT_TRUE(capture.Start(&error));

  RunToEnd(capture, 20, paths);
  const auto status = capture.Status();
  capture.Stop();

  EXPECT_EQ(status.written, 300);
  EXPECT_GT(status.dropped, 0);
  EXPECT_EQ(status.missing, 0);  // 저장 누락은 카메라 단 누락으로 세지 않습니다
  const std::string json = ReadText(paths.sidecar);
  EXPECT_NE(json.find("\"dropped\": " + std::to_string(status.dropped)), std::string::npos);
  EXPECT_NE(json.find("\"written\": 300"), std::string::npos);
}

#ifdef HWLAB_HAS_FFV1
// 저장 누락이 있어도 영상 파일에 든 장 수가 목표 프레임 수입니다. 분석은 이 파일을 읽습니다.
TEST(Capture, VideoHoldsTargetFramesDespiteDrops) {
  FakeCameraScript script{.initial = kInitial};
  script.wall_period_us = 0;
  FakeRgbCamera camera(script);
  auto spec = SmallSpec(SettingMode::kManual);
  spec.buffer_frames = 1;
  spec.target_frames = 300;
  spec.codec = hwlab::flow::VideoCodec::kFfv1;
  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_capture";
  std::filesystem::create_directories(dir);
  const Paths paths{(dir / "dropped.mkv").string(), (dir / "dropped_mkv.json").string()};
  Capture capture(camera, spec);
  std::string error;
  ASSERT_TRUE(capture.Start(&error));

  RunToEnd(capture, 20, paths);
  const auto status = capture.Status();
  capture.Stop();

  EXPECT_GT(status.dropped, 0);
  auto reader = hwlab::platform::MakeVideoReader(&error);
  ASSERT_TRUE(reader && reader->Open(paths.video, &error)) << error;
  EXPECT_EQ(reader->FrameCount(), 300);
}
#endif

// 영상 파일을 열지 못하면 모든 프레임이 저장 누락이고, 사이드카는 파일에 없는 프레임을 적지 않습니다.
TEST(Capture, UnwritableVideoLeavesNoFrameRecords) {
  FakeRgbCamera camera({.initial = kInitial});
  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_capture";
  std::filesystem::create_directories(dir);
  // 같은 이름의 폴더가 있으면 그 경로에 파일을 만들 수 없습니다.
  const auto blocked = dir / "unwritable.avi";
  std::filesystem::remove_all(blocked);
  std::filesystem::create_directories(blocked);
  const Paths paths{blocked.string(), (dir / "unwritable.json").string()};
  auto spec = SmallSpec(SettingMode::kManual);
  spec.target_frames = 10;
  Capture capture(camera, spec);
  std::string error;
  ASSERT_TRUE(capture.Start(&error));
  while (capture.Status().received < 5) std::this_thread::sleep_for(1ms);
  ASSERT_TRUE(capture.StartRecording(paths.video, paths.sidecar));
  // 쓰기 스레드의 실패는 촬영 흐름에 알리지 않으므로 목표 장 수만큼 받고 끝납니다. 열지 못한 파일은 오류로 남습니다.
  ASSERT_TRUE(capture.WaitFinished(10s));
  const auto status = capture.Status();
  capture.Stop();

  EXPECT_EQ(status.written, 0);
  EXPECT_EQ(status.dropped, 10);
  EXPECT_FALSE(status.error.empty());
  const std::string json = ReadText(paths.sidecar);
  EXPECT_NE(json.find("\"written\": 0"), std::string::npos) << json.substr(0, 400);
  EXPECT_NE(json.find("\"index\": []"), std::string::npos);
}

// 앞 녹화의 사이드카를 다 쓰기 전에는 다음 녹화를 받지 않습니다. 받으면 다음 녹화가 앞 녹화의 누락 수와 설정값을 지웁니다.
// 녹화가 끝나 미리보기로 돌아오자마자 다음 녹화를 거듭 눌러도 각 사이드카는 자기 녹화의 장 수를 적습니다.
TEST(Capture, BackToBackRecordingsKeepTheirOwnSidecars) {
  FakeRgbCamera camera({.initial = kInitial});
  auto spec = SmallSpec(SettingMode::kManual);
  spec.target_frames = 30;
  spec.codec = hwlab::flow::VideoCodec::kUncompressed;
  Capture capture(camera, spec);
  std::string error;
  ASSERT_TRUE(capture.Start(&error));
  while (capture.Status().received < 5) std::this_thread::sleep_for(1ms);

  std::vector<Paths> shots;
  for (int i = 0; i < 4; ++i) shots.push_back(TempPaths(("back_to_back_" + std::to_string(i)).c_str()));
  for (const Paths& paths : shots) std::filesystem::remove(paths.sidecar);
  int refused = 0;
  for (size_t i = 0; i < shots.size(); ++i) {
    // 미리보기로 돌아온 직후부터 받아 줄 때까지 누릅니다.
    while (!capture.StartRecording(shots[i].video, shots[i].sidecar)) {
      ++refused;
      std::this_thread::sleep_for(100us);
    }
    while (capture.Status().phase == hwlab::flow::Phase::kPreview && !capture.Status().finished) {
      std::this_thread::sleep_for(100us);
    }
    while (capture.Status().phase != hwlab::flow::Phase::kPreview) std::this_thread::sleep_for(100us);
  }
  ASSERT_TRUE(capture.WaitFinished(10s));
  capture.Stop();

  EXPECT_GT(refused, 0);  // 녹화 중과 사이드카를 쓰는 동안은 받지 않았습니다
  for (const Paths& paths : shots) {
    const std::string json = ReadText(paths.sidecar);
    EXPECT_NE(json.find("\"written\": 30"), std::string::npos) << paths.sidecar;
    EXPECT_NE(json.find("\"dropped\": 0"), std::string::npos) << paths.sidecar;
  }
}

// 녹화 중에는 다음 녹화를 받지 않고 false를 돌려줍니다. 부른 쪽은 끝났다는 알림을 기다리지 않습니다.
TEST(Capture, RefusesStartWhileRecording) {
  FakeRgbCamera camera({.initial = kInitial});
  auto spec = SmallSpec(SettingMode::kManual);
  spec.target_frames = 100000;
  const Paths first = TempPaths("refuse_first");
  const Paths second = TempPaths("refuse_second");
  Capture capture(camera, spec);
  std::string error;
  ASSERT_TRUE(capture.Start(&error));
  while (capture.Status().received < 5) std::this_thread::sleep_for(1ms);
  ASSERT_TRUE(capture.StartRecording(first.video, first.sidecar));
  while (capture.Status().written < 5) std::this_thread::sleep_for(1ms);

  EXPECT_FALSE(capture.StartRecording(second.video, second.sidecar));
  capture.Stop();
}

// 미리보기 중 설정을 바꾸면 새 목표로 맞추고, 녹화 중에는 거부합니다.
TEST(Capture, UpdatesSettingsOnlyInPreview) {
  FakeRgbCamera camera({.initial = kInitial});
  auto spec = SmallSpec(SettingMode::kManual);
  spec.target_frames = 100000;
  const Paths paths = TempPaths("update");
  Capture capture(camera, spec);
  std::string error;
  ASSERT_TRUE(capture.Start(&error));
  while (capture.Status().received < 5) std::this_thread::sleep_for(1ms);

  auto changed = spec.settings;
  changed.gain = 16;
  ASSERT_TRUE(capture.UpdateSettings(changed));
  while (camera.State().gain != 16) std::this_thread::sleep_for(1ms);
  capture.StartRecording(paths.video, paths.sidecar);
  while (capture.Status().written < 5) std::this_thread::sleep_for(1ms);
  changed.gain = 32;
  const bool accepted_while_recording = capture.UpdateSettings(changed);
  capture.Stop();

  EXPECT_FALSE(accepted_while_recording);
  EXPECT_NE(ReadText(paths.sidecar).find("\"gain\": 16"), std::string::npos);
}

// 미리보기 함수는 정한 간격마다 수신 스레드에서 불립니다.
TEST(Capture, CallsPreviewSinkEveryNFrames) {
  FakeRgbCamera camera({.initial = kInitial});
  Capture capture(camera, SmallSpec(SettingMode::kManual));
  std::atomic<int> calls{0};
  capture.SetPreviewSink(5, [&](const hwlab::FrameView& view) {
    if (view.width == 64) ++calls;
  });
  std::string error;
  ASSERT_TRUE(capture.Start(&error));
  while (capture.Status().received < 50) std::this_thread::sleep_for(1ms);
  capture.Stop();

  EXPECT_GE(calls.load(), 10);
}

#ifdef HWLAB_HAS_FFV1
// FFV1(MKV)로 녹화한 파일을 libav로 다시 풀면 가짜 카메라가 보낸 화소와 같습니다(무손실).
// 가짜 카메라 프레임은 Y가 프레임마다 1씩 커지고 U, V는 128입니다.
TEST(Capture, Ffv1RoundTripsLosslessly) {
  FakeRgbCamera camera({.initial = kInitial});
  auto spec = SmallSpec(SettingMode::kManual);
  spec.codec = hwlab::flow::VideoCodec::kFfv1;
  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_capture";
  std::filesystem::create_directories(dir);
  const Paths paths{(dir / "ffv1.mkv").string(), (dir / "ffv1.json").string()};
  Capture capture(camera, spec);
  std::string error;
  ASSERT_TRUE(capture.Start(&error));
  RunToEnd(capture, 20, paths);
  const auto status = capture.Status();
  capture.Stop();
  ASSERT_TRUE(status.error.empty()) << status.error;
  ASSERT_EQ(status.written, 60);

  AVFormatContext* input = nullptr;
  ASSERT_EQ(avformat_open_input(&input, paths.video.c_str(), nullptr, nullptr), 0);
  // fps는 파일에 적힌 장당 길이(DefaultDuration)에서 와야 합니다. 머리글만 연 지금 값이 그것이고, 없으면 0/0입니다.
  // 없으면 읽는 쪽이 앞 몇 장의 시각으로 짐작해, 장이 큰 실기 파일에서 29.97로 읽고 899장으로 셌습니다.
  EXPECT_EQ(input->streams[0]->avg_frame_rate.num, 30);
  EXPECT_EQ(input->streams[0]->avg_frame_rate.den, 1);
  ASSERT_GE(avformat_find_stream_info(input, nullptr), 0);
  const AVStream* stream = input->streams[0];
  ASSERT_EQ(stream->codecpar->codec_id, AV_CODEC_ID_FFV1);
  // 관류 분석의 OpenCV는 컨테이너 길이 x fps로 장 수를 셉니다. 길이에 마지막 장이 들어가야 60장으로 셉니다.
  EXPECT_EQ(av_rescale(input->duration, 30, AV_TIME_BASE), 60);
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_FFV1);
  AVCodecContext* decoder = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(decoder, stream->codecpar);
  ASSERT_EQ(avcodec_open2(decoder, codec, nullptr), 0);
  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  int decoded = 0;
  int previous_y = -1;
  bool exact = true;
  const auto take = [&] {
    while (avcodec_receive_frame(decoder, frame) == 0) {
      const int y = frame->data[0][0];
      for (int r = 0; r < 32; ++r) {
        for (int c = 0; c < 64; ++c) exact = exact && frame->data[0][r * frame->linesize[0] + c] == y;
        for (int c = 0; c < 32; ++c) {
          exact = exact && frame->data[1][r * frame->linesize[1] + c] == 128 &&
                  frame->data[2][r * frame->linesize[2] + c] == 128;
        }
      }
      if (previous_y >= 0) exact = exact && (y == previous_y + 1 || (previous_y == 219 && y == 20));
      previous_y = y;
      ++decoded;
    }
  };
  while (av_read_frame(input, packet) >= 0) {
    avcodec_send_packet(decoder, packet);
    av_packet_unref(packet);
    take();
  }
  avcodec_send_packet(decoder, nullptr);
  take();
  av_frame_free(&frame);
  av_packet_free(&packet);
  avcodec_free_context(&decoder);
  avformat_close_input(&input);

  EXPECT_EQ(decoded, 60);
  EXPECT_TRUE(exact);
  EXPECT_NE(ReadText(paths.sidecar).find("\"codec\": \"ffv1\""), std::string::npos);
}
#endif

// 검증을 통과하지 못한 선언은 시작하지 않고 카메라도 건드리지 않습니다.
TEST(Capture, RejectsInvalidSpec) {
  FakeRgbCamera camera({.initial = kInitial});
  auto spec = SmallSpec(SettingMode::kManual);
  spec.width = 63;
  const Paths paths = TempPaths("invalid");
  Capture capture(camera, spec);
  std::string error;

  EXPECT_FALSE(capture.Start(&error));
  EXPECT_TRUE(camera.ExecutedCommands().empty());
}

}  // namespace

namespace {

// fps 저하 문장은 첫 구간과 최저 fps를 쓰고, 구간이 더 있으면 수를 붙입니다. 느린 간격이 없으면 빈 문장입니다.
TEST(DescribeSlowFrames, WritesFirstSpanAndCount) {
  hwlab::timing::FrameRateCheck check;
  EXPECT_EQ(hwlab::platform::DescribeSlowFrames(check), "");
  check.slow_intervals = 52;
  check.min_fps = 25.04;
  check.spans = {{12'300'000, 14'100'000, 50}};
  EXPECT_EQ(hwlab::platform::DescribeSlowFrames(check), "fps 저하 12.3~14.1초, 최저 25.0fps");
  check.spans.push_back({20'000'000, 20'080'000, 2});
  EXPECT_EQ(hwlab::platform::DescribeSlowFrames(check), "fps 저하 12.3~14.1초, 최저 25.0fps 외 1곳");
}

}  // namespace
