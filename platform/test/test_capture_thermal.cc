#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "hwlab/platform/capture.h"
#include "hwlab/platform/fake_camera.h"
#include "hwlab/platform/fake_thermal_camera.h"

namespace {

using namespace std::chrono_literals;
using hwlab::CameraState;
using hwlab::SettingMode;
using hwlab::flow::CaptureSpec;
using hwlab::flow::EndReason;
using hwlab::platform::CameraTemperature;
using hwlab::platform::Capture;
using hwlab::platform::CaptureStatus;
using hwlab::platform::FakeCameraScript;
using hwlab::platform::FakeRgbCamera;
using hwlab::platform::FakeThermalCamera;
using hwlab::platform::FakeThermalScript;
using hwlab::platform::RecordingPaths;
using hwlab::platform::ThermalConnection;

constexpr int64_t kPeriodUs = 33'333;
constexpr int32_t kThermalWidth = 64;
constexpr int32_t kThermalHeight = 48;

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

// 두 가짜 장치가 같은 PC 시계를 쓰고 실제 30fps로 프레임을 냅니다.
FakeCameraScript RgbScript() {
  FakeCameraScript script{.initial = kInitial};
  script.pc_clock = true;
  script.wall_period_us = kPeriodUs;
  return script;
}

// 열화상 장치 주기 시나리오입니다. 실험 장치(TE-EV1)는 30Hz, 제품 장치는 9Hz이고 평균 7.5Hz까지 내려갑니다.
struct ThermalRate {
  int64_t period_us;
  int64_t jitter_us;  // 수신 지연 변동
};
constexpr ThermalRate kThirtyHz{33'333, 0};
constexpr ThermalRate kNineHz{111'111, 15'000};
constexpr ThermalRate kSevenAndHalfHz{133'333, 15'000};
constexpr int64_t kNineHzRecordUs = 111'111;  // 선언 파일의 thermal_record_period_us

FakeThermalScript ThermalScript(ThermalRate rate = kThirtyHz) {
  FakeThermalScript script;
  script.width = kThermalWidth;
  script.height = kThermalHeight;
  script.device_period_us = rate.period_us;
  script.jitter_us = rate.jitter_us;
  script.pc_clock = true;
  script.loss_rule = {2'000'000, 200'000};
  return script;
}

// 원시값 파일 옆의 카메라 온도 파일입니다. X_thermal.raw16이면 X_thermal_camera_temp.csv입니다.
std::string CameraTemperaturePath(const RecordingPaths& paths) {
  const std::string suffix = ".raw16";
  return paths.thermal_raw.substr(0, paths.thermal_raw.size() - suffix.size()) + "_camera_temp.csv";
}

RecordingPaths TempPaths(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_capture_thermal";
  std::filesystem::create_directories(dir);
  RecordingPaths paths{(dir / (name + ".avi")).string(), (dir / (name + ".json")).string(),
                       (dir / (name + "_thermal.raw16")).string(), (dir / (name + "_thermal.json")).string(),
                       (dir / (name + "_thermal.avi")).string(), {}, {}};
  for (const std::string& p : {paths.video, paths.sidecar, paths.thermal_raw, paths.thermal_sidecar, paths.thermal_preview,
                               CameraTemperaturePath(paths)}) {
    std::filesystem::remove(p);
  }
  return paths;
}

std::string ReadText(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

std::vector<uint8_t> ReadBytes(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// 사이드카의 "key": [1, 2, 3] 배열을 읽습니다.
std::vector<int64_t> IntArray(const std::string& json, const std::string& key) {
  std::vector<int64_t> values;
  const size_t at = json.find("\"" + key + "\": [");
  if (at == std::string::npos) return values;
  const size_t end = json.find(']', at);
  std::stringstream items(json.substr(json.find('[', at) + 1, end - json.find('[', at) - 1));
  std::string item;
  while (std::getline(items, item, ',')) {
    if (item.find_first_not_of(' ') != std::string::npos) values.push_back(std::stoll(item));
  }
  return values;
}

// 사이드카에서 처음 나오는 "key": 숫자를 읽습니다.
int64_t IntField(const std::string& json, const std::string& key, size_t from = 0) {
  const std::string pattern = "\"" + key + "\": ";
  const size_t at = json.find(pattern, from);
  if (at == std::string::npos) return -1;
  return std::stoll(json.substr(at + pattern.size()));
}

double DoubleField(const std::string& json, const std::string& key) {
  const std::string pattern = "\"" + key + "\": ";
  const size_t at = json.find(pattern);
  if (at == std::string::npos) return 0;
  return std::stod(json.substr(at + pattern.size()));
}

// 미리보기 몇 장 뒤 촬영 버튼을 누르고 RGB와 열화상 사이드카를 모두 쓸 때까지 기다립니다.
CaptureStatus Record(Capture& capture, int preview_frames, const RecordingPaths& paths) {
  while (capture.Status().received < preview_frames) std::this_thread::sleep_for(1ms);
  capture.StartRecording(paths);
  EXPECT_TRUE(capture.WaitFinished(20s));
  return capture.Status();
}

// 자동 교정 멈춤과 되돌림 호출 수를 세는 열화상입니다. 문을 닫아 두면 셔터 교정을 시험이 열 때까지 끝내지 않습니다.
class CountingThermalCamera final : public hwlab::platform::ThermalCamera {
 public:
  CountingThermalCamera(FakeThermalCamera& inner, bool gate_closed) : inner_(inner), gate_closed_(gate_closed) {}

  bool Open(std::string* error) override { return inner_.Open(error); }
  void Close() override { inner_.Close(); }
  std::string DeviceName() const override { return inner_.DeviceName(); }
  int32_t Width() const override { return inner_.Width(); }
  int32_t Height() const override { return inner_.Height(); }
  std::optional<hwlab::platform::ReceivedFrame> ReadFrame(int64_t timeout_us) override {
    return inner_.ReadFrame(timeout_us);
  }
  ThermalConnection Connection() const override { return inner_.Connection(); }
  bool CalibrateNow(std::string* error) override {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      calibrating_ = true;
      changed_.notify_all();
      changed_.wait(lock, [&] { return !gate_closed_; });
    }
    return inner_.CalibrateNow(error);
  }
  bool HoldAutoCalibration(bool hold, std::string* error) override {
    const bool ok = inner_.HoldAutoCalibration(hold, error);
    std::lock_guard<std::mutex> lock(mutex_);
    ++(hold ? holds_ : releases_);
    changed_.notify_all();
    return ok;
  }

  // 셔터 교정에 들어올 때까지 기다립니다.
  bool WaitCalibrating() {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, 5s, [&] { return calibrating_; });
  }
  // 멈춤과 되돌림이 각각 holds번 불릴 때까지 기다립니다.
  bool WaitBalanced(int holds) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, 5s, [&] { return holds_ == holds && releases_ == holds; });
  }
  void OpenGate() {
    std::lock_guard<std::mutex> lock(mutex_);
    gate_closed_ = false;
    changed_.notify_all();
  }
  int Holds() {
    std::lock_guard<std::mutex> lock(mutex_);
    return holds_;
  }
  int Releases() {
    std::lock_guard<std::mutex> lock(mutex_);
    return releases_;
  }

 private:
  FakeThermalCamera& inner_;
  std::mutex mutex_;
  std::condition_variable changed_;
  bool gate_closed_;
  bool calibrating_ = false;
  int holds_ = 0;
  int releases_ = 0;
};

// 상태가 조건을 만족할 때까지 RGB 수신 주기로 다시 읽습니다. 5초 안에 만족하지 않으면 false입니다.
template <typename Condition>
bool WaitStatus(const Capture& capture, Condition condition) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!condition(capture.Status())) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    std::this_thread::sleep_for(1ms);
  }
  return true;
}

