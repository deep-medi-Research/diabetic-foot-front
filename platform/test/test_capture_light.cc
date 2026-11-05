#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "hwlab/platform/capture.h"
#include "hwlab/platform/fake_camera.h"
#include "hwlab/platform/fake_serial_port.h"
#include "hwlab/platform/light.h"

namespace {

using namespace std::chrono_literals;
using hwlab::CameraState;
using hwlab::platform::ReceivedFrame;
using hwlab::SettingMode;
using hwlab::flow::CaptureSpec;
using hwlab::flow::EndReason;
using hwlab::flow::Phase;
using hwlab::pixel::LightResponse;
using hwlab::platform::Capture;
using hwlab::platform::CaptureStatus;
using hwlab::platform::FakeCameraScript;
using hwlab::platform::FakeRgbCamera;
using hwlab::platform::FakeSerialLine;
using hwlab::platform::FakeSerialPort;
using hwlab::platform::Light;
using hwlab::platform::RecordingPaths;
using hwlab::record::LightSource;

using Bytes = std::vector<uint8_t>;

const CameraState kInitial{false, -5, 4, false, 4600, 60, true};

CaptureSpec SmallSpec(int64_t target_frames) {
  CaptureSpec spec;
  spec.device = "Fake";
  spec.width = 64;
  spec.height = 32;
  spec.settings = {SettingMode::kManual, -6, 8, 4600, 30};
  spec.settle = {4, 3, 3, 10};
  spec.target_frames = target_frames;
  spec.clip_level = 250;
  spec.buffer_frames = 64;
  spec.codec = hwlab::flow::VideoCodec::kUncompressed;
  return spec;
}

// 가짜 카메라 프레임의 밝기(Y)를 LED 보드에 마지막으로 쓴 밝기로 바꿉니다. Y = 20 + 밝기(%).
// follows가 false이면 LED와 관계없이 Y = 20입니다(명령이 영상에 나타나지 않는 경우).
class LitCamera final : public hwlab::platform::RgbCamera {
 public:
  LitCamera(FakeCameraScript script, std::shared_ptr<FakeSerialLine> line, bool follows)
      : inner_(std::move(script)), line_(std::move(line)), follows_(follows) {}

  std::string DeviceName() const override { return inner_.DeviceName(); }
  std::optional<ReceivedFrame> ReadFrame() override {
    auto frame = inner_.ReadFrame();
    if (!frame) return frame;
    const Bytes written = line_->Written();
    const int32_t percent = follows_ && !written.empty() ? written.back() : 0;
    const int32_t w = frame->view.width;
    const int32_t h = frame->view.height;
    pixels_.assign(static_cast<size_t>(w) * h * 2, 128);
    for (size_t i = 1; i < pixels_.size(); i += 2) pixels_[i] = static_cast<uint8_t>(20 + percent);
    frame->view.plane[0] = pixels_.data();
    frame->view.row_stride[0] = w * 2;
    return frame;
  }
  bool Reopen() override { return inner_.Reopen(); }
  CameraState ReadState() override { return inner_.ReadState(); }
  CameraState ReadStoredState() override { return inner_.ReadStoredState(); }
  bool Execute(const hwlab::flow::SettingCommand& command) override { return inner_.Execute(command); }
  void Interrupt() override { inner_.Interrupt(); }

 private:
  FakeRgbCamera inner_;
  std::shared_ptr<FakeSerialLine> line_;
  bool follows_;
  std::vector<uint8_t> pixels_;
};

struct Rig {
  explicit Rig(int64_t target_frames, bool follows = true, int32_t wall_period_us = 1000)
      : camera({.initial = kInitial, .wall_period_us = wall_period_us}, line, follows),
        capture(camera, SmallSpec(target_frames), nullptr, &light) {}

