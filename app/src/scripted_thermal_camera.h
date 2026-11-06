#ifndef HWLAB_APP_SCRIPTED_THERMAL_CAMERA_H_
#define HWLAB_APP_SCRIPTED_THERMAL_CAMERA_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "hwlab/platform/thermal_camera.h"

/**
 * 가짜 열화상 카메라 앞에 두어 열화상 안내 화면의 두 상태를 재현하는 카메라입니다. 명령줄 시험 옵션에만 씁니다.
 *
 * - absent가 true이면 Open이 항상 실패합니다(장치를 찾지 못함, 연결 상태 kClosed)
 * - first_frame_delay_ms가 양수이면 연 뒤 그 시간 동안 프레임을 주지 않고 kWaiting을 알립니다(셔터 교정 대기)
 */
class ScriptedThermalCamera final : public hwlab::platform::ThermalCamera {
 public:
  /**
   * @param inner 실제로 프레임을 내는 카메라(보통 가짜 열화상)
   * @param absent true면 Open이 항상 실패합니다
   * @param first_frame_delay_ms 연 뒤 첫 프레임을 주기까지의 시간
   */
  ScriptedThermalCamera(std::unique_ptr<hwlab::platform::ThermalCamera> inner, bool absent, int64_t first_frame_delay_ms);

  bool Open(std::string* error) override;
  void Close() override;
  std::string DeviceName() const override;
  int32_t Width() const override;
  int32_t Height() const override;
  std::optional<hwlab::platform::ReceivedFrame> ReadFrame(int64_t timeout_us) override;
  hwlab::platform::ThermalConnection Connection() const override;
  void SetLensShadingFolder(const std::string& folder) override;
  std::optional<hwlab::platform::LensShadingStatus> LensShading() const override;
  bool CorrectLensShading(const std::optional<hwlab::timing::LensShadingConditions>& made, std::string* error) override;
  std::optional<hwlab::platform::CameraTemperature> LastCameraTemperature() const override;

 private:
  bool Holding() const;

  std::unique_ptr<hwlab::platform::ThermalCamera> inner_;
  const bool absent_;
  const std::chrono::milliseconds delay_;
  mutable std::mutex mutex_;
  bool open_ = false;                                 // mutex_
  bool inner_open_ = false;                           // mutex_. 대기가 끝나 안쪽 카메라를 열었는지
  std::chrono::steady_clock::time_point opened_at_;  // mutex_
};

#endif  // HWLAB_APP_SCRIPTED_THERMAL_CAMERA_H_
