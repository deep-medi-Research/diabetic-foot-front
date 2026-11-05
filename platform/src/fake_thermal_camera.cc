#include "hwlab/platform/fake_thermal_camera.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "hwlab/platform/shot_folder.h"

namespace hwlab::platform {

namespace {

// DeviceName과 보정 파일 이름이 같은 이름을 쓰게 합니다(실제 구현은 장치 이름으로 파일 이름을 만듭니다).
constexpr const char* kDeviceName = "FakeThermal";

int64_t PcMicros() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

FakeThermalCamera::FakeThermalCamera(FakeThermalScript script)
    : script_(std::move(script)),
      pixels_(static_cast<size_t>(script_.width) * static_cast<size_t>(script_.height) * 2),
      jitter_state_(script_.jitter_seed) {}

uint16_t FakeThermalCamera::PixelValue(int64_t device_frame, int32_t x, int32_t y) {
  return static_cast<uint16_t>(7000 + (x + 2 * static_cast<int64_t>(y) + 5 * device_frame) % 2000);
}

bool FakeThermalCamera::Open(std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  open_ = false;
  if (!connected_) {
    if (script_.reopen_failures < 0 || reopen_attempts_++ < script_.reopen_failures) {
      if (error != nullptr) *error = "열화상 카메라를 찾지 못했습니다";
      return false;
    }
    connected_ = true;
    script_.disconnect_after = -1;
  }
  if (script_.pc_clock) now_us_ = PcMicros();
  open_ = true;
  lens_shading_ = lens_shading_folder_.empty() ? std::nullopt : std::optional<LensShadingStatus>(LoadLensShading());
  opened_at_ = Timestamp{now_us_};
  last_frame_.reset();
  next_ideal_us_ = now_us_ + script_.device_period_us;
  pending_arrival_us_.reset();
  next_due_ = {};
  return true;
}

void FakeThermalCamera::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  open_ = false;
  held_ = false;
}

std::string FakeThermalCamera::DeviceName() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return open_ ? kDeviceName : "";
}

int32_t FakeThermalCamera::Width() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return open_ ? script_.width : 0;
}

int32_t FakeThermalCamera::Height() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return open_ ? script_.height : 0;
}

std::optional<ReceivedFrame> FakeThermalCamera::ReadFrame(int64_t timeout_us) {
  if (script_.pc_clock) return ReadFrameOnPcClock(timeout_us);
  // 실제 수신 스레드처럼 한 번 부를 때마다 조금 기다립니다. 정해진 시각에 맞춰 만드는 시간까지 간격에 넣습니다.
  if (script_.wall_period_us > 0) {
    const auto now = std::chrono::steady_clock::now();
    if (next_due_.time_since_epoch().count() == 0 || next_due_ < now) next_due_ = now;
    next_due_ += std::chrono::microseconds(script_.wall_period_us);
    std::this_thread::sleep_until(next_due_);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!open_) return std::nullopt;
  if (script_.disconnect_after >= 0 && sent_ >= script_.disconnect_after) connected_ = false;
  if (!connected_) {
    // 실제 SDK는 뽑아도 열린 상태로 남고 프레임만 주지 않으므로 시간만 흐릅니다.
    now_us_ += timeout_us;
    return std::nullopt;
  }
  PlanNextArrival();
  if (*pending_arrival_us_ - now_us_ > timeout_us) {
    now_us_ += timeout_us;
    return std::nullopt;
  }
  now_us_ = std::max(now_us_, *pending_arrival_us_);
  pending_arrival_us_.reset();
  return Emit();
}

std::optional<ReceivedFrame> FakeThermalCamera::ReadFrameOnPcClock(int64_t timeout_us) {
  std::unique_lock<std::mutex> lock(mutex_);
  now_us_ = PcMicros();
  if (!open_) return std::nullopt;
  if (script_.disconnect_after >= 0 && sent_ >= script_.disconnect_after) connected_ = false;
  std::optional<int64_t> arrival;
  if (connected_) {
    PlanNextArrival();
    arrival = pending_arrival_us_;
  }
  const int64_t wait_us = arrival ? *arrival - now_us_ : timeout_us + 1;
  // 다른 스레드의 Connection, Now 호출을 막지 않도록 기다리는 동안 잠금을 풉니다.
  lock.unlock();
  std::this_thread::sleep_for(std::chrono::microseconds(std::clamp<int64_t>(wait_us, 0, timeout_us)));
  lock.lock();
  now_us_ = PcMicros();
  if (!open_ || !arrival || wait_us > timeout_us) return std::nullopt;
  pending_arrival_us_.reset();
  // Windows의 대기 단위(약 15ms)만큼 늦게 반환되어도 프레임 시각은 예정 도착 시각으로 두어 간격이 대기 단위에 좌우되지 않게 합니다.
  now_us_ = std::min(now_us_, *arrival);
  return Emit();
}