class CaptureThermalRate : public ::testing::TestWithParam<ThermalRate> {};

// 열화상 프레임의 공통 시각은 모두 RGB 녹화의 첫 프레임과 마지막 프레임 공통 시각에서 한 주기 안에 있습니다.
// 기록 주기 9Hz에서 30Hz 장치는 셋 중 하나를 기록하고, 9Hz와 7.5Hz 장치는 도착 간격이 변해도 받은 프레임을 모두 기록합니다.
TEST_P(CaptureThermalRate, ThermalCommonTimesStayInsideRgbRecording) {
  const ThermalRate rate = GetParam();
  FakeRgbCamera camera(RgbScript());
  FakeThermalCamera thermal(ThermalScript(rate));
  const RecordingPaths paths = TempPaths("window_" + std::to_string(rate.period_us));
  CaptureSpec spec = SmallSpec(60);
  spec.thermal_record_period_us = kNineHzRecordUs;
  Capture capture(camera, spec, &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;

  const CaptureStatus status = Record(capture, 10, paths);
  capture.Stop();

  EXPECT_EQ(status.end_reason, EndReason::kCompleted);
  EXPECT_EQ(status.written, 60);
  EXPECT_TRUE(status.thermal.attached);
  EXPECT_TRUE(status.thermal.error.empty()) << status.thermal.error;
  const double device_hz = 1e6 / static_cast<double>(rate.period_us);
  EXPECT_NEAR(status.thermal.measured_fps, device_hz, device_hz * 0.1);
  const std::string rgb = ReadText(paths.sidecar);
  const std::string th = ReadText(paths.thermal_sidecar);
  const std::vector<int64_t> rgb_times = IntArray(rgb, "common_time_us");
  const std::vector<int64_t> thermal_times = IntArray(th, "common_time_us");
  ASSERT_EQ(rgb_times.size(), 60u);
  ASSERT_EQ(static_cast<int64_t>(thermal_times.size()), status.thermal.written);
  ASSERT_FALSE(thermal_times.empty());
  const int64_t tolerance_us = std::max(rate.period_us, kPeriodUs);
  for (const int64_t t : thermal_times) {
    EXPECT_GE(t, rgb_times.front() - tolerance_us);
    EXPECT_LE(t, rgb_times.back() + tolerance_us);
  }
  const int64_t received = IntField(th, "received");
  const int64_t written = IntField(th, "written");
  EXPECT_EQ(IntField(th, "record_period_us"), kNineHzRecordUs);
  if (rate.period_us * 100 >= kNineHzRecordUs * 80) {
    EXPECT_EQ(written, received) << th;
  } else {
    // 2초 동안 30Hz로 약 60장을 받고 9Hz로 약 20장을 기록합니다.
    EXPECT_NEAR(static_cast<double>(written), static_cast<double>(received) / 3.0, 2.0) << th;
  }
  const int64_t in_window = IntField(th, "written_in_rgb_window");
  EXPECT_LE(in_window, written);
  EXPECT_GE(in_window, written - 2);
  EXPECT_EQ(IntField(rgb, "written_in_rgb_window"), 60);
  EXPECT_NE(th.find("\"stream\": \"thermal\""), std::string::npos);
  EXPECT_NE(th.find("\"codec\": \"raw16\""), std::string::npos);
  EXPECT_EQ(IntField(th, "missing"), 0) << th;
  EXPECT_TRUE(std::filesystem::exists(paths.thermal_preview));
}

// 원시값 파일의 바이트는 가짜 열화상이 보낸 화소 값과 같고, 사이드카의 표시 범위는 첫 프레임의 최솟값과 최댓값입니다.
TEST_P(CaptureThermalRate, WritesRawBytesAndDisplayRange) {
  const ThermalRate rate = GetParam();
  FakeRgbCamera camera(RgbScript());
  FakeThermalCamera thermal(ThermalScript(rate));
  const RecordingPaths paths = TempPaths("raw_" + std::to_string(rate.period_us));
  CaptureSpec spec = SmallSpec(30);
  spec.thermal_record_period_us = kNineHzRecordUs;
  Capture capture(camera, spec, &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;

  const CaptureStatus status = Record(capture, 10, paths);
  capture.Stop();

  const std::string th = ReadText(paths.thermal_sidecar);
  const std::vector<int64_t> index = IntArray(th, "index");
  const std::vector<uint8_t> raw = ReadBytes(paths.thermal_raw);
  const size_t frame_bytes = static_cast<size_t>(kThermalWidth) * kThermalHeight * 2;
  ASSERT_GT(index.size(), 0u);
  ASSERT_EQ(static_cast<int64_t>(index.size()), status.thermal.written);
  ASSERT_EQ(raw.size(), index.size() * frame_bytes);
  EXPECT_EQ(IntField(th, "file_bytes"), static_cast<int64_t>(raw.size()));
  // 끊김과 누락이 없으면 장치 프레임 번호는 받은 순서와 같습니다.
  size_t mismatched = 0;
  for (size_t k = 0; k < index.size(); ++k) {
    for (int32_t y = 0; y < kThermalHeight; ++y) {
      for (int32_t x = 0; x < kThermalWidth; ++x) {
        const size_t at = k * frame_bytes + (static_cast<size_t>(y) * kThermalWidth + x) * 2;
        const uint16_t value = static_cast<uint16_t>(raw[at] | (raw[at + 1] << 8));
        mismatched += value != FakeThermalCamera::PixelValue(index[k], x, y) ? 1 : 0;
      }
    }
  }
  EXPECT_EQ(mismatched, 0u);

  uint16_t low = 0xFFFF, high = 0;
  for (int32_t y = 0; y < kThermalHeight; ++y) {
    for (int32_t x = 0; x < kThermalWidth; ++x) {
      low = std::min(low, FakeThermalCamera::PixelValue(index[0], x, y));
      high = std::max(high, FakeThermalCamera::PixelValue(index[0], x, y));
    }
  }
  const size_t range_at = th.find("\"display_range\": {");
  ASSERT_NE(range_at, std::string::npos) << th;
  EXPECT_EQ(IntField(th, "low", range_at), low);
  EXPECT_EQ(IntField(th, "high", range_at), high);
  EXPECT_NE(ReadText(paths.sidecar).find("\"display_range\": null"), std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(DeviceRates, CaptureThermalRate, ::testing::Values(kThirtyHz, kNineHz, kSevenAndHalfHz),
                         [](const ::testing::TestParamInfo<ThermalRate>& info) {
                           return "Period" + std::to_string(info.param.period_us) + "us";
                         });

// 30Hz 장치를 기록 주기 9Hz로 3초 녹화하면 약 27장을 평균 간격 111ms로 기록합니다. 미리보기는 거르지 않은 30Hz입니다.
TEST(CaptureThermal, ThirtyHzDeviceRecordsAtNineHz) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalCamera thermal(ThermalScript(kThirtyHz));
  const RecordingPaths paths = TempPaths("nine_hz");
  CaptureSpec spec = SmallSpec(90);
  spec.thermal_record_period_us = kNineHzRecordUs;
  Capture capture(camera, spec, &thermal);
  std::atomic<int> previews{0};
  capture.SetThermalPreviewSink(1, [&](const hwlab::FrameView&) { ++previews; });
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;

  const CaptureStatus status = Record(capture, 10, paths);
  const int64_t received_total = status.thermal.received;
  capture.Stop();

  const std::string th = ReadText(paths.thermal_sidecar);
  const std::vector<int64_t> host = IntArray(th, "host_time_us");
  ASSERT_GE(host.size(), 2u);
  EXPECT_GE(host.size(), 25u);
  EXPECT_LE(host.size(), 29u);
  const double mean_interval_us =
      static_cast<double>(host.back() - host.front()) / static_cast<double>(host.size() - 1);
  EXPECT_NEAR(mean_interval_us, static_cast<double>(kNineHzRecordUs), 5'000.0);
  EXPECT_GE(IntField(th, "received"), 85);
  EXPECT_GE(previews.load(), received_total - 1);
}

// RGB 장치 시각에 오프셋과 속도 차이(400ppm)가 있고 수신 지연이 프레임마다 0.5~0.8ms 사이에서 달라도, 사이드카의 시계 식으로 옮긴
// 공통 시각은 촬영 시각과 1ms 안입니다.
TEST(CaptureThermal, CommonTimeWithinOneMillisecondForOffsetAndRate) {
  constexpr int64_t kOffsetUs = -987'654'321;
  constexpr double kRate = 1.0004;
  FakeCameraScript script = RgbScript();
  script.device_offset_us = kOffsetUs;
  script.device_rate = kRate;
  script.host_latency_us = 500;
  script.host_jitter_us = 300;
  FakeRgbCamera camera(script);
  FakeThermalCamera thermal(ThermalScript());
  const RecordingPaths paths = TempPaths("clock");
  Capture capture(camera, SmallSpec(60), &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;

  const CaptureStatus status = Record(capture, 10, paths);
  capture.Stop();

  ASSERT_EQ(status.written, 60);
  const std::string rgb = ReadText(paths.sidecar);
  const std::vector<int64_t> device = IntArray(rgb, "device_time_us");
  const std::vector<int64_t> host = IntArray(rgb, "host_time_us");
  const std::vector<int64_t> common = IntArray(rgb, "common_time_us");
  ASSERT_EQ(device.size(), 60u);
  ASSERT_EQ(common.size(), 60u);
  int64_t worst_us = 0;
  for (size_t i = 0; i < device.size(); ++i) {
    // 가짜 카메라는 장치 시각 = 오프셋 + 속도 * 촬영 시각(PC 시계)으로 매깁니다.
    const double capture_us = (static_cast<double>(device[i]) - static_cast<double>(kOffsetUs)) / kRate;
    worst_us = std::max(worst_us, static_cast<int64_t>(std::llabs(common[i] - std::llround(capture_us))));
    EXPECT_LE(common[i], host[i]);
  }
  EXPECT_LE(worst_us, 1000);
  EXPECT_NEAR(DoubleField(rgb, "rate"), 1.0 / kRate, 50e-6);

  const std::vector<int64_t> thermal_times = IntArray(ReadText(paths.thermal_sidecar), "common_time_us");
  ASSERT_FALSE(thermal_times.empty());
  for (const int64_t t : thermal_times) {
    EXPECT_GE(t, common.front() - kPeriodUs);
    EXPECT_LE(t, common.back() + kPeriodUs);
  }
}

// 녹화 중 열화상이 끊겼다가 다시 연결되어도 RGB는 목표 프레임 수를 채우고, 끊긴 구간은 열화상 누락으로 남습니다.
TEST(CaptureThermal, ThermalLossKeepsRgbRecordingAndRecordsGap) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalScript thermal_script = ThermalScript();
  thermal_script.disconnect_after = 40;  // 시작 뒤 약 1.3초. 녹화는 약 0.7초부터 3.2초까지입니다
  thermal_script.reopen_failures = 0;
  FakeThermalCamera thermal(thermal_script);
  const RecordingPaths paths = TempPaths("thermal_loss");
  Capture capture(camera, SmallSpec(75), &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;

  const CaptureStatus status = Record(capture, 10, paths);
  capture.Stop();

  EXPECT_EQ(status.end_reason, EndReason::kCompleted);
  EXPECT_EQ(status.written, 75);
  EXPECT_TRUE(status.error.empty()) << status.error;
  EXPECT_GE(status.thermal.reconnects, 1);
  const std::string th = ReadText(paths.thermal_sidecar);
  const int64_t missing = IntField(th, "missing");
  EXPECT_GE(missing, 3) << th;
  EXPECT_EQ(missing, status.thermal.missing);
  EXPECT_NE(th.find("\"after_frame\""), std::string::npos);
}

// 렌즈 셰이딩 보정 파일 폴더를 비워 만듭니다. saved_s를 주면 가짜 열화상 카메라의 보정 파일을 그 시각으로 둡니다.
std::string LensShadingFolder(const std::string& name, std::optional<int64_t> saved_s) {
  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_capture_lens_shading" / name;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  if (saved_s) std::ofstream(dir / "lens_shading_FakeThermal.cal") << *saved_s << "\n";
  return dir.string();
}

// 보정 파일을 불러온 열화상이 녹화 중에 끊겼다가 다시 열리면 다시 연 연결도 그 파일을 불러와 사이드카에 loaded를 남깁니다.
TEST(CaptureThermal, ReconnectedThermalReloadsLensShading) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalScript thermal_script = ThermalScript();
  thermal_script.disconnect_after = 40;
  thermal_script.reopen_failures = 0;
  FakeThermalCamera thermal(thermal_script);
  thermal.SetLensShadingFolder(LensShadingFolder("reconnect", 1761205212));
  const RecordingPaths paths = TempPaths("thermal_loss_lens_shading");
  Capture capture(camera, SmallSpec(75), &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;

  const CaptureStatus status = Record(capture, 10, paths);
  capture.Stop();

  ASSERT_GE(status.thermal.reconnects, 1);
  ASSERT_TRUE(status.thermal.lens_shading);
  EXPECT_EQ(status.thermal.lens_shading->record.source, hwlab::record::LensShadingSource::kLoaded);
  const std::string th = ReadText(paths.thermal_sidecar);
  EXPECT_NE(th.find("\"source\": \"loaded\""), std::string::npos) << th;
  EXPECT_NE(th.find("\"file_unix_s\": 1761205212"), std::string::npos) << th;
}

// 미리보기에서 받은 보정 요청은 제어 스레드가 보정하고 파일로 저장하며, 그 뒤 녹화의 사이드카에 corrected를 남깁니다.
// 미리보기가 아닐 때(녹화 준비와 녹화 중)는 요청을 받지 않습니다.
TEST(CaptureThermal, CorrectsLensShadingOnlyInPreview) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalCamera thermal(ThermalScript());
  const std::string folder = LensShadingFolder("correct", std::nullopt);
  thermal.SetLensShadingFolder(folder);
  const RecordingPaths paths = TempPaths("lens_shading_correct");
  Capture capture(camera, SmallSpec(20), &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  ASSERT_TRUE(WaitStatus(capture, [](const CaptureStatus& s) { return s.thermal.received > 0; }));
  ASSERT_TRUE(capture.Status().thermal.lens_shading);
  EXPECT_EQ(capture.Status().thermal.lens_shading->record.source, hwlab::record::LensShadingSource::kNone);

  ASSERT_TRUE(capture.CorrectThermalLensShading());
  ASSERT_TRUE(WaitStatus(capture, [](const CaptureStatus& s) { return !s.thermal.lens_shading_busy; }));
  const CaptureStatus corrected = capture.Status();
  ASSERT_TRUE(corrected.thermal.lens_shading);
  EXPECT_EQ(corrected.thermal.lens_shading->record.source, hwlab::record::LensShadingSource::kCorrected);
  EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(folder) / "lens_shading_FakeThermal.cal"));

  capture.StartRecording(paths);
  ASSERT_TRUE(WaitStatus(capture, [](const CaptureStatus& s) {
    return s.phase != hwlab::flow::Phase::kPreview || s.finished;
  }));
  EXPECT_FALSE(capture.CorrectThermalLensShading());
  EXPECT_TRUE(capture.WaitFinished(20s));
  capture.Stop();
  EXPECT_NE(ReadText(paths.thermal_sidecar).find("\"source\": \"corrected\""), std::string::npos);
}

TEST(CaptureThermal, LensShadingKeepsCameraTemperatureAtCorrection) {
  using hwlab::timing::WarmupPhase;
  FakeRgbCamera camera(RgbScript());
  FakeThermalScript thermal_script = ThermalScript();
  // 가상 시계로 기다리지 않고 프레임을 내어, 데워진 카메라가 예열 완료(구간 10분과 유지 15분)에 닿는 시간을 몇 초로 줄입니다.
  thermal_script.pc_clock = false;
  thermal_script.wall_period_us = 0;
  thermal_script.fpa_tau_us = 15 * 60'000'000LL;
  thermal_script.warmed_before_open_us = 120 * 60'000'000LL;
  FakeThermalCamera thermal(thermal_script);
  const std::string folder = LensShadingFolder("conditions", std::nullopt);
  thermal.SetLensShadingFolder(folder);
  Capture capture(camera, SmallSpec(20), &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  ASSERT_TRUE(WaitStatus(capture, [](const CaptureStatus& s) { return s.thermal.camera_temperature.has_value(); }));
  EXPECT_EQ(capture.Status().thermal.warmup.phase, WarmupPhase::kMeasuring);
  const auto deadline = std::chrono::steady_clock::now() + 60s;
  while (capture.Status().thermal.warmup.phase != WarmupPhase::kReady && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(10ms);
  }
  const CaptureStatus ready = capture.Status();
  ASSERT_EQ(ready.thermal.warmup.phase, WarmupPhase::kReady);

  ASSERT_TRUE(capture.CorrectThermalLensShading());
  ASSERT_TRUE(WaitStatus(capture, [](const CaptureStatus& s) { return !s.thermal.lens_shading_busy; }));
  const CaptureStatus corrected = capture.Status();
  capture.Stop();
  ASSERT_TRUE(corrected.thermal.lens_shading && corrected.thermal.lens_shading->made);
  const hwlab::timing::LensShadingConditions made = *corrected.thermal.lens_shading->made;
  EXPECT_EQ(made.warmup, WarmupPhase::kReady);
  EXPECT_NEAR(made.fpa, ready.thermal.camera_temperature->fpa, 1e-3);
  EXPECT_NEAR(made.shutter_c, ready.thermal.camera_temperature->shutter_c, 1e-3);

  FakeThermalCamera reopened(thermal_script);
  reopened.SetLensShadingFolder(folder);
  ASSERT_TRUE(reopened.Open(&error));
  ASSERT_TRUE(reopened.LensShading());
  EXPECT_EQ(reopened.LensShading()->record.source, hwlab::record::LensShadingSource::kLoaded);
  ASSERT_TRUE(reopened.LensShading()->made);
  EXPECT_NEAR(reopened.LensShading()->made->fpa, made.fpa, 1e-3);
  EXPECT_EQ(reopened.LensShading()->made->warmup, WarmupPhase::kReady);
}

// 열화상이 끊긴 동안은 보정 요청을 받지 않습니다. 다시 여는 몇 초 뒤에 보정하면 그 사이 렌즈 앞을 치웠을 수 있습니다.
TEST(CaptureThermal, CorrectsLensShadingOnlyWhileReceiving) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalScript thermal_script = ThermalScript();
  thermal_script.disconnect_after = 10;
  FakeThermalCamera thermal(thermal_script);
  const std::string folder = LensShadingFolder("not_receiving", std::nullopt);
  thermal.SetLensShadingFolder(folder);
  Capture capture(camera, SmallSpec(20), &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  ASSERT_TRUE(WaitStatus(capture, [](const CaptureStatus& s) {
    return s.thermal.received > 0 && s.thermal.connection != ThermalConnection::kReceiving;
  }));

  EXPECT_FALSE(capture.CorrectThermalLensShading());
  EXPECT_FALSE(capture.Status().thermal.lens_shading_busy);
  capture.Stop();
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(folder) / "lens_shading_FakeThermal.cal"));
}

// 제어 스레드가 실행하기 전에 멈추면 보정 중 표시를 남기지 않습니다.
TEST(CaptureThermal, StopClearsPendingLensShadingRequest) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalCamera thermal(ThermalScript());
  thermal.SetLensShadingFolder(LensShadingFolder("stop_pending", std::nullopt));
  Capture capture(camera, SmallSpec(20), &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  ASSERT_TRUE(WaitStatus(capture, [](const CaptureStatus& s) { return s.thermal.received > 0; }));

  ASSERT_TRUE(capture.CorrectThermalLensShading());
  capture.Stop();
  EXPECT_FALSE(capture.Status().thermal.lens_shading_busy);
}

// 렌즈 셰이딩 보정에 들어가면 시험이 문을 열 때까지 끝내지 않는 열화상입니다.
class GatedLensShadingCamera final : public hwlab::platform::ThermalCamera {
 public:
  explicit GatedLensShadingCamera(FakeThermalCamera& inner) : inner_(inner) {}

  bool Open(std::string* error) override { return inner_.Open(error); }
  void Close() override { inner_.Close(); }
  std::string DeviceName() const override { return inner_.DeviceName(); }
  int32_t Width() const override { return inner_.Width(); }
  int32_t Height() const override { return inner_.Height(); }
  std::optional<hwlab::platform::ReceivedFrame> ReadFrame(int64_t timeout_us) override {
    return inner_.ReadFrame(timeout_us);
  }
  ThermalConnection Connection() const override { return inner_.Connection(); }
  bool CalibrateNow(std::string* error) override { return inner_.CalibrateNow(error); }
  bool HoldAutoCalibration(bool hold, std::string* error) override { return inner_.HoldAutoCalibration(hold, error); }
  void SetLensShadingFolder(const std::string& folder) override { inner_.SetLensShadingFolder(folder); }
  std::optional<hwlab::platform::LensShadingStatus> LensShading() const override { return inner_.LensShading(); }
  bool CorrectLensShading(const std::optional<hwlab::timing::LensShadingConditions>& made, std::string* error) override {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      correcting_ = true;
      changed_.notify_all();
      changed_.wait(lock, [&] { return gate_open_; });
    }
    return inner_.CorrectLensShading(made, error);
  }

  bool WaitCorrecting() {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, 5s, [&] { return correcting_; });
  }
  void OpenGate() {
    std::lock_guard<std::mutex> lock(mutex_);
    gate_open_ = true;
    changed_.notify_all();
  }

 private:
  FakeThermalCamera& inner_;
  std::mutex mutex_;
  std::condition_variable changed_;
  bool correcting_ = false;
  bool gate_open_ = false;
};

