#include "hwlab/platform/fake_camera.h"

#include <chrono>
#include <cmath>
#include <thread>

namespace hwlab::platform {

FakeRgbCamera::FakeRgbCamera(FakeCameraScript script)
    : script_(std::move(script)),
      pixels_(static_cast<size_t>(script_.width) * static_cast<size_t>(script_.height) * 2),
      state_(script_.initial),
      jitter_state_(script_.host_jitter_seed) {}

std::optional<ReceivedFrame> FakeRgbCamera::ReadFrame() {
  if (script_.stall_after >= 0 && sent_ >= script_.stall_after) {
    std::unique_lock<std::mutex> lock(mutex_);
    interrupted_cv_.wait(lock, [this] { return interrupted_; });
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (interrupted_) return std::nullopt;
  }
  if (script_.disconnect_after >= 0 && sent_ >= script_.disconnect_after) {
    connected_ = false;
  }
  if (!connected_) return std::nullopt;
  // 프레임을 만드는 시간까지 간격 안에 들어가도록 정해진 시각에 맞춰 내보냅니다.
  // 기다린 뒤 만들면 간격이 (기다림 + 만들기)로 늘어나 30fps가 20fps로 떨어졌습니다.
  const auto now = std::chrono::steady_clock::now();
  if (next_due_.time_since_epoch().count() == 0) next_due_ = now;
  int32_t skipped = 0;
  if (const auto it = script_.skips.find(sent_); it != script_.skips.end()) skipped = it->second;
  // PC 시계를 쓰면 빠진 장의 시간도 실제로 지나야 다른 가짜 장치와 시각이 맞습니다.
  if (script_.pc_clock) next_due_ += std::chrono::microseconds(static_cast<int64_t>(script_.wall_period_us) * skipped);
  next_due_ += std::chrono::microseconds(script_.wall_period_us);
  std::this_thread::sleep_until(next_due_);
  device_frame_ += skipped;
  int32_t fps = 30;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.sdk_frame_rate && *state_.sdk_frame_rate > 0) fps = *state_.sdk_frame_rate;
  }
  if (script_.varied_pixels) {
    const int32_t row_bytes = script_.width * 2;
    for (int32_t r = 0; r < script_.height; ++r) {
      for (int32_t b = 0; b < row_bytes; ++b) {
        pixels_[static_cast<size_t>(r) * row_bytes + b] = PatternByte(sent_, b, r);
      }
    }
  } else {
    const uint8_t y = static_cast<uint8_t>(sent_ % 200 + 20);
    for (size_t i = 0; i < pixels_.size(); i += 2) {
      pixels_[i] = 128;
      pixels_[i + 1] = y;
    }
  }
  int64_t capture_us = 0;
  if (script_.pc_clock) {
    capture_us = std::chrono::duration_cast<std::chrono::microseconds>(next_due_.time_since_epoch()).count();
  } else {
    capture_us = script_.device_period_us > 0 ? device_frame_ * script_.device_period_us : device_frame_ * 1'000'000 / fps;
  }
  ReceivedFrame frame;
  frame.view.format = PixelFormat::kUyvy;
  frame.view.width = script_.width;
  frame.view.height = script_.height;
  frame.view.plane[0] = pixels_.data();
  frame.view.row_stride[0] = script_.width * 2;
  frame.view.index = static_cast<FrameIndex>(sent_);
  frame.view.time = Timestamp{script_.device_offset_us +
                              static_cast<int64_t>(std::llround(script_.device_rate * static_cast<double>(capture_us)))};
  int64_t latency_us = 0;
  if (script_.pc_clock) {
    jitter_state_ = jitter_state_ * 1664525u + 1013904223u;
    latency_us = script_.host_latency_us +
                 (script_.host_jitter_us > 0 ? static_cast<int64_t>(jitter_state_ >> 8) % (script_.host_jitter_us + 1) : 0);
  }
  frame.host_time = Timestamp{capture_us + latency_us};
  ++sent_;
  ++device_frame_;
  return frame;
}

uint8_t FakeRgbCamera::PatternByte(int64_t index, int32_t byte_x, int32_t y) {
  return static_cast<uint8_t>((static_cast<int64_t>(byte_x) * 7 + static_cast<int64_t>(y) * 13 + index * 3) % 251);
}

bool FakeRgbCamera::Reopen() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (interrupted_) return false;
  }
  if (connected_) return true;
  if (script_.reopen_failures < 0 || reopen_attempts_++ < script_.reopen_failures) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  connected_ = true;
  script_.disconnect_after = -1;
  state_ = script_.factory;
  next_due_ = {};
  return true;
}

CameraState FakeRgbCamera::ReadState() {
  std::lock_guard<std::mutex> lock(mutex_);
  CameraState readback = state_;
  // 실제 카메라처럼 펌웨어 자동 노출 중에는 노출값을 읽지 못합니다.
  if (readback.exposure_auto == true) readback.exposure_step.reset();
  return readback;
}

CameraState FakeRgbCamera::ReadStoredState() {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

bool FakeRgbCamera::Execute(const flow::SettingCommand& c) {
  std::lock_guard<std::mutex> lock(mutex_);
  executed_.push_back(c);
  switch (c.kind) {
    case flow::CommandKind::kSdkFrameRate: state_.sdk_frame_rate = c.value; break;
    case flow::CommandKind::kUnlock: state_.locked = false; break;
    case flow::CommandKind::kLock: state_.locked = true; break;
    case flow::CommandKind::kExposureAuto: state_.exposure_auto = true; break;
    case flow::CommandKind::kExposureManual:
      state_.exposure_auto = false;
      state_.exposure_step = c.value;
      break;
    case flow::CommandKind::kGain: state_.gain = c.value; break;
    case flow::CommandKind::kWhiteBalanceAuto: state_.white_balance_auto = true; break;
    case flow::CommandKind::kWhiteBalanceManual:
      state_.white_balance_auto = false;
      state_.white_balance_k = c.value;
      break;
  }
  return true;
}

std::vector<flow::SettingCommand> FakeRgbCamera::ExecutedCommands() {
  std::lock_guard<std::mutex> lock(mutex_);
  return executed_;
}

CameraState FakeRgbCamera::State() {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

void FakeRgbCamera::Interrupt() {
  std::lock_guard<std::mutex> lock(mutex_);
  interrupted_ = true;
  ++interrupts_;
  interrupted_cv_.notify_all();
}

int32_t FakeRgbCamera::Interrupts() {
  std::lock_guard<std::mutex> lock(mutex_);
  return interrupts_;
}

}  // namespace hwlab::platform
