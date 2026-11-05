#include "hwlab/platform/thermal_camera.h"

namespace hwlab::platform {

ThermalConnection JudgeThermalConnection(bool open, Timestamp opened_at, std::optional<Timestamp> last_frame,
                                         Timestamp now, const ThermalLossRule& rule) {
  if (!open) return ThermalConnection::kClosed;
  if (!last_frame) {
    // 연 직후의 셔터 교정 동안 프레임이 없으므로 받던 중의 끊김보다 길게 기다립니다.
    return IntervalUs(opened_at, now) < rule.first_frame_us ? ThermalConnection::kWaiting : ThermalConnection::kLost;
  }
  return IntervalUs(*last_frame, now) < rule.stall_us ? ThermalConnection::kReceiving : ThermalConnection::kLost;
}

}  // namespace hwlab::platform

// SDK 구현(windows/thermal_camera_windows.cc)을 빌드하지 않을 때는 실제 열화상 카메라가 없음을 nullptr로 알립니다.
#ifndef HWLAB_HAS_THERMAL_SDK
namespace hwlab::platform {

std::unique_ptr<ThermalCamera> MakeDeviceThermalCamera() { return nullptr; }
std::unique_ptr<ThermalCamera> MakeDeviceThermalCamera(const pixel::CalibrationRule&) { return nullptr; }

}  // namespace hwlab::platform
#endif