// 보정하는 동안에는 두 번째 요청을 받지 않습니다. 보정 중에 촬영 버튼을 누르면 잠금과 열화상 준비가 같은 제어 스레드에서
// 보정 뒤로 밀려, 녹화는 보정이 끝난 뒤에 시작하고 사이드카에 corrected가 남습니다.
TEST(CaptureThermal, RecordingWaitsForRunningLensShadingCorrection) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalCamera inner(ThermalScript());
  GatedLensShadingCamera thermal(inner);
  thermal.SetLensShadingFolder(LensShadingFolder("gated", std::nullopt));
  const RecordingPaths paths = TempPaths("lens_shading_gated");
  Capture capture(camera, SmallSpec(20), &thermal);
  // 중간에 실패해도 Capture를 거둘 때 제어 스레드가 문에서 멈춰 있지 않도록 먼저 엽니다(capture보다 먼저 소멸).
  struct OpenOnExit {
    GatedLensShadingCamera& camera;
    ~OpenOnExit() { camera.OpenGate(); }
  } open_on_exit{thermal};
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  ASSERT_TRUE(WaitStatus(capture, [](const CaptureStatus& s) { return s.thermal.received > 0; }));

  ASSERT_TRUE(capture.CorrectThermalLensShading());
  ASSERT_TRUE(thermal.WaitCorrecting());
  EXPECT_FALSE(capture.CorrectThermalLensShading());
  EXPECT_TRUE(capture.Status().thermal.lens_shading_busy);

  capture.StartRecording(paths);
  ASSERT_TRUE(WaitStatus(capture, [](const CaptureStatus& s) { return s.phase == hwlab::flow::Phase::kSettling; }));
  std::this_thread::sleep_for(300ms);
  EXPECT_EQ(capture.Status().phase, hwlab::flow::Phase::kSettling);
  EXPECT_EQ(capture.Status().written, 0);

  thermal.OpenGate();
  EXPECT_TRUE(capture.WaitFinished(20s));
  const CaptureStatus status = capture.Status();
  capture.Stop();
  EXPECT_EQ(status.written, 20);
  EXPECT_FALSE(status.thermal.lens_shading_busy);
  EXPECT_NE(ReadText(paths.thermal_sidecar).find("\"source\": \"corrected\""), std::string::npos);
}