void FakeThermalCamera::PlanNextArrival() {
  if (pending_arrival_us_) return;
  if (const auto it = script_.skips.find(sent_); it != script_.skips.end()) {
    device_frame_ += it->second;
    next_ideal_us_ += it->second * script_.device_period_us;
  }
  jitter_state_ = jitter_state_ * 1664525u + 1013904223u;
  const int64_t delay = script_.jitter_us > 0 ? static_cast<int64_t>(jitter_state_ >> 8) % (script_.jitter_us + 1) : 0;
  const int64_t floor = last_frame_ ? last_frame_->us + 1 : 0;
  pending_arrival_us_ = std::max(next_ideal_us_ + delay, floor);
}

ReceivedFrame FakeThermalCamera::Emit() {
  for (int32_t y = 0; y < script_.height; ++y) {
    uint8_t* row = pixels_.data() + static_cast<size_t>(y) * static_cast<size_t>(script_.width) * 2;
    for (int32_t x = 0; x < script_.width; ++x) {
      const uint16_t v = PixelValue(device_frame_, x, y);
      row[x * 2] = static_cast<uint8_t>(v & 0xFF);
      row[x * 2 + 1] = static_cast<uint8_t>(v >> 8);
    }
  }
  ReceivedFrame frame;
  frame.view.format = PixelFormat::kY16;
  frame.view.width = script_.width;
  frame.view.height = script_.height;
  frame.view.plane[0] = pixels_.data();
  frame.view.row_stride[0] = script_.width * 2;
  frame.view.stream = StreamKind::kThermal;
  frame.view.index = static_cast<FrameIndex>(sent_);
  frame.view.time = Timestamp{now_us_};
  frame.host_time = frame.view.time;
  last_frame_ = frame.host_time;
  last_device_frame_ = device_frame_;
  ++sent_;
  ++device_frame_;
  next_ideal_us_ += script_.device_period_us;
  return frame;
}

int64_t FakeThermalCamera::ClockNow() const { return script_.pc_clock ? PcMicros() : now_us_; }

ThermalConnection FakeThermalCamera::Connection() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return JudgeThermalConnection(open_, opened_at_, last_frame_, Timestamp{ClockNow()}, script_.loss_rule);
}

bool FakeThermalCamera::CalibrateNow(std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!open_ || !script_.calibration_supported) {
    if (error != nullptr) *error = open_ ? "셔터 교정을 지원하지 않습니다" : "열려 있지 않습니다";
    return false;
  }
  ++calibrations_;
  return true;
}

bool FakeThermalCamera::HoldAutoCalibration(bool hold, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!open_ || !script_.calibration_supported) {
    if (error != nullptr) *error = open_ ? "원래 셔터 모드를 모릅니다" : "열려 있지 않습니다";
    return false;
  }
  held_ = hold;
  return true;
}

std::string FakeThermalCamera::AutoCalibrationSetting() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!open_) return {};
  return held_ ? "fake, held" : "fake";
}

void FakeThermalCamera::SetLensShadingFolder(const std::string& folder) {
  std::lock_guard<std::mutex> lock(mutex_);
  lens_shading_folder_ = folder;
}

std::optional<LensShadingStatus> FakeThermalCamera::LensShading() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return open_ ? lens_shading_ : std::nullopt;
}

bool FakeThermalCamera::CorrectLensShading(std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!open_ || lens_shading_folder_.empty()) {
    if (error != nullptr) *error = open_ ? "렌즈 셰이딩 보정 파일 폴더가 정해지지 않았습니다" : "열려 있지 않습니다";
    return false;
  }
  if (script_.lens_shading_fails) {
    lens_shading_ = LensShadingStatus{{record::LensShadingSource::kFailed, std::nullopt}, "렌즈 셰이딩 보정이 실패했습니다"};
    if (error != nullptr) *error = lens_shading_->error;
    return false;
  }
  const int64_t now_s =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  const std::filesystem::path path = LensShadingPath();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::trunc);
  file << now_s << "\n";
  file.close();
  lens_shading_ = file ? LensShadingStatus{{record::LensShadingSource::kCorrected, now_s}, std::string()}
                       : LensShadingStatus{{record::LensShadingSource::kCorrected, std::nullopt},
                                           "보정은 적용했지만 보정 파일을 저장하지 못했습니다"};
  return true;
}

std::filesystem::path FakeThermalCamera::LensShadingPath() const {
  return PathFromUtf8(lens_shading_folder_) / PathFromUtf8(LensShadingFileName(kDeviceName));
}

LensShadingStatus FakeThermalCamera::LoadLensShading() const {
  std::ifstream file(LensShadingPath());
  if (!file) return {};
  int64_t saved_s = 0;
  if (!(file >> saved_s)) {
    return {{record::LensShadingSource::kFailed, std::nullopt}, "보정 파일을 불러오지 못했습니다"};
  }
  return {{record::LensShadingSource::kLoaded, saved_s}, std::string()};
}

int32_t FakeThermalCamera::Calibrations() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return calibrations_;
}

bool FakeThermalCamera::AutoCalibrationHeld() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return held_;
}

Timestamp FakeThermalCamera::Now() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return Timestamp{ClockNow()};
}

int64_t FakeThermalCamera::LastDeviceFrame() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_device_frame_;
}

}  // namespace hwlab::platform
