#ifndef HWLAB_FLOW_FLOW_H_
#define HWLAB_FLOW_FLOW_H_

#include <cstdint>
#include <vector>

#include "hwlab/camera_settings.h"
#include "hwlab/stream.h"
#include "hwlab/timing/settle.h"

namespace hwlab::flow {

/**
 * 촬영 상태입니다(설계 문서 표 4).
 *
 * @note 마무리(파일 닫기, 누락 검사)는 상태로 두지 않고 platform이 kEndRecording을 받아 합니다.
 */
enum class Phase : uint8_t {
  kPreview,
  kSettling,
  kRecording,
  kDisconnected,
};

/** 촬영 흐름에 넣는 사건의 종류입니다. */
enum class EventKind : uint8_t {
  kFrame,           // 프레임 하나를 받음. 어느 스트림의 프레임인지는 Event::stream입니다
  kSettingChanged,  // platform이 카메라에 설정값을 설정함
  kStartPressed,
  kStopPressed,
  kDisconnected,
  kReconnected,
  kLockApplied,     // 제어 스레드가 잠금 요청(kLockSettings)을 실행하고 다시 읽기까지 마침
  kThermalPrepared, // 제어 스레드가 열화상 준비 요청(kPrepareThermal)을 실행함
};

/**
 * 촬영 흐름에 넣는 사건 하나입니다.
 *
 * @note RGB가 아닌 프레임은 녹화 중에만 kWriteFrame을 받고, 안정화 판정과 목표 프레임 수에는 들어가지 않습니다.
 *       녹화의 시작과 끝은 RGB 프레임으로만 정합니다.
 * @note 녹화는 안정화 프레임 수, 잠금 완료, 열화상 준비, PPG 안정 대기가 모두 끝난 RGB 프레임에서 시작합니다.
 *       PPG 안정 대기는 촬영 버튼 뒤 받은 RGB 프레임을 1부터 셉니다.
 * @note 열화상을 쓰고(FlowRule::prepare_thermal) PPG 안정 대기가 있으면 잠금(자동 맞춤)과 열화상 준비(직접 지정)는 촬영 버튼에서
 *       하지 않고, 대기가 셔터 교정 프레임 수(FlowRule::thermal_calibration_frames, 잠금 뒤 안정화 프레임 수보다 작으면
 *       그 수)만큼 남은 RGB 프레임에서 요청합니다. 둘 다 열화상 셔터 교정을 하므로 교정이 대기와 함께 끝나고, 교정이 끝난 뒤 녹화까지의
 *       시간은 대기가 없을 때와 같습니다. 셔터 교정이 이 프레임 수보다 오래 걸리면 녹화는 PPG 안정 대기보다 늦게 시작합니다(설계 문서 표 10).
 */
struct Event {
  EventKind kind = EventKind::kFrame;
  SettingKind setting = SettingKind::kExposure;  // kSettingChanged일 때만 씁니다
  StreamKind stream = StreamKind::kRgb;          // kFrame일 때만 씁니다
  // kFrame일 때만 씁니다. false이면 쓰기 버퍼가 없어 이 프레임을 파일에 쓸 수 없습니다(저장 누락).
  // 목표 프레임 수는 파일에 쓴 프레임만 세므로(표 2) 쓸 수 없는 프레임은 세지 않습니다
  bool storable = true;
  // kStartPressed일 때만 씁니다. 촬영 버튼 뒤 PPG 안정 대기로 녹화를 미루는 RGB 프레임 수이고, 0이면 기다리지 않습니다
  int32_t ppg_settle_frames = 0;
};

/** 녹화가 끝난 이유입니다. 사이드카와 화면에 남깁니다. */
enum class EndReason : uint8_t {
  kCompleted,       // 목표 프레임 수를 채움
  kStopped,         // 중지 버튼
  kSettingChanged,  // 녹화 중 설정 변경. 앞뒤 프레임의 조건이 달라 녹화를 끝냅니다
  kDisconnected,
};

/** 촬영 흐름이 platform에 시키는 일의 종류입니다. */
enum class ActionKind : uint8_t {
  kLockSettings,     // 자동 맞춤: 펌웨어 자동 노출을 잠금
  kBeginRecording,   // 파일을 엶. 같은 Advance의 kWriteFrame보다 먼저 옵니다
  kWriteFrame,       // 이번 이벤트의 프레임을 그 스트림의 파일에 씀
  kDropFrame,        // 이번 이벤트의 프레임을 쓰지 않고 저장 누락으로 셈(storable이 false)
  kEndRecording,     // 파일을 닫고 사이드카와 누락 검사를 함
  // 미리보기 목표로 설정값을 모두 다시 설정하고 다시 읽음. 열화상 자동 교정 멈춤도 되돌립니다.
  // 미리보기로 돌아가는 모든 경로(녹화 끝, 안정화 중 중지, 재연결)에서 나옵니다
  kReapplySettings,
  kPrepareThermal,   // 직접 지정: RGB 설정은 그대로 두고 열화상 셔터 교정과 자동 교정 멈춤만 함
};

/** platform이 실행할 일 하나입니다. */
struct Action {
  ActionKind kind = ActionKind::kWriteFrame;
  EndReason reason = EndReason::kCompleted;  // kEndRecording일 때만 씁니다
};

/** 촬영 흐름의 규칙입니다. 촬영 선언에서 꺼냅니다(RuleOf). */
struct FlowRule {
  timing::SettleRule settle;
  int64_t target_frames = 0;
  SettingMode mode = SettingMode::kAuto;
  // 녹화 직전 열화상 준비(셔터 교정, 자동 교정 멈춤)를 기다림. 열화상 카메라를 쓰면 platform이 true로 정합니다.
  // 자동 맞춤은 잠금 요청에서 함께 하므로 직접 지정에만 영향이 있습니다.
  bool prepare_thermal = false;
  // 열화상 셔터 교정에 걸리는 RGB 프레임 수. 열화상을 쓰면 PPG 안정 대기가 이만큼(잠금 뒤 안정화 프레임 수보다 작으면 그 수)
  // 남을 때 잠금과 열화상 준비를 요청해 교정이 대기와 함께 끝나게 합니다. platform이 ThermalCamera::CalibrationUs로 정합니다
  int32_t thermal_calibration_frames = 0;
};

/** 촬영 흐름의 상태입니다. 값으로 주고받으며 함수 밖에 상태를 두지 않습니다. */
struct FlowState {
  Phase phase = Phase::kPreview;
  timing::SettleState settle;
  int64_t written_frames = 0;  // 파일에 쓴 RGB 프레임 수
  // 잠금을 요청하고 아직 끝났다는 사건을 받지 못함. 잠금은 다른 스레드에서 실행되어 안정화 프레임 수보다 늦게 끝날 수 있습니다.
  bool awaiting_lock = false;
  // 직접 지정에서 열화상 준비를 요청하고 아직 끝났다는 사건을 받지 못함
  bool awaiting_thermal = false;
  // 마지막 LED 변경이 영상에 반영되기까지 남은 RGB 프레임 수(SettleRule::light_applied_frame에서 셈)
  int32_t frames_until_light_applied = 0;
  // 자동 맞춤에서 촬영 버튼을 눌렀지만 LED 변경이 아직 반영되지 않았거나, 열화상을 쓰는데 PPG 안정 대기가 많이 남아 잠금 요청을 미룸
  bool lock_deferred = false;
  // 직접 지정에서 촬영 버튼을 눌렀지만 PPG 안정 대기가 많이 남아 열화상 준비 요청을 미룸
  bool thermal_deferred = false;
  // PPG 안정 대기가 끝나기까지 남은 RGB 프레임 수(촬영 버튼의 Event::ppg_settle_frames에서 셈)
  int32_t frames_until_ppg_settled = 0;
};

/** Advance의 결과입니다. 다음 상태와 platform이 할 일입니다. */
struct Step {
  FlowState state;
  std::vector<Action> actions;  // platform이 순서대로 실행합니다
};

/**
 * 사건 하나를 받아 다음 상태와 할 일을 정합니다. 촬영 흐름의 판단은 모두 이 함수에서 합니다.
 *
 * 같은 상태와 사건에는 항상 같은 결과를 내고, 스레드나 시계나 파일을 쓰지 않습니다.
 *
 * @param state 지금 상태
 * @param event 받은 사건
 * @param rule 촬영 흐름 규칙
 * @return 다음 상태와 할 일 목록. 할 일은 platform이 순서대로 실행합니다
 * @note platform은 사건을 한 스레드(수신 스레드)에서 차례로 넣어야 합니다.
 */
Step Advance(FlowState state, const Event& event, const FlowRule& rule);

/**
 * 지금 상태에서 LED 밝기를 바꿔도 되는지 정합니다.
 *
 * @param phase 지금 촬영 상태
 * @return 미리보기에서만 true
 * @note 안정화 중에 바꾸면 자동 맞춤이 이미 잠근 노출이 새 LED 밝기와 맞지 않고, 녹화 중에 바꾸면 앞뒤 프레임의 조건이 달라집니다.
 *       연결 끊김 중에는 다시 열린 뒤 안정화를 셀 기준 프레임이 없습니다. 그래서 LED 변경은 미리보기에서만 받고,
 *       자동 맞춤은 LED 변경이 반영된 뒤에 잠급니다(SettleRule::light_applied_frame).
 */
constexpr bool AllowsLightChange(Phase phase) { return phase == Phase::kPreview; }

}  // namespace hwlab::flow

#endif  // HWLAB_FLOW_FLOW_H_