// 열화상이 녹화 중에 끊긴 뒤 돌아오지 않아도 RGB 녹화는 끝나고, 끊긴 뒤 RGB 마지막 프레임까지가 열화상 누락으로 남습니다.
TEST(CaptureThermal, ThermalLostUntilEndRecordsTrailingGap) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalScript thermal_script = ThermalScript();
  thermal_script.disconnect_after = 40;
  thermal_script.reopen_failures = -1;
  FakeThermalCamera thermal(thermal_script);
  const RecordingPaths paths = TempPaths("thermal_gone");
  Capture capture(camera, SmallSpec(75), &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;

  const CaptureStatus status = Record(capture, 10, paths);
  capture.Stop();

  EXPECT_EQ(status.end_reason, EndReason::kCompleted);
  EXPECT_EQ(status.written, 75);
  EXPECT_NE(status.thermal.connection, ThermalConnection::kReceiving);
  const std::string th = ReadText(paths.thermal_sidecar);
  const int64_t written = IntField(th, "written");
  const int64_t missing = IntField(th, "missing");
  EXPECT_GT(written, 0);
  // RGB 75장 동안 열화상은 written장만 왔으므로 나머지 대부분이 누락입니다.
  EXPECT_GE(written + missing, 70) << th;
  EXPECT_EQ(missing, status.thermal.missing);
}

