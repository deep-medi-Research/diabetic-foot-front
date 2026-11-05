#ifndef HWLAB_CAMERA_SETTINGS_H_
#define HWLAB_CAMERA_SETTINGS_H_

#include <cmath>
#include <cstdint>
#include <optional>

namespace hwlab {

/**
 * 설정값의 종류입니다. 종류마다 새 값이 반영되는 프레임이 다릅니다(timing::SettleRule).
 *
 * @note kLight는 카메라 설정이 아니라 LED 조명 밝기입니다. 영상 밝기를 바꾸므로 카메라 설정 변경과 같은 사건으로 다루어
 *       안정화를 다시 기다리고, 녹화 중에 오면 녹화를 끝냅니다.
 */
enum class SettingKind : uint8_t {
  kExposure,
  kGain,
  kWhiteBalance,
  kLight,
};

/** 촬영 전 설정값을 정하는 방식입니다. */
enum class SettingMode : uint8_t {
  kAuto,    // 펌웨어 자동 노출을 촬영 버튼에서 잠금
  kManual,  // 사용자가 입력한 값
};

/**
 * 카메라 설정 전체입니다.
 *
 * 두 가지로 씁니다. 카메라에서 다시 읽은 값으로 쓰면 빈 항목은 읽지 못한 값이고,
 * 목표 상태로 쓰면 빈 항목은 바꾸지 않을 값입니다.
 *
 * @note 펌웨어 자동 노출 중에는 실제 노출값을 읽을 방법이 없어 exposure_step이 비어 있습니다(2026-10-16 실측).
 */
struct CameraState {
  std::optional<bool> exposure_auto;      // 펌웨어 자동 노출
  std::optional<int32_t> exposure_step;   // DirectShow 노출값. 1 커지면 노출 시간 2배
  std::optional<int32_t> gain;
  std::optional<bool> white_balance_auto;
  std::optional<int32_t> white_balance_k;
  std::optional<int32_t> sdk_frame_rate;  // 제조사 SDK로 지정한 fps
  std::optional<bool> locked;             // 확장 유닛 잠금

  friend bool operator==(const CameraState&, const CameraState&) = default;
};

/**
 * 노출 단계를 노출 시간(ms)으로 바꿉니다. 단계 n은 2ⁿ초입니다(Windows CameraControl_Exposure의 정의).
 *
 * @param step 노출 단계(CameraState::exposure_step)
 * @return 노출 시간(ms). -5는 31.25입니다
 * @note 카메라가 실제로 이 시간만큼 노출하는지는 재지 않았습니다. 정의에 따른 명목값입니다.
 */
inline double ExposureStepMs(int32_t step) { return std::ldexp(1000.0, step); }

}  // namespace hwlab

#endif  // HWLAB_CAMERA_SETTINGS_H_
