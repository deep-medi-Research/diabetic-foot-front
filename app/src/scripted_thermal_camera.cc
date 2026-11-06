#include "scripted_thermal_camera.h"

#include <algorithm>
#include <thread>
#include <utility>

ScriptedThermalCamera::ScriptedThermalCamera(std::unique_ptr<hwlab::platform::ThermalCamera> inner, bool absent,
                                             int64_t first_frame_delay_ms)
    : inner_(std::move(inner)), absent_(absent), delay_(std::max<int64_t>(0, first_frame_delay_ms)) {}

bool ScriptedThermalCamera::Open(std::string* error) {
  if (absent_) {
    if (error != nullptr) *error = "열화상 카메라를 찾지 못했습니다(시험 옵션)";
    return false;
  }
  // 안쪽 카메라는 대기가 끝난 뒤 엽니다. 미리 열면 대기 동안의 프레임이 한꺼번에 나오기 때문입니다.
  if (delay_.count() == 0 && !inner_->Open(error)) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  open_ = true;
  inner_open_ = delay_.count() == 0;
  opened_at_ = std::chrono::steady_clock::now();
  return true;
}

void ScriptedThermalCamera::Close() {
  inner_->Close();
  std::lock_guard<std::mutex> lock(mutex_);
  open_ = false;
  inner_open_ = false;
}

std::string ScriptedThermalCamera::DeviceName() const { return inner_->DeviceName(); }

int32_t ScriptedThermalCamera::Width() const { return inner_->Width(); }

int32_t ScriptedThermalCamera::Height() const { return inner_->Height(); }

void ScriptedThermalCamera::SetLensShadingFolder(const std::string& folder) { inner_->SetLensShadingFolder(folder); }

std::optional<hwlab::platform::LensShadingStatus> ScriptedThermalCamera::LensShading() const {
  return inner_->LensShading();
}

bool ScriptedThermalCamera::CorrectLensShading(const std::optional<hwlab::timing::LensShadingConditions>& made,
                                               std::string* error) {
  return inner_->CorrectLensShading(made, error);
}

std::optional<hwlab::platform::CameraTemperature> ScriptedThermalCamera::LastCameraTemperature() const {
  return inner_->LastCameraTemperature();
}

bool ScriptedThermalCamera::Holding() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return open_ && std::chrono::steady_clock::now() < opened_at_ + delay_;
}

std::optional<hwlab::platform::ReceivedFrame> ScriptedThermalCamera::ReadFrame(int64_t timeout_us) {
  if (Holding()) {
    std::this_thread::sleep_for(std::chrono::microseconds(timeout_us));
    return std::nullopt;
  }
  bool open_inner = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    open_inner = open_ && !inner_open_;
  }
  if (open_inner) {
    std::string ignored;
    if (!inner_->Open(&ignored)) return std::nullopt;
    std::lock_guard<std::mutex> lock(mutex_);
    inner_open_ = true;
  }
  return inner_->ReadFrame(timeout_us);
}

hwlab::platform::ThermalConnection ScriptedThermalCamera::Connection() const {
  // 안쪽 카메라를 열기 전에는 안쪽 카메라가 닫힘을 알리므로 여기서 첫 프레임 대기 상태를 알립니다.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (open_ && !inner_open_) return hwlab::platform::ThermalConnection::kWaiting;
  }
  return inner_->Connection();
}