// 열화상 미리보기 함수는 프레임 번호가 정한 간격의 배수인 프레임마다 열화상 수신 스레드에서 불립니다.
TEST(CaptureThermal, CallsThermalPreviewSinkEveryNFrames) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalCamera thermal(ThermalScript());
  Capture capture(camera, SmallSpec(45), &thermal);
  std::mutex mutex;
  std::vector<int64_t> indices;
  bool all_y16 = true;
  capture.SetThermalPreviewSink(5, [&](const hwlab::FrameView& view) {
    std::lock_guard<std::mutex> lock(mutex);
    indices.push_back(static_cast<int64_t>(view.index));
    all_y16 = all_y16 && view.format == hwlab::PixelFormat::kY16 && view.width == kThermalWidth &&
              view.stream == hwlab::StreamKind::kThermal;
  });
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  while (capture.Status().thermal.received < 50) std::this_thread::sleep_for(1ms);
  capture.Stop();

  std::lock_guard<std::mutex> lock(mutex);
  EXPECT_GE(indices.size(), 10u);
  EXPECT_TRUE(all_y16);
  for (const int64_t i : indices) EXPECT_EQ(i % 5, 0);
}

// 열화상 경로를 주지 않으면 열화상 카메라가 있어도 RGB 파일만 씁니다.
TEST(CaptureThermal, SkipsThermalFilesWithoutPaths) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalCamera thermal(ThermalScript());
  const RecordingPaths paths = TempPaths("rgb_only");
  Capture capture(camera, SmallSpec(20), &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  while (capture.Status().received < 5) std::this_thread::sleep_for(1ms);

  capture.StartRecording(paths.video, paths.sidecar);
  ASSERT_TRUE(capture.WaitFinished(10s));
  const CaptureStatus status = capture.Status();
  capture.Stop();

  EXPECT_EQ(status.written, 20);
  EXPECT_EQ(status.thermal.written, 0);
  EXPECT_GT(status.thermal.received, 0);
  EXPECT_TRUE(std::filesystem::exists(paths.sidecar));
  EXPECT_FALSE(std::filesystem::exists(paths.thermal_raw));
  EXPECT_FALSE(std::filesystem::exists(paths.thermal_sidecar));
}

