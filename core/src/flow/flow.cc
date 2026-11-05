#include "hwlab/flow/flow.h"

#include <algorithm>

namespace hwlab::flow {

namespace {

Step Stay(const FlowState& state) { return {state, {}}; }

// 녹화를 끝내고 미리보기 목표로 되돌립니다. 자동 맞춤은 미리보기에서 잠금을 풀어야 다음 장면에 노출을 다시 맞춥니다.
// 직접 지정은 두 목표가 같아 실제로 바뀌는 설정이 없습니다.
std::vector<Action> EndActions(EndReason reason) {
  return {{ActionKind::kEndRecording, reason}, {ActionKind::kReapplySettings}};
}

Step End(FlowState state, Phase next, EndReason reason) {
  state.phase = next;
  if (next == Phase::kDisconnected) {
    // 연결이 끊긴 카메라에는 설정할 수 없습니다. 재연결 때 다시 설정합니다.
    return {state, {{ActionKind::kEndRecording, reason}}};
  }
  return {state, EndActions(reason)};
}

// 잠금은 노출과 게인을 함께 고정하므로 두 설정 변경으로 다룹니다.
timing::SettleState AfterLock(timing::SettleState settle, const timing::SettleRule& rule) {
  settle = timing::AfterSettingChange(settle, SettingKind::kExposure, rule);
  return timing::AfterSettingChange(settle, SettingKind::kGain, rule);
}

// 미룬 동안 앞 촬영의 늦은 완료 사건이 기다림을 풀었을 수 있어, 요청을 보낼 때 다시 기다립니다.
Step RequestLock(FlowState state, const FlowRule& rule) {
  state.settle = AfterLock(state.settle, rule.settle);
  state.lock_deferred = false;
  state.awaiting_lock = true;
  return {state, {{ActionKind::kLockSettings}}};
}

Step RequestThermal(FlowState state) {
  state.thermal_deferred = false;
  state.awaiting_thermal = true;
  return {state, {{ActionKind::kPrepareThermal}}};
}

// 잠금과 열화상 준비는 열화상 셔터 교정을 함께 하므로, 녹화보다 일찍 하면 교정 뒤 녹화까지의 시간이 길어집니다.
// 그래서 열화상을 쓰면 PPG 안정 대기가 교정 프레임 수(잠금 뒤 안정화 프레임 수보다 작으면 그 수)만큼 남을 때까지 미룹니다.
bool IsThermalCalibrationDue(const FlowState& state, const FlowRule& rule) {
  const int32_t lead = std::max(AfterLock({}, rule.settle).frames_until_settled, rule.thermal_calibration_frames);
  return !rule.prepare_thermal || state.frames_until_ppg_settled <= lead;
}

// 안정화에서 미리보기나 연결 끊김으로 나갈 때 이번 촬영의 기다림을 모두 버립니다.
FlowState ClearSettlingWaits(FlowState state) {
  state.awaiting_lock = false;
  state.awaiting_thermal = false;
  state.lock_deferred = false;
  state.thermal_deferred = false;
  state.frames_until_ppg_settled = 0;
  return state;
}

Step OnPreview(FlowState state, const Event& event, const FlowRule& rule) {
  switch (event.kind) {
    case EventKind::kFrame:
      state.settle = timing::AfterFrame(state.settle);
      return Stay(state);
    case EventKind::kSettingChanged:
      state.settle = timing::AfterSettingChange(state.settle, event.setting, rule.settle);
      return Stay(state);
    case EventKind::kStartPressed: {
      state.phase = Phase::kSettling;
      state.written_frames = 0;
      state.frames_until_ppg_settled = std::max(event.ppg_settle_frames, 0);
      if (rule.mode == SettingMode::kAuto) {
        state.awaiting_lock = true;
        // 펌웨어 자동 노출이 새 LED 밝기를 본 뒤에 잠가야 노출이 녹화 조명에 맞습니다.
        if (state.frames_until_light_applied > 0 || !IsThermalCalibrationDue(state, rule)) {
          state.lock_deferred = true;
          return Stay(state);
        }
        return RequestLock(state, rule);
      }
      // 직접 지정은 RGB 설정을 바꾸지 않으므로 안정화를 다시 세지 않고 열화상 준비만 기다립니다.
      if (rule.prepare_thermal) {
        state.awaiting_thermal = true;
        if (!IsThermalCalibrationDue(state, rule)) {
          state.thermal_deferred = true;
          return Stay(state);
        }
        return RequestThermal(state);
      }
      return Stay(state);
    }
    case EventKind::kStopPressed:
    case EventKind::kReconnected:
    case EventKind::kLockApplied:
    case EventKind::kThermalPrepared:
      return Stay(state);
    case EventKind::kDisconnected:
      state.phase = Phase::kDisconnected;
      return Stay(state);
  }
  return Stay(state);
}

Step OnSettling(FlowState state, const Event& event, const FlowRule& rule) {
  switch (event.kind) {
    case EventKind::kFrame: {
      state.settle = timing::AfterFrame(state.settle);
      if (state.lock_deferred && state.frames_until_light_applied <= 0 && IsThermalCalibrationDue(state, rule)) {
        return RequestLock(state, rule);
      }
      if (state.thermal_deferred && IsThermalCalibrationDue(state, rule)) return RequestThermal(state);
      // 잠금이 끝났다는 사건을 받기 전에는 프레임 수가 차도 녹화하지 않습니다.
      if (!timing::IsSettled(state.settle) || state.awaiting_lock || state.awaiting_thermal ||
          state.frames_until_ppg_settled > 0) {
        return Stay(state);
      }
      // 안정화가 끝난 프레임부터 씁니다.
      state.phase = Phase::kRecording;
      state.written_frames = event.storable ? 1 : 0;
      Step step{state, {{ActionKind::kBeginRecording},
                        {event.storable ? ActionKind::kWriteFrame : ActionKind::kDropFrame}}};
      if (state.written_frames >= rule.target_frames) {
        step.state.phase = Phase::kPreview;
        for (const Action& a : EndActions(EndReason::kCompleted)) step.actions.push_back(a);
      }
      return step;
    }
    case EventKind::kSettingChanged:
      state.settle = timing::AfterSettingChange(state.settle, event.setting, rule.settle);
      return Stay(state);
    case EventKind::kStopPressed:
      // 아직 파일을 열지 않았으므로 닫을 것은 없고, 잠금과 열화상 준비(자동 교정 멈춤)는 녹화 끝과 같게 되돌립니다.
      // 잠금이나 준비가 아직 실행 전이어도 platform은 요청을 받은 순서로 실행하므로 되돌림이 그 뒤에 옵니다.
      state = ClearSettlingWaits(state);
      state.phase = Phase::kPreview;
      return {state, {{ActionKind::kReapplySettings}}};
    case EventKind::kDisconnected:
      state = ClearSettlingWaits(state);
      state.phase = Phase::kDisconnected;
      return Stay(state);
    case EventKind::kLockApplied:
      state.awaiting_lock = false;
      return Stay(state);
    case EventKind::kThermalPrepared:
      state.awaiting_thermal = false;
      return Stay(state);
    case EventKind::kStartPressed:
    case EventKind::kReconnected:
      return Stay(state);
  }
  return Stay(state);
}

Step OnRecording(FlowState state, const Event& event, const FlowRule& rule) {
  switch (event.kind) {
    case EventKind::kFrame: {
      if (!event.storable) return {state, {{ActionKind::kDropFrame}}};
      ++state.written_frames;
      Step step{state, {{ActionKind::kWriteFrame}}};
      if (state.written_frames >= rule.target_frames) {
        step.state.phase = Phase::kPreview;
        for (const Action& a : EndActions(EndReason::kCompleted)) step.actions.push_back(a);
      }
      return step;
    }
    case EventKind::kSettingChanged:
      state.settle = timing::AfterSettingChange(state.settle, event.setting, rule.settle);
      return End(state, Phase::kPreview, EndReason::kSettingChanged);
    case EventKind::kStopPressed:
      return End(state, Phase::kPreview, EndReason::kStopped);
    case EventKind::kDisconnected:
      return End(state, Phase::kDisconnected, EndReason::kDisconnected);
    case EventKind::kStartPressed:
    case EventKind::kReconnected:
    case EventKind::kLockApplied:
    case EventKind::kThermalPrepared:
      return Stay(state);
  }
  return Stay(state);
}

Step OnDisconnected(FlowState state, const Event& event) {
  if (event.kind != EventKind::kReconnected) {
    return Stay(state);
  }
  // 재연결 뒤의 설정값은 platform이 다시 설정하며 kSettingChanged로 알려 줍니다.
  state.phase = Phase::kPreview;
  return {state, {{ActionKind::kReapplySettings}}};
}

}  // namespace

Step Advance(FlowState state, const Event& event, const FlowRule& rule) {
  if (event.kind == EventKind::kFrame && event.stream != StreamKind::kRgb) {
    // 보조 스트림은 RGB 녹화 구간만 저장합니다. 설정 변경은 RGB 카메라에만 하므로 안정화 프레임 수에 넣지 않습니다.
    if (state.phase == Phase::kRecording) return {state, {{ActionKind::kWriteFrame}}};
    return Stay(state);
  }
  if (event.kind == EventKind::kFrame && state.frames_until_light_applied > 0) --state.frames_until_light_applied;
  if (event.kind == EventKind::kFrame && state.frames_until_ppg_settled > 0) --state.frames_until_ppg_settled;
  if (event.kind == EventKind::kSettingChanged && event.setting == SettingKind::kLight) {
    state.frames_until_light_applied = std::max(state.frames_until_light_applied, rule.settle.light_applied_frame);
  }
  switch (state.phase) {
    case Phase::kPreview:
      return OnPreview(state, event, rule);
    case Phase::kSettling:
      return OnSettling(state, event, rule);
    case Phase::kRecording:
      return OnRecording(state, event, rule);
    case Phase::kDisconnected:
      return OnDisconnected(state, event);
  }
  return Stay(state);
}

}  // namespace hwlab::flow
