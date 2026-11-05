#ifndef HWLAB_PLATFORM_FAKE_THERMAL_CAMERA_H_
#define HWLAB_PLATFORM_FAKE_THERMAL_CAMERA_H_

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/platform/thermal_camera.h"

namespace hwlab::platform {

/** 가짜 열화상 카메라가 만들 상황 목록입니다. 누락, 연결 끊김, 재연결, 수신 간격 변동을 만듭니다. */
struct FakeThermalScript {
  int32_t width = 640;
  int32_t height = 480;
  std::map<int64_t, int32_t> skips;  // 보낸 프레임 수가 키와 같을 때 그 앞에서 놓칠 장 수
  int64_t disconnect_after = -1;     // 이만큼 보낸 뒤 프레임이 오지 않음(열린 상태는 유지). -1이면 끊기지 않음
  int32_t reopen_failures = -1;      // 끊긴 뒤 Open이 실패하는 횟수. -1이면 다시 연결되지 않음
  int64_t device_period_us = 33'333;  // 장치가 프레임을 내는 간격(30Hz)
  int64_t jitter_us = 0;              // 수신 지연의 최대값. 프레임마다 0 이상 이 값 이하에서 고름
  uint32_t jitter_seed = 1;           // 같은 값이면 같은 지연 순서가 나옴
  int32_t wall_period_us = 1000;      // ReadFrame 한 번에 실제로 기다리는 간격. 수신 시각과 따로 두어 테스트를 빨리 끝냅니다
  ThermalLossRule loss_rule;
  // true이면 가상 시계 대신 PC 시계(steady_clock, 마이크로초)를 씁니다. ReadFrame은 프레임 수신 시각까지 실제로 기다리고
  // wall_period_us는 쓰지 않습니다. FakeCameraScript::pc_clock과 함께 켜면 가짜 RGB와 같은 시간축이 됩니다
  bool pc_clock = false;
  bool calibration_supported = true;  // false이면 CalibrateNow와 HoldAutoCalibration이 실패합니다(원래 설정을 모르는 장치)
  bool lens_shading_fails = false;    // true이면 CorrectLensShading이 실패합니다
};

/**
 * 장비 없이 열화상 저장과 끊김 처리를 시험하는 카메라입니다.
 *
 * 시각은 기본으로 시나리오가 정한 가상 시계(마이크로초, 0부터)이고, pc_clock이면 PC 시계입니다. 장치 프레임은 연 시각부터
 * device_period_us 간격으로 나오고(누락된 장의 자리도 셈) 0~jitter_us의 수신 지연 뒤에 도착합니다. 화소 값은 PixelValue입니다.
 * 처음에는 닫혀 있으므로 Open을 먼저 부릅니다.
 *
 * 렌즈 셰이딩 보정은 실제 구현과 같게 동작합니다. 보정 파일 폴더를 정하면 열 때마다 lens_shading_FakeThermal.cal이 있으면
 * 불러오고, CorrectLensShading이 그 파일을 씁니다. 파일 내용은 보정한 PC 시각(UTC 유닉스 초) 한 줄이며, 그 값을
 * file_unix_s로 씁니다. 화소 값은 바꾸지 않습니다.
 */
class FakeThermalCamera final : public ThermalCamera {
 public:
  explicit FakeThermalCamera(FakeThermalScript script);

  bool Open(std::string* error) override;
  void Close() override;
  std::string DeviceName() const override;
  int32_t Width() const override;
  int32_t Height() const override;
  std::optional<ReceivedFrame> ReadFrame(int64_t timeout_us) override;
  ThermalConnection Connection() const override;
  bool CalibrateNow(std::string* error) override;
  bool HoldAutoCalibration(bool hold, std::string* error) override;
  std::string AutoCalibrationSetting() const override;
  void SetLensShadingFolder(const std::string& folder) override;
  std::optional<LensShadingStatus> LensShading() const override;
  bool CorrectLensShading(std::string* error) override;

  /**
   * 장치 프레임의 화소 값입니다. 행마다, 프레임마다 값이 달라 바이트 순서나 행 어긋남을 찾을 수 있습니다.
   *
   * @param device_frame 누락된 장까지 센 장치 프레임 번호
   * @param x 열
   * @param y 행
   * @return 7000 이상 8999 이하의 원시값
   */
  static uint16_t PixelValue(int64_t device_frame, int32_t x, int32_t y);

  /** 이 카메라 시계(가상 시계 또는 PC 시계)의 지금 시각입니다. */
  Timestamp Now() const;

  /** 방금 받은 프레임의 장치 프레임 번호입니다. 받은 프레임이 없으면 -1입니다. */
  int64_t LastDeviceFrame() const;

  /** CalibrateNow가 성공한 횟수입니다. */
  int32_t Calibrations() const;

  /** 지금 자동 셔터 교정을 멈춘 상태인지입니다. Close가 되돌리면 false입니다. */
  bool AutoCalibrationHeld() const;

 private:
  std::filesystem::path LensShadingPath() const;  // mutex_를 잡고 부릅니다
  LensShadingStatus LoadLensShading() const;     // mutex_를 잡고 부릅니다
  std::optional<ReceivedFrame> ReadFrameOnPcClock(int64_t timeout_us);
  void PlanNextArrival();  // mutex_를 잡고 부릅니다
  ReceivedFrame Emit();    // mutex_를 잡고 부릅니다
  int64_t ClockNow() const;  // mutex_를 잡고 부릅니다

  FakeThermalScript script_;
  std::vector<uint8_t> pixels_;
  mutable std::mutex mutex_;
  bool open_ = false;
  bool connected_ = true;  // 장치가 USB에 붙어 있는지. 열린 상태와 따로 둡니다
  int32_t reopen_attempts_ = 0;
  int64_t sent_ = 0;
  int64_t device_frame_ = 0;  // 다음에 낼 장치 프레임 번호
  int64_t last_device_frame_ = -1;
  int64_t now_us_ = 0;
  int64_t next_ideal_us_ = 0;  // 다음 장치 프레임이 나오는 시각(지연 전)
  std::optional<int64_t> pending_arrival_us_;  // 누락된 장을 반영하고 지연을 더한 다음 프레임의 수신 시각
  Timestamp opened_at_;
  std::optional<Timestamp> last_frame_;
  uint32_t jitter_state_ = 1;
  int32_t calibrations_ = 0;
  bool held_ = false;
  std::string lens_shading_folder_;  // 비면 보정 기능이 없는 장치
  std::optional<LensShadingStatus> lens_shading_;  // 지금 연결의 보정 상태
  std::chrono::steady_clock::time_point next_due_{};
};

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_FAKE_THERMAL_CAMERA_H_