// 녹화 직전 열화상 셔터 교정(자동 모드의 잠금 단계): 촬영 버튼 뒤 한 번 교정한 뒤 녹화하며,
// 녹화 동안 자동 교정을 멈추고 녹화가 끝나 미리보기로 돌아가면 되돌립니다.
TEST(CaptureThermal, CalibratesBeforeRecordingAndHoldsUntilPreview) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalCamera thermal(ThermalScript());
  const RecordingPaths paths = TempPaths("calibration_hold");
  // 셔터 교정은 잠금 단계에서 하므로 잠금이 있는 자동 모드로 녹화합니다.
  CaptureSpec spec = SmallSpec(30);
  spec.settings.mode = SettingMode::kAuto;
  Capture capture(camera, spec, &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  while (capture.Status().received < 5) std::this_thread::sleep_for(1ms);
  EXPECT_EQ(thermal.Calibrations(), 0);

  capture.StartRecording(paths);
  bool held_while_recording = false;
  std::optional<std::chrono::steady_clock::time_point> recording_seen;
  while (!capture.Status().finished) {
    if (capture.Status().phase == hwlab::flow::Phase::kRecording) {
      if (!recording_seen) recording_seen = std::chrono::steady_clock::now();
      held_while_recording = held_while_recording || thermal.AutoCalibrationHeld();
    }
    std::this_thread::sleep_for(1ms);
  }
  EXPECT_EQ(thermal.Calibrations(), 1);
  EXPECT_TRUE(held_while_recording);
  EXPECT_TRUE(recording_seen.has_value());

  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (thermal.AutoCalibrationHeld() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }
  EXPECT_FALSE(thermal.AutoCalibrationHeld());
  capture.Stop();
}

// 직접 지정 모드도 녹화 직전에 한 번 교정하고 녹화 동안 자동 교정을 멈추며, 녹화가 끝나면 되돌립니다.
TEST(CaptureThermal, ManualModeCalibratesBeforeRecordingAndHolds) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalCamera thermal(ThermalScript());
  const RecordingPaths paths = TempPaths("calibration_manual");
  CaptureSpec spec = SmallSpec(30);
  ASSERT_EQ(spec.settings.mode, SettingMode::kManual);
  Capture capture(camera, spec, &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  while (capture.Status().received < 5) std::this_thread::sleep_for(1ms);
  EXPECT_EQ(thermal.Calibrations(), 0);

  capture.StartRecording(paths);
  bool held_while_recording = false;
  bool calibrated_before_recording = true;
  while (!capture.Status().finished) {
    if (capture.Status().phase == hwlab::flow::Phase::kRecording) {
      held_while_recording = held_while_recording || thermal.AutoCalibrationHeld();
      calibrated_before_recording = calibrated_before_recording && thermal.Calibrations() == 1;
    }
    std::this_thread::sleep_for(1ms);
  }
  EXPECT_EQ(capture.Status().written, 30);
  EXPECT_EQ(thermal.Calibrations(), 1);
  EXPECT_TRUE(calibrated_before_recording);
  EXPECT_TRUE(held_while_recording);

  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (thermal.AutoCalibrationHeld() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }
  EXPECT_FALSE(thermal.AutoCalibrationHeld());
  capture.Stop();
}

// 셔터 교정을 지원하지 않는 열화상이어도 자동 모드 녹화는 기다림 없이 진행되고 끝납니다.
TEST(CaptureThermal, RecordsWhenThermalCalibrationUnsupported) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalScript script = ThermalScript();
  script.calibration_supported = false;
  FakeThermalCamera thermal(script);
  const RecordingPaths paths = TempPaths("calibration_unsupported");
  CaptureSpec spec = SmallSpec(20);
  spec.settings.mode = SettingMode::kAuto;
  Capture capture(camera, spec, &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  const CaptureStatus status = Record(capture, 5, paths);
  capture.Stop();
  EXPECT_EQ(status.written, 20);
  EXPECT_EQ(thermal.Calibrations(), 0);
  EXPECT_FALSE(thermal.AutoCalibrationHeld());
}

// 열 때 불러온 렌즈 셰이딩 보정 상태를 열화상 사이드카에 남깁니다. 보정 기능이 없는 카메라와 RGB 사이드카는 null입니다.
TEST(CaptureThermal, RecordsLensShadingInThermalSidecar) {
  const struct {
    const char* name;
    bool folder;
    std::optional<int64_t> saved_s;
    const char* json;
  } cases[] = {{"loaded", true, 1761205212, "\"source\": \"loaded\",\n    \"file_unix_s\": 1761205212"},
               {"none", true, std::nullopt, "\"source\": \"none\",\n    \"file_unix_s\": null"},
               {"unsupported", false, std::nullopt, "\"lens_shading\": null,"}};
  for (const auto& c : cases) {
    SCOPED_TRACE(c.name);
    FakeRgbCamera camera(RgbScript());
    FakeThermalCamera thermal(ThermalScript());
    if (c.folder) thermal.SetLensShadingFolder(LensShadingFolder(std::string("sidecar_") + c.name, c.saved_s));
    const RecordingPaths paths = TempPaths(std::string("lens_shading_") + c.name);
    Capture capture(camera, SmallSpec(20), &thermal);
    std::string error;
    ASSERT_TRUE(capture.Start(&error)) << error;
    const CaptureStatus status = Record(capture, 5, paths);
    capture.Stop();
    EXPECT_EQ(status.thermal.lens_shading.has_value(), c.folder);
    const std::string th = ReadText(paths.thermal_sidecar);
    EXPECT_NE(th.find(c.json), std::string::npos) << th;
    EXPECT_NE(ReadText(paths.sidecar).find("\"lens_shading\": null,"), std::string::npos);
  }
}

