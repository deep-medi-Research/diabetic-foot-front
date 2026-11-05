#ifndef HWLAB_FLOW_SETTINGS_PLAN_H_
#define HWLAB_FLOW_SETTINGS_PLAN_H_

#include <cstdint>
#include <vector>

#include "hwlab/camera_settings.h"

namespace hwlab::flow {

/** 촬영 한 번에 쓸 설정값의 선언입니다. 화면이나 선언 파일이 이 값을 만듭니다. */
struct SettingsSpec {
  SettingMode mode = SettingMode::kAuto;
  int32_t exposure_step = 0;  // 직접 지정일 때만 씁니다
  int32_t gain = 0;
  int32_t white_balance_k = 0;
  int32_t sdk_frame_rate = 0;
};

/**
 * 미리보기 동안 카메라가 가져야 할 목표 상태를 만듭니다.
 *
 * @param spec 설정값 선언
 * @return 자동 맞춤은 펌웨어 자동 노출을 켜고 잠금을 풀며, 직접 지정은 입력한 값으로 잠급니다
 */
CameraState PreviewTarget(const SettingsSpec& spec);

/**
 * 녹화 동안 카메라가 가져야 할 목표 상태를 만듭니다.
 *
 * @param spec 설정값 선언
 * @return 미리보기 목표에 잠금을 더한 상태. 두 방식 모두 잠금입니다
 */
CameraState RecordingTarget(const SettingsSpec& spec);

/** 카메라에 보내는 설정 명령의 종류입니다. */
enum class CommandKind : uint8_t {
  kSdkFrameRate,
  kUnlock,
  kExposureAuto,
  kExposureManual,  // value: 노출 단계
  kGain,
  kWhiteBalanceManual,  // value: K
  kWhiteBalanceAuto,
  kLock,
};

/** 설정 명령 하나입니다. value는 노출 단계, 게인, 색온도, fps처럼 값이 필요한 명령에만 씁니다. */
struct SettingCommand {
  CommandKind kind = CommandKind::kGain;
  int32_t value = 0;

  friend bool operator==(const SettingCommand&, const SettingCommand&) = default;
};

/**
 * 지금 상태에서 목표 상태로 가는 명령을 실행 순서대로 만듭니다.
 *
 * 같은 항목은 명령을 만들지 않습니다. 순서는 fps, 잠금 풀기, 노출, 게인, 색온도, 잠금입니다.
 * 잠금 중에는 펌웨어가 노출을 바꾸지 않으므로 풀기를 노출보다 먼저 둡니다.
 * 잠긴 카메라를 잠금 목표로 두고 노출, 게인, 색온도를 바꿀 때도 풀고 쓴 뒤 다시 잠급니다. 잠긴 채 쓴 값은 영상에
 * 반영되지 않았기 때문입니다(2026-10-27 실기, 색온도).
 *
 * @param current 카메라에서 다시 읽은 지금 상태. 읽지 못한 항목은 다르다고 봅니다
 * @param target 목표 상태. 빈 항목은 바꾸지 않습니다
 * @return 명령 목록. 이미 목표에 있으면 빈 목록
 */
std::vector<SettingCommand> PlanCommands(const CameraState& current, const CameraState& target);

/** 카메라 설정의 항목입니다. 목표와 다른 항목을 알릴 때 씁니다. */
enum class SettingField : uint8_t {
  kExposureAuto,
  kExposureStep,
  kGain,
  kWhiteBalanceAuto,
  kWhiteBalanceK,
  kSdkFrameRate,
  kLocked,
};

/**
 * 다시 읽은 값이 목표와 다른 항목을 찾습니다.
 *
 * @param readback 명령을 실행한 뒤 다시 읽은 값
 * @param target 목표 상태
 * @return 다른 항목 목록. 목표가 비어 있는 항목과, 목표가 자동인 노출과 색온도의 값은 보지 않습니다
 * @note 노출 단계는 범위 밖 값을 넣으면 카메라가 성공을 돌려주고 다른 값으로 설정하므로, 이 함수로 확인해야 합니다.
 */
std::vector<SettingField> FindMismatches(const CameraState& readback, const CameraState& target);

/**
 * 실험 뒤 카메라를 시작 전 상태로 되돌리는 명령을 실행 순서대로 만듭니다.
 *
 * PlanCommands와 같지만 자동 모드인 노출과 색온도도 저장값을 되돌립니다. 자동 모드에서는 값을 쓸 명령이 없으므로,
 * 저장값이 다르면 수동 명령으로 값을 쓴 뒤 자동 명령으로 모드를 돌립니다.
 *
 * @param current 자동 모드에서도 저장값을 채워 읽은 지금 상태(RgbCamera::ReadStoredState)
 * @param saved 시작 전에 같은 방법으로 읽어 둔 상태. 빈 항목과 0 이하의 sdk_frame_rate는 실행 전 값을 모르는 것으로 보고
 *        바꾸지 않습니다(FindUnknownSaved)
 * @return 명령 목록. 이미 시작 전 상태이면 빈 목록
 * @note 2026-10-19 실기에서 자동 명령만 보내면 모드는 돌아왔지만 저장값은 녹화 때 수동 값(-5, 4600K)으로 남았습니다.
 *       USB 재연결 직후 읽은 sdk_frame_rate가 0인 경우가 있어, 0을 그대로 되돌리면 카메라에 fps 0 명령을 보내게 됩니다.
 */
std::vector<SettingCommand> PlanRestore(const CameraState& current, const CameraState& saved);

/**
 * 되돌린 뒤 다시 읽은 값이 시작 전 상태와 다른 항목을 찾습니다.
 *
 * FindMismatches와 달리 자동 모드인 노출과 색온도의 저장값도 비교합니다.
 *
 * @param readback 되돌린 뒤 저장값을 채워 다시 읽은 값
 * @param saved 시작 전에 읽어 둔 상태
 * @return 다른 항목 목록. 시작 전에 읽지 못한 항목(FindUnknownSaved)은 보지 않습니다
 */
std::vector<SettingField> FindRestoreMismatches(const CameraState& readback, const CameraState& saved);

/**
 * 시작 전에 읽어 둔 상태에서 값을 모르는 항목을 찾습니다. 이 항목은 되돌리지도 비교하지도 않으므로 따로 알립니다.
 *
 * @param saved 시작 전에 읽어 둔 상태
 * @return 빈 항목과, sdk_frame_rate가 0 이하이면 그 항목. 순서는 SettingField 선언 순서입니다
 * @note sdk_frame_rate 0은 카메라가 실제로 0 fps라는 뜻이 아니라 USB 재연결 직후 SDK가 값을 채우지 않은 것입니다.
 */
std::vector<SettingField> FindUnknownSaved(const CameraState& saved);

/**
 * 설정 항목의 이름입니다. 복원에 실패한 항목을 출력할 때 씁니다.
 *
 * @param field 항목
 * @return 사이드카의 키와 같은 영문 이름(예: "exposure_step")
 */
const char* NameOf(SettingField field);

/**
 * 설정 명령 종류의 이름입니다. 실패한 명령을 출력할 때 씁니다.
 *
 * @param kind 명령 종류
 * @return 영문 이름(예: "exposure_manual")
 */
const char* NameOf(CommandKind kind);

}  // namespace hwlab::flow

#endif  // HWLAB_FLOW_SETTINGS_PLAN_H_