  std::shared_ptr<FakeSerialLine> line = std::make_shared<FakeSerialLine>();
  Light light{FakeSerialPort::Opener("COM7", line)};
  LitCamera camera;
  Capture capture;
};

// 조건이 참이 될 때까지 기다립니다. Windows 대기 시간 단위(약 15ms)를 고려해 넉넉히 둡니다.
bool WaitUntil(const std::function<bool(const CaptureStatus&)>& done, Capture& capture,
               std::chrono::milliseconds timeout = 5s) {
  const auto until = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < until) {
    if (done(capture.Status())) return true;
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

RecordingPaths TempRecording(const char* name) {
  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_capture_light";
  std::filesystem::create_directories(dir);
  RecordingPaths paths;
  paths.video = (dir / (std::string(name) + ".avi")).string();
  paths.sidecar = (dir / (std::string(name) + ".json")).string();
  return paths;
}

std::string ReadText(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

// 미리보기에서 연 포트에 녹화 중 밝기를 보내려 하면 거부하고 보드에 아무것도 쓰지 않으며, 녹화는 끝나지 않습니다.
TEST(CaptureLight, RejectsLightChangeWhileRecording) {
  Rig rig(100000);
  std::string error;
  ASSERT_TRUE(rig.capture.Start(&error)) << error;
  ASSERT_TRUE(rig.capture.OpenLight("COM7", 30));
  ASSERT_TRUE(WaitUntil([](const CaptureStatus& s) { return s.light.connected && s.light.pending == 0; }, rig.capture));
  rig.capture.StartRecording(TempRecording("reject"));
  ASSERT_TRUE(WaitUntil([](const CaptureStatus& s) { return s.phase == Phase::kRecording && s.written > 5; },
                        rig.capture));

  EXPECT_FALSE(rig.capture.SetLight(80));
  EXPECT_FALSE(rig.capture.OpenLight("COM7", 80));
  std::this_thread::sleep_for(100ms);
  const CaptureStatus status = rig.capture.Status();
  rig.capture.StopRecording();
  ASSERT_TRUE(rig.capture.WaitFinished(10s));
  const CaptureStatus end = rig.capture.Status();
  rig.capture.Stop();

  EXPECT_EQ(rig.line->Writes(), (std::vector<Bytes>{{0xFE, 30}}));
  EXPECT_EQ(status.phase, Phase::kRecording);
  EXPECT_EQ(status.light.pending, 0);
  EXPECT_EQ(end.end_reason, EndReason::kStopped);
}

// 조명이 없는 Capture는 밝기 요청을 받지 않습니다.
TEST(CaptureLight, RejectsWithoutLight) {
  FakeRgbCamera camera({.initial = kInitial});
  Capture capture(camera, SmallSpec(60));

  EXPECT_FALSE(capture.SetLight(50));
  EXPECT_FALSE(capture.OpenLight("COM7", 50));
  EXPECT_FALSE(capture.Status().light.attached);
}

// 미리보기 중 보낸 LED 밝기는 설정 변경 사건이 되어 안정화 프레임 수(블랙 레벨 10프레임)를 다시 셉니다.
TEST(CaptureLight, LightChangeRestartsSettle) {
  // 10프레임이 100ms가 되게 해 상태를 읽는 간격(Windows 약 15ms)보다 길게 둡니다.
  Rig rig(60, true, 10000);
  std::string error;
  ASSERT_TRUE(rig.capture.Start(&error)) << error;
  ASSERT_TRUE(rig.capture.OpenLight("COM7", 30));
  // 시작할 때의 설정 변경과 포트를 열며 보낸 밝기의 안정화가 끝나기를 기다립니다.
  ASSERT_TRUE(WaitUntil(
      [](const CaptureStatus& s) { return s.light.pending == 0 && s.received > 40 && s.frames_until_settled == 0; },
      rig.capture));

  ASSERT_TRUE(rig.capture.SetLight(60));
  const bool restarted = WaitUntil([](const CaptureStatus& s) { return s.frames_until_settled > 0; }, rig.capture, 2s);
  rig.capture.Stop();

  EXPECT_TRUE(restarted);
  EXPECT_EQ(rig.line->Writes(), (std::vector<Bytes>{{0xFE, 30}, {0xFE, 60}}));
}

// 판정 기준이 있으면 LED 변경 전후의 영상 밝기로 반영을 판정하고, 녹화의 사이드카 light에 출처, 밝기, 포트, 판정, 깜빡임 요약을 남깁니다.
TEST(CaptureLight, SidecarRecordsLightAndResponse) {
  Rig rig(60);
  rig.capture.SetLightResponseRule({.applied_frame = 2, .window_frames = 3, .min_mean_change = 5.0});
  std::string error;
  ASSERT_TRUE(rig.capture.Start(&error)) << error;
  ASSERT_TRUE(rig.capture.OpenLight("COM7", 30));
  ASSERT_TRUE(WaitUntil([](const CaptureStatus& s) { return s.light.pending == 0 && s.received > 20; }, rig.capture));
  const int64_t before_change = rig.capture.Status().received;
  ASSERT_TRUE(WaitUntil([&](const CaptureStatus& s) { return s.received > before_change + 5; }, rig.capture));

  ASSERT_TRUE(rig.capture.SetLight(80));
  ASSERT_TRUE(WaitUntil([](const CaptureStatus& s) { return s.light.pending == 0 && !s.light.judging; }, rig.capture));
  const CaptureStatus judged = rig.capture.Status();
  RecordingPaths paths = TempRecording("sidecar_light");
  paths.light.source = LightSource::kApp;
  paths.light.usb_vendor_id = 0x0403;
  paths.light.usb_product_id = 0x6001;
  paths.light.serial = "FAKE0001A";
  paths.light.confirmed = true;
  rig.capture.StartRecording(paths);
  ASSERT_TRUE(rig.capture.WaitFinished(10s));
  rig.capture.Stop();

  EXPECT_EQ(judged.light.response, LightResponse::kApplied);
  EXPECT_EQ(judged.light.percent, 80);
  EXPECT_EQ(judged.light.port, "COM7");
  const std::string json = ReadText(paths.sidecar);
  // 녹화 중 LED는 80%로 고정이라 Y = 100이고 프레임마다 같습니다.
  const std::string expected = R"(  "light": {
    "source": "app",
    "percent": 80,
    "port_vid_pid": "0403:6001",
    "serial": "FAKE0001A",
    "confirmed": true,
    "response": "applied",
    "flicker": {
      "frames": 60,
      "mean": 100.0000,
      "stddev": 0.0000,
      "peak_to_peak": 0.0000,
      "periodic_fraction": 0.0000,
      "dominant_period_frames": null
    }
  },)";
  EXPECT_NE(json.find(expected), std::string::npos) << json;
}

// LED 명령이 영상 밝기에 나타나지 않으면 not_seen입니다.
TEST(CaptureLight, NotSeenWhenFrameIgnoresLight) {
  Rig rig(60, false);
  rig.capture.SetLightResponseRule({.applied_frame = 2, .window_frames = 3, .min_mean_change = 5.0});
  std::string error;
  ASSERT_TRUE(rig.capture.Start(&error)) << error;
  ASSERT_TRUE(rig.capture.OpenLight("COM7", 30));
  ASSERT_TRUE(WaitUntil([](const CaptureStatus& s) { return s.light.pending == 0 && s.received > 20; }, rig.capture));
  const int64_t before_change = rig.capture.Status().received;
  ASSERT_TRUE(WaitUntil([&](const CaptureStatus& s) { return s.received > before_change + 5; }, rig.capture));

  ASSERT_TRUE(rig.capture.SetLight(80));
  ASSERT_TRUE(WaitUntil([](const CaptureStatus& s) { return s.light.pending == 0 && !s.light.judging; }, rig.capture));
  const CaptureStatus judged = rig.capture.Status();
  rig.capture.Stop();

  EXPECT_EQ(judged.light.response, LightResponse::kNotSeen);
}

// 앱 밖에서 켠 조명은 Capture에 조명이 있어도 밝기와 판정을 남기지 않고, 깜빡임 요약만 남깁니다.
TEST(CaptureLight, ExternalLightKeepsPercentNull) {
  Rig rig(60);
  std::string error;
  ASSERT_TRUE(rig.capture.Start(&error)) << error;
  ASSERT_TRUE(rig.capture.OpenLight("COM7", 30));
  ASSERT_TRUE(WaitUntil([](const CaptureStatus& s) { return s.light.pending == 0 && s.received > 20; }, rig.capture));
  RecordingPaths paths = TempRecording("sidecar_external");
  paths.light.source = LightSource::kExternal;
  rig.capture.StartRecording(paths);
  ASSERT_TRUE(rig.capture.WaitFinished(10s));
  rig.capture.Stop();

  const std::string json = ReadText(paths.sidecar);
  EXPECT_NE(json.find("\"source\": \"external\",\n    \"percent\": null,"), std::string::npos) << json;
  EXPECT_NE(json.find("\"response\": \"unknown\",\n    \"flicker\": {\n      \"frames\": 60,"), std::string::npos)
      << json;
}

// 포트를 열지 못하면 사건을 만들지 않고 오류를 남깁니다.
TEST(CaptureLight, OpenFailureLeavesErrorAndNoEvent) {
  Rig rig(60);
  std::string error;
  ASSERT_TRUE(rig.capture.Start(&error)) << error;
  ASSERT_TRUE(WaitUntil([](const CaptureStatus& s) { return s.received > 40 && s.frames_until_settled == 0; },
                        rig.capture));

  ASSERT_TRUE(rig.capture.OpenLight("COM3", 30));
  ASSERT_TRUE(WaitUntil([](const CaptureStatus& s) { return s.light.pending == 0; }, rig.capture));
  std::this_thread::sleep_for(50ms);
  const CaptureStatus status = rig.capture.Status();
  rig.capture.Stop();

  EXPECT_FALSE(status.light.connected);
  EXPECT_FALSE(status.light.error.empty());
  EXPECT_EQ(status.frames_until_settled, 0);
  EXPECT_TRUE(rig.line->Writes().empty());
}

}  // namespace