// 자동 모드 안정화 중에 중지하면 잠금 단계에서 건 자동 교정 멈춤을 녹화 끝과 같게 되돌립니다.
TEST(CaptureThermal, StopDuringSettlingReleasesCalibrationHold) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalCamera fake(ThermalScript());
  CountingThermalCamera thermal(fake, false);
  CaptureSpec spec = SmallSpec(30);
  spec.settings.mode = SettingMode::kAuto;
  spec.settle.black_level_settled_frame = 1'000'000;  // 안정화가 끝나지 않게 합니다
  Capture capture(camera, spec, &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  ASSERT_TRUE(WaitStatus(capture, [](const CaptureStatus& s) { return s.received >= 5; }));

  capture.StartRecording(TempPaths("hold_settling"));
  ASSERT_TRUE(WaitStatus(capture, [&](const CaptureStatus&) { return thermal.Holds() == 1; }));
  EXPECT_EQ(capture.Status().phase, hwlab::flow::Phase::kSettling);
  EXPECT_TRUE(fake.AutoCalibrationHeld());
  capture.StopRecording();

  EXPECT_TRUE(thermal.WaitBalanced(1));
  EXPECT_FALSE(fake.AutoCalibrationHeld());
  EXPECT_EQ(capture.Status().phase, hwlab::flow::Phase::kPreview);
  EXPECT_EQ(capture.Status().written, 0);
  capture.Stop();
  EXPECT_EQ(thermal.Releases(), 1);
}

// 실제 fps가 선언과 달라 녹화를 거부해도 잠금 단계에서 건 자동 교정 멈춤을 되돌립니다.
TEST(CaptureThermal, FpsRejectionReleasesCalibrationHold) {
  FakeCameraScript script{.initial = kInitial};
  script.device_period_us = 66'666;  // 장치 시각 15fps, 선언 30fps
  FakeRgbCamera camera(script);
  FakeThermalCamera fake(ThermalScript());
  CountingThermalCamera thermal(fake, false);
  CaptureSpec spec = SmallSpec(30);
  spec.settings.mode = SettingMode::kAuto;
  Capture capture(camera, spec, &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  ASSERT_TRUE(WaitStatus(capture, [](const CaptureStatus& s) { return s.received >= 15; }));

  capture.StartRecording(TempPaths("hold_rejected"));
  ASSERT_TRUE(capture.WaitFinished(5s));
  const CaptureStatus status = capture.Status();
  EXPECT_EQ(status.written, 0);
  EXPECT_NE(status.error.find("fps"), std::string::npos) << status.error;
  EXPECT_TRUE(thermal.WaitBalanced(1));
  EXPECT_FALSE(fake.AutoCalibrationHeld());
  capture.Stop();
}

// 직접 지정 모드에서 열화상 준비(셔터 교정) 중에 중지하면, 준비가 끝나 건 멈춤을 바로 되돌리고 녹화하지 않습니다.
TEST(CaptureThermal, StopWhilePreparingThermalReleasesCalibrationHold) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalCamera fake(ThermalScript());
  CountingThermalCamera thermal(fake, true);
  CaptureSpec spec = SmallSpec(30);
  ASSERT_EQ(spec.settings.mode, SettingMode::kManual);
  Capture capture(camera, spec, &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  ASSERT_TRUE(WaitStatus(capture, [](const CaptureStatus& s) { return s.received >= 5; }));

  capture.StartRecording(TempPaths("hold_preparing"));
  ASSERT_TRUE(thermal.WaitCalibrating());
  EXPECT_EQ(capture.Status().phase, hwlab::flow::Phase::kSettling);
  capture.StopRecording();
  ASSERT_TRUE(WaitStatus(capture, [](const CaptureStatus& s) { return s.phase == hwlab::flow::Phase::kPreview; }));
  thermal.OpenGate();

  EXPECT_TRUE(thermal.WaitBalanced(1));
  EXPECT_FALSE(fake.AutoCalibrationHeld());
  EXPECT_EQ(capture.Status().phase, hwlab::flow::Phase::kPreview);
  EXPECT_EQ(capture.Status().written, 0);
  capture.Stop();
}

// 정상 녹화가 끝나면 멈춤과 되돌림이 한 번씩 불립니다.
TEST(CaptureThermal, CompletedRecordingReleasesCalibrationHoldOnce) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalCamera fake(ThermalScript());
  CountingThermalCamera thermal(fake, false);
  Capture capture(camera, SmallSpec(20), &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  const CaptureStatus status = Record(capture, 5, TempPaths("hold_completed"));
  EXPECT_EQ(status.written, 20);
  EXPECT_TRUE(thermal.WaitBalanced(1));
  EXPECT_FALSE(fake.AutoCalibrationHeld());
  capture.Stop();
  EXPECT_EQ(thermal.Releases(), 1);
}

// 안정화 중에 촬영을 끝내지 않고 앱을 닫아도(Capture::Stop) 장치를 닫기 전에 멈춤을 되돌립니다.
TEST(CaptureThermal, StopReleasesCalibrationHoldBeforeClosing) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalCamera fake(ThermalScript());
  CountingThermalCamera thermal(fake, false);
  CaptureSpec spec = SmallSpec(30);
  spec.settings.mode = SettingMode::kAuto;
  spec.settle.black_level_settled_frame = 1'000'000;
  Capture capture(camera, spec, &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  ASSERT_TRUE(WaitStatus(capture, [](const CaptureStatus& s) { return s.received >= 5; }));
  capture.StartRecording(TempPaths("hold_shutdown"));
  ASSERT_TRUE(WaitStatus(capture, [&](const CaptureStatus&) { return thermal.Holds() == 1; }));

  capture.Stop();
  EXPECT_EQ(thermal.Releases(), 1);
}

// 프레임 번호로 정한 카메라 온도입니다. 넷 중 한 장은 값이 없고, 셔터 원시값은 셔터가 닫힐 때만 바뀌는 것처럼 30장마다 바뀝니다.
// fpa와 shutter_c는 글로 써도 끝자리가 바뀌지 않는 값입니다.
std::optional<CameraTemperature> TemperatureOf(int64_t index) {
  if (index % 4 == 1) return std::nullopt;
  CameraTemperature t;
  t.fpa = 88.0f + 0.0625f * static_cast<float>(index % 160);
  t.shutter_raw = static_cast<uint16_t>(22981 + index / 30);
  t.shutter_c = 36.625 + 0.25 * static_cast<double>(index % 7);
  return t;
}

// TemperatureOf로 카메라 온도를 주는 열화상입니다. 시험이 SupplyTemperatures를 부르기 전에 받은 프레임에는 값을 주지 않습니다.
class TemperatureThermalCamera final : public hwlab::platform::ThermalCamera {
 public:
  explicit TemperatureThermalCamera(FakeThermalCamera& inner) : inner_(inner) {}

  bool Open(std::string* error) override { return inner_.Open(error); }
  void Close() override { inner_.Close(); }
  std::string DeviceName() const override { return inner_.DeviceName(); }
  int32_t Width() const override { return inner_.Width(); }
  int32_t Height() const override { return inner_.Height(); }
  std::optional<hwlab::platform::ReceivedFrame> ReadFrame(int64_t timeout_us) override {
    std::optional<hwlab::platform::ReceivedFrame> frame = inner_.ReadFrame(timeout_us);
    if (frame) last_ = supplying_ ? TemperatureOf(static_cast<int64_t>(frame->view.index)) : std::nullopt;
    return frame;
  }
  ThermalConnection Connection() const override { return inner_.Connection(); }
  bool CalibrateNow(std::string* error) override { return inner_.CalibrateNow(error); }
  bool HoldAutoCalibration(bool hold, std::string* error) override { return inner_.HoldAutoCalibration(hold, error); }
  std::string AutoCalibrationSetting() const override { return inner_.AutoCalibrationSetting(); }
  std::optional<CameraTemperature> LastCameraTemperature() const override { return last_; }

  void SupplyTemperatures() { supplying_ = true; }

 private:
  FakeThermalCamera& inner_;
  std::atomic<bool> supplying_{false};
  std::optional<CameraTemperature> last_;  // 수신 스레드만 씁니다
};

// 줄 끝으로 나눕니다. 마지막 줄 끝 뒤는 줄로 세지 않습니다.
std::vector<std::string> Lines(const std::string& text) {
  std::vector<std::string> lines;
  std::stringstream stream(text);
  for (std::string line; std::getline(stream, line);) lines.push_back(line);
  return lines;
}

// 쉼표로 나눕니다. 빈 칸도 한 칸으로 셉니다.
std::vector<std::string> Cells(const std::string& line) {
  std::vector<std::string> cells;
  size_t from = 0;
  for (;;) {
    const size_t comma = line.find(',', from);
    cells.push_back(line.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
    if (comma == std::string::npos) return cells;
    from = comma + 1;
  }
}

// 카메라 온도를 주는 열화상은 원시값 파일에 쓴 프레임마다(기록 주기로 거른 뒤) 카메라 온도 파일에 한 줄을 씁니다. 번호와 시각은
// 열화상 사이드카의 같은 프레임 값이고 값은 카메라가 준 그대로입니다. 값을 처음 받기 전에 쓴 프레임과 값이 없는 프레임은 값 칸이 빕니다.
TEST(CaptureThermal, WritesCameraTemperatureForEachWrittenFrame) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalCamera inner(ThermalScript(kThirtyHz));
  TemperatureThermalCamera thermal(inner);
  const RecordingPaths paths = TempPaths("camera_temp");
  CaptureSpec spec = SmallSpec(60);
  spec.thermal_record_period_us = kNineHzRecordUs;
  Capture capture(camera, spec, &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;
  ASSERT_TRUE(WaitStatus(capture, [](const CaptureStatus& s) { return s.received >= 10; }));

  capture.StartRecording(paths);
  ASSERT_TRUE(WaitStatus(capture, [](const CaptureStatus& s) { return s.thermal.written >= 3 || s.finished; }));
  thermal.SupplyTemperatures();
  ASSERT_TRUE(capture.WaitFinished(20s));
  const CaptureStatus status = capture.Status();
  capture.Stop();

  EXPECT_TRUE(status.thermal.error.empty()) << status.thermal.error;
  const std::string th = ReadText(paths.thermal_sidecar);
  const std::vector<int64_t> index = IntArray(th, "index");
  const std::vector<int64_t> device = IntArray(th, "device_time_us");
  const std::vector<int64_t> host = IntArray(th, "host_time_us");
  ASSERT_GE(index.size(), 3u);
  ASSERT_EQ(device.size(), index.size());
  ASSERT_EQ(host.size(), index.size());
  const size_t frame_bytes = static_cast<size_t>(kThermalWidth) * kThermalHeight * 2;
  ASSERT_EQ(ReadBytes(paths.thermal_raw).size(), index.size() * frame_bytes);
  EXPECT_LT(static_cast<int64_t>(index.size()), IntField(th, "received")) << th;

  const std::vector<std::string> lines = Lines(ReadText(CameraTemperaturePath(paths)));
  ASSERT_EQ(lines.size(), index.size() + 1);
  EXPECT_EQ(lines[0], "index,device_time_us,host_time_us,fpa,shutter_raw,shutter_c");
  std::optional<size_t> first_value;
  size_t empty_after_first = 0;
  for (size_t k = 0; k < index.size(); ++k) {
    SCOPED_TRACE(lines[k + 1]);
    const std::vector<std::string> cells = Cells(lines[k + 1]);
    ASSERT_EQ(cells.size(), 6u);
    EXPECT_EQ(std::stoll(cells[0]), index[k]);
    EXPECT_EQ(std::stoll(cells[1]), device[k]);
    EXPECT_EQ(std::stoll(cells[2]), host[k]);
    if (!first_value && !cells[3].empty()) first_value = k;
    const std::optional<CameraTemperature> t = first_value ? TemperatureOf(index[k]) : std::nullopt;
    if (!t) {
      EXPECT_TRUE(cells[3].empty() && cells[4].empty() && cells[5].empty());
      if (first_value) ++empty_after_first;
      continue;
    }
    EXPECT_EQ(std::strtof(cells[3].c_str(), nullptr), t->fpa);
    EXPECT_EQ(std::stoll(cells[4]), t->shutter_raw);
    EXPECT_DOUBLE_EQ(std::stod(cells[5]), t->shutter_c);
  }
  // 값을 주기 시작하기 전에 쓴 세 장은 값 칸이 비고, 값을 준 뒤에도 값이 없는 프레임은 값 칸이 빕니다.
  ASSERT_TRUE(first_value.has_value());
  EXPECT_GE(*first_value, 3u);
  EXPECT_GT(empty_after_first, 0u);
}

// 카메라 온도를 주지 않는 열화상은 카메라 온도 파일을 만들지 않습니다.
TEST(CaptureThermal, SkipsCameraTemperatureFileWithoutValues) {
  FakeRgbCamera camera(RgbScript());
  FakeThermalCamera thermal(ThermalScript());
  const RecordingPaths paths = TempPaths("camera_temp_none");
  Capture capture(camera, SmallSpec(20), &thermal);
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;

  const CaptureStatus status = Record(capture, 5, paths);
  capture.Stop();

  EXPECT_GT(status.thermal.written, 0);
  EXPECT_TRUE(status.thermal.error.empty()) << status.thermal.error;
  EXPECT_TRUE(std::filesystem::exists(paths.thermal_raw));
  EXPECT_FALSE(std::filesystem::exists(CameraTemperaturePath(paths)));
}

}  // namespace
