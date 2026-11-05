#include <gtest/gtest.h>

#include <vector>

#include "hwlab/flow/flow.h"

namespace {

using hwlab::SettingKind;
using hwlab::SettingMode;
using hwlab::StreamKind;
using hwlab::flow::Action;
using hwlab::flow::ActionKind;
using hwlab::flow::Advance;
using hwlab::flow::EndReason;
using hwlab::flow::Event;
using hwlab::flow::EventKind;
using hwlab::flow::FlowRule;
using hwlab::flow::FlowState;
using hwlab::flow::Phase;

// See3CAM 실측값(설계 문서 표 5)과 목표 900장입니다.
FlowRule MakeRule(SettingMode mode) {
  return {{.exposure_applied_frame = 4,
           .gain_applied_frame = 3,
           .white_balance_applied_frame = 3,
           .black_level_settled_frame = 75},
          900,
          mode};
}

const Event kFrame{EventKind::kFrame};
const Event kStart{EventKind::kStartPressed};
const Event kStop{EventKind::kStopPressed};
const Event kThermalFrame{EventKind::kFrame, SettingKind::kExposure, StreamKind::kThermal};

// PPG 안정 대기 프레임 수를 실은 촬영 버튼 이벤트입니다.
Event StartWithPpgSettle(int32_t frames) {
  Event start{EventKind::kStartPressed};
  start.ppg_settle_frames = frames;
  return start;
}

// 이벤트를 차례로 넣으며 상태와 모든 동작을 모읍니다. 동작마다 몇 번째 이벤트에서 나왔는지 남깁니다.
struct EventLog {
  FlowState state;
  std::vector<std::pair<int, Action>> actions;
  int events = 0;

  void Feed(const Event& event, const FlowRule& rule, int times = 1) {
    for (int i = 0; i < times; ++i) {
      auto step = Advance(state, event, rule);
      state = step.state;
      for (const Action& a : step.actions) {
        actions.push_back({events, a});
      }
      ++events;
    }
  }
  int Count(ActionKind kind) const {
    int n = 0;
    for (const auto& [at, a] : actions) {
      n += a.kind == kind ? 1 : 0;
    }
    return n;
  }
  // 마지막 녹화 끝 동작입니다.
  Action LastEnd() const {
    for (auto it = actions.rbegin(); it != actions.rend(); ++it) {
      if (it->second.kind == ActionKind::kEndRecording) return it->second;
    }
    return {};
  }
  int FirstAt(ActionKind kind) const {
    for (const auto& [at, a] : actions) {
      if (a.kind == kind) {
        return at;
      }
    }
    return -1;
  }
};

// 직접 지정에서 설정을 바꾼 뒤 75프레임 지나 촬영 버튼을 누르면, 다음 프레임부터 900장을 쓰고 끝납니다.
TEST(Flow, ManualRecordsNineHundredFramesAfterSettle) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  EventLog run;
  run.Feed({EventKind::kSettingChanged, SettingKind::kExposure}, rule);
  run.Feed(kFrame, rule, 100);
  run.Feed(kStart, rule);

  run.Feed(kFrame, rule, 905);

  EXPECT_EQ(run.Count(ActionKind::kBeginRecording), 1);
  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 102);
  EXPECT_EQ(run.Count(ActionKind::kWriteFrame), 900);
  ASSERT_EQ(run.Count(ActionKind::kEndRecording), 1);
  EXPECT_EQ(run.LastEnd().reason, EndReason::kCompleted);
  // 끝난 뒤 미리보기 목표로 되돌리는 동작이 이어집니다.
  EXPECT_EQ(run.actions.back().second.kind, ActionKind::kReapplySettings);
  EXPECT_EQ(run.state.phase, Phase::kPreview);
  EXPECT_EQ(run.state.written_frames, 900);
}

// 쓰기 버퍼가 없어 저장하지 못한 프레임은 목표 프레임 수에 넣지 않습니다(표 2: 파일에 쓴 프레임만 셈).
// 녹화 중 3장을 저장하지 못하면 3장 더 받아 파일에 900장을 채웁니다.
TEST(Flow, UnstoredFramesDoNotCountTowardTarget) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  const Event unstored{EventKind::kFrame, SettingKind::kExposure, StreamKind::kRgb, false};
  EventLog run;
  run.Feed(kFrame, rule, 100);
  run.Feed(kStart, rule);
  run.Feed(kFrame, rule, 10);
  run.Feed(unstored, rule, 3);
  run.Feed(kFrame, rule, 889);
  EXPECT_EQ(run.Count(ActionKind::kEndRecording), 0);
  EXPECT_EQ(run.state.written_frames, 899);

  run.Feed(kFrame, rule);

  EXPECT_EQ(run.Count(ActionKind::kWriteFrame), 900);
  EXPECT_EQ(run.Count(ActionKind::kDropFrame), 3);
  ASSERT_EQ(run.Count(ActionKind::kEndRecording), 1);
  EXPECT_EQ(run.LastEnd().reason, EndReason::kCompleted);
  EXPECT_EQ(run.state.written_frames, 900);
}

// 안정화가 끝난 첫 프레임을 저장하지 못해도 녹화는 시작하고, 그 프레임은 세지 않습니다.
TEST(Flow, UnstoredFirstFrameStartsRecordingWithoutCounting) {
  FlowRule rule = MakeRule(SettingMode::kManual);
  rule.target_frames = 2;
  const Event unstored{EventKind::kFrame, SettingKind::kExposure, StreamKind::kRgb, false};
  EventLog run;
  run.Feed(kFrame, rule, 100);
  run.Feed(kStart, rule);

  run.Feed(unstored, rule);
  EXPECT_EQ(run.state.phase, Phase::kRecording);
  EXPECT_EQ(run.state.written_frames, 0);
  EXPECT_EQ(run.Count(ActionKind::kBeginRecording), 1);
  EXPECT_EQ(run.Count(ActionKind::kDropFrame), 1);
  EXPECT_EQ(run.Count(ActionKind::kWriteFrame), 0);

  run.Feed(kFrame, rule, 2);
  EXPECT_EQ(run.Count(ActionKind::kWriteFrame), 2);
  EXPECT_EQ(run.Count(ActionKind::kEndRecording), 1);
}

// 미리보기 중에는 저장 여부와 관계없이 쓰기도 누락도 지시하지 않습니다.
TEST(Flow, UnstoredFrameInPreviewDoesNothing) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  const Event unstored{EventKind::kFrame, SettingKind::kExposure, StreamKind::kRgb, false};
  EventLog run;
  run.Feed(unstored, rule, 5);
  EXPECT_TRUE(run.actions.empty());
}

// 설정을 바꾸자마자 촬영 버튼을 누르면 바꾼 뒤 75번째 프레임부터 씁니다.
TEST(Flow, ManualWaitsForSettleAfterLastChange) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  EventLog run;
  run.Feed({EventKind::kSettingChanged, SettingKind::kGain}, rule);
  run.Feed(kStart, rule);

  run.Feed(kFrame, rule, 80);

  // 이벤트 0: 설정 변경, 1: 촬영 버튼, 2부터 프레임. 75번째 프레임은 이벤트 76입니다.
  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 76);
}

// 열화상을 쓰는 직접 지정은 촬영 버튼에서 열화상 준비를 요청하고, 준비가 끝났다는 사건 전에는 녹화하지 않습니다.
TEST(Flow, ManualWithThermalWaitsForThermalPrepared) {
  FlowRule rule = MakeRule(SettingMode::kManual);
  rule.prepare_thermal = true;
  EventLog run;
  run.Feed(kFrame, rule, 100);
  run.Feed(kStart, rule);  // 이벤트 100
  run.Feed(kFrame, rule, 50);
  EXPECT_EQ(run.Count(ActionKind::kBeginRecording), 0);

  run.Feed({EventKind::kThermalPrepared}, rule);  // 이벤트 151
  run.Feed(kFrame, rule, 1);

  EXPECT_EQ(run.FirstAt(ActionKind::kPrepareThermal), 100);
  EXPECT_EQ(run.Count(ActionKind::kPrepareThermal), 1);
  // 직접 지정의 설정 동작은 그대로입니다: 잠금도, 안정화 재시작도 없습니다.
  EXPECT_EQ(run.Count(ActionKind::kLockSettings), 0);
  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 152);
}

// 열화상을 쓰는 직접 지정에서도 안정화 프레임 수는 마지막 설정 변경부터 셉니다.
TEST(Flow, ManualWithThermalStillWaitsForSettle) {
  FlowRule rule = MakeRule(SettingMode::kManual);
  rule.prepare_thermal = true;
  EventLog run;
  run.Feed({EventKind::kSettingChanged, SettingKind::kGain}, rule);
  run.Feed(kStart, rule);
  run.Feed({EventKind::kThermalPrepared}, rule);  // 이벤트 2

  run.Feed(kFrame, rule, 80);

  // 이벤트 0: 설정 변경, 1: 촬영 버튼, 2: 준비 끝, 3부터 프레임. 75번째 프레임은 이벤트 77입니다.
  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 77);
}

// 열화상 준비 중에 중지하면 미리보기로 돌아가고, 다음 촬영 버튼에서 준비를 다시 요청합니다.
TEST(Flow, ManualStopDuringThermalPrepareClearsWait) {
  FlowRule rule = MakeRule(SettingMode::kManual);
  rule.prepare_thermal = true;
  EventLog run;
  run.Feed(kFrame, rule, 100);
  run.Feed(kStart, rule);
  run.Feed(kStop, rule);
  EXPECT_EQ(run.state.phase, Phase::kPreview);
  EXPECT_FALSE(run.state.awaiting_thermal);

  run.Feed(kStart, rule);
  EXPECT_EQ(run.Count(ActionKind::kPrepareThermal), 2);
}

// 자동 맞춤은 잠금 요청에서 열화상도 준비하므로 열화상 준비를 따로 요청하지 않습니다.
TEST(Flow, AutoWithThermalUsesLockOnly) {
  FlowRule rule = MakeRule(SettingMode::kAuto);
  rule.prepare_thermal = true;
  EventLog run;
  run.Feed(kFrame, rule, 10);
  run.Feed(kStart, rule);
  run.Feed({EventKind::kLockApplied}, rule);
  run.Feed(kFrame, rule, 80);

  EXPECT_EQ(run.Count(ActionKind::kPrepareThermal), 0);
  EXPECT_EQ(run.Count(ActionKind::kLockSettings), 1);
  EXPECT_EQ(run.Count(ActionKind::kBeginRecording), 1);
}

// 자동 맞춤에서는 촬영 버튼에서 잠금을 요청하고, 잠금 뒤 75번째 프레임부터 씁니다.
TEST(Flow, AutoLocksThenWaitsForSettle) {
  const FlowRule rule = MakeRule(SettingMode::kAuto);
  EventLog run;
  run.Feed(kFrame, rule, 200);
  run.Feed(kStart, rule);
  run.Feed({EventKind::kLockApplied}, rule);  // 이벤트 201

  run.Feed(kFrame, rule, 80);

  EXPECT_EQ(run.FirstAt(ActionKind::kLockSettings), 200);
  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 201 + 75);
}

// 잠금이 끝났다는 사건이 오기 전에는 안정화 프레임 수가 지나도 녹화하지 않습니다.
TEST(Flow, AutoWaitsForLockAppliedEvent) {
  const FlowRule rule = MakeRule(SettingMode::kAuto);
  EventLog run;
  run.Feed(kStart, rule);
  run.Feed(kFrame, rule, 200);
  EXPECT_EQ(run.Count(ActionKind::kBeginRecording), 0);

  run.Feed({EventKind::kLockApplied}, rule);  // 이벤트 201
  run.Feed(kFrame, rule, 1);

  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 202);
}

// 안정화 중에 설정을 바꾸면 그 시점부터 75프레임을 다시 기다립니다.
TEST(Flow, ChangeDuringSettlingRestartsWait) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  EventLog run;
  run.Feed({EventKind::kSettingChanged, SettingKind::kExposure}, rule);
  run.Feed(kStart, rule);
  run.Feed(kFrame, rule, 50);
  run.Feed({EventKind::kSettingChanged, SettingKind::kExposure}, rule);  // 이벤트 52

  run.Feed(kFrame, rule, 80);

  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 52 + 75);
}

// 안정화 중 중지는 파일을 열기 전이므로 닫는 동작 없이 미리보기로 돌아가고, 잠금은 녹화 끝과 같게 되돌립니다.
TEST(Flow, StopDuringSettlingCancelsWithoutFile) {
  const FlowRule rule = MakeRule(SettingMode::kAuto);
  EventLog run;
  run.Feed(kStart, rule);
  run.Feed(kFrame, rule, 10);

  run.Feed(kStop, rule);  // 이벤트 11
  run.Feed(kFrame, rule, 100);

  EXPECT_EQ(run.state.phase, Phase::kPreview);
  EXPECT_EQ(run.Count(ActionKind::kBeginRecording), 0);
  EXPECT_EQ(run.Count(ActionKind::kEndRecording), 0);
  EXPECT_EQ(run.Count(ActionKind::kLockSettings), 1);
  EXPECT_EQ(run.Count(ActionKind::kReapplySettings), 1);
  EXPECT_EQ(run.FirstAt(ActionKind::kReapplySettings), 11);
}

// 직접 지정에서 열화상 준비를 기다리는 중에 중지해도 준비(자동 교정 멈춤)를 되돌리는 동작이 나옵니다.
TEST(Flow, StopWhilePreparingThermalReappliesSettings) {
  FlowRule rule = MakeRule(SettingMode::kManual);
  rule.prepare_thermal = true;
  EventLog run;
  run.Feed(kStart, rule);
  run.Feed(kFrame, rule, 5);

  run.Feed(kStop, rule);  // 이벤트 6
  run.Feed({EventKind::kThermalPrepared}, rule);
  run.Feed(kFrame, rule, 100);

  EXPECT_EQ(run.state.phase, Phase::kPreview);
  EXPECT_EQ(run.Count(ActionKind::kPrepareThermal), 1);
  EXPECT_EQ(run.Count(ActionKind::kBeginRecording), 0);
  EXPECT_EQ(run.Count(ActionKind::kReapplySettings), 1);
  EXPECT_EQ(run.FirstAt(ActionKind::kReapplySettings), 6);
}

// 잠금이나 열화상 준비를 요청한 뒤 미리보기로 돌아가는 모든 경로(완료, 녹화 중 중지, 안정화 중 중지, 끊김 뒤 재연결)에서
// 그 요청보다 뒤에 되돌림이 나옵니다. platform은 이 순서로 자동 교정 멈춤을 겁니다.
TEST(Flow, EveryReturnToPreviewAfterHoldReappliesSettings) {
  const Event disconnected{EventKind::kDisconnected};
  const Event reconnected{EventKind::kReconnected};
  const Event locked{EventKind::kLockApplied};
  struct Path {
    SettingMode mode;
    bool recorded;  // 파일을 열었다가 닫는 경로
    std::vector<std::pair<Event, int>> after_start;
  };
  const std::vector<Path> paths = {
      {SettingMode::kAuto, true, {{locked, 1}, {kFrame, 1000}}},
      {SettingMode::kAuto, true, {{locked, 1}, {kFrame, 200}, {kStop, 1}}},
      {SettingMode::kAuto, false, {{kFrame, 10}, {kStop, 1}}},
      {SettingMode::kAuto, false, {{kFrame, 10}, {disconnected, 1}, {reconnected, 1}}},
      {SettingMode::kManual, false, {{kStop, 1}}},
      {SettingMode::kManual, true, {{{EventKind::kThermalPrepared}, 1}, {kFrame, 300}, {kStop, 1}}},
  };
  for (size_t i = 0; i < paths.size(); ++i) {
    FlowRule rule = MakeRule(paths[i].mode);
    rule.prepare_thermal = true;
    EventLog run;
    run.Feed(kStart, rule);
    for (const auto& [event, times] : paths[i].after_start) run.Feed(event, rule, times);
    run.Feed(kFrame, rule, 5);
    SCOPED_TRACE(i);
    EXPECT_EQ(run.state.phase, Phase::kPreview);
    EXPECT_EQ(run.Count(ActionKind::kEndRecording), paths[i].recorded ? 1 : 0);
    ASSERT_EQ(run.Count(ActionKind::kLockSettings) + run.Count(ActionKind::kPrepareThermal), 1);
    int hold_index = -1;
    int last_reapply = -1;
    for (size_t k = 0; k < run.actions.size(); ++k) {
      const ActionKind kind = run.actions[k].second.kind;
      if (kind == ActionKind::kLockSettings || kind == ActionKind::kPrepareThermal) hold_index = static_cast<int>(k);
      if (kind == ActionKind::kReapplySettings) last_reapply = static_cast<int>(k);
    }
    EXPECT_GT(last_reapply, hold_index);
  }
}

// 녹화 중 중지는 그때까지 쓴 장 수를 남기고 끝냅니다.
TEST(Flow, StopDuringRecordingEnds) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  EventLog run;
  run.Feed(kStart, rule);
  run.Feed(kFrame, rule, 300);

  run.Feed(kStop, rule);

  ASSERT_EQ(run.Count(ActionKind::kEndRecording), 1);
  EXPECT_EQ(run.LastEnd().reason, EndReason::kStopped);
  EXPECT_EQ(run.state.written_frames, 300);
  EXPECT_EQ(run.state.phase, Phase::kPreview);
}

// 녹화 중 설정이 바뀌면 앞뒤 프레임의 조건이 달라지므로 녹화를 끝냅니다.
TEST(Flow, SettingChangeDuringRecordingEnds) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  EventLog run;
  run.Feed(kStart, rule);
  run.Feed(kFrame, rule, 10);

  run.Feed({EventKind::kSettingChanged, SettingKind::kGain}, rule);

  EXPECT_EQ(run.LastEnd().kind, ActionKind::kEndRecording);
  EXPECT_EQ(run.LastEnd().reason, EndReason::kSettingChanged);
}

// 녹화 중 연결이 끊기면 녹화를 끝내고, 재연결 때 설정값을 다시 설정합니다.
TEST(Flow, DisconnectEndsAndReconnectReappliesSettings) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  EventLog run;
  run.Feed(kStart, rule);
  run.Feed(kFrame, rule, 10);

  run.Feed({EventKind::kDisconnected}, rule);
  run.Feed(kFrame, rule, 5);
  run.Feed(kStart, rule);
  EXPECT_EQ(run.state.phase, Phase::kDisconnected);
  run.Feed({EventKind::kReconnected}, rule);

  EXPECT_EQ(run.Count(ActionKind::kEndRecording), 1);
  EXPECT_EQ(run.Count(ActionKind::kWriteFrame), 10);
  EXPECT_EQ(run.actions.back().second.kind, ActionKind::kReapplySettings);
  EXPECT_EQ(run.state.phase, Phase::kPreview);
}

// 녹화 중 촬영 버튼을 다시 눌러도 아무 일도 일어나지 않습니다.
TEST(Flow, StartDuringRecordingIsIgnored) {
  const FlowRule rule = MakeRule(SettingMode::kAuto);
  EventLog run;
  run.Feed(kStart, rule);
  run.Feed({EventKind::kLockApplied}, rule);
  run.Feed(kFrame, rule, 100);
  const int before = static_cast<int>(run.actions.size());

  run.Feed(kStart, rule);

  EXPECT_EQ(static_cast<int>(run.actions.size()), before);
  EXPECT_EQ(run.state.phase, Phase::kRecording);
}

// RGB와 열화상을 번갈아 넣으면 열화상은 녹화 중에만 쓰고, 녹화는 RGB 900장에서 끝납니다.
TEST(Flow, AuxiliaryFramesWrittenOnlyWhileRecording) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  FlowState state;
  int rgb_writes = 0;
  int thermal_writes = 0;
  int thermal_writes_outside = 0;
  int ends = 0;
  for (int i = 0; i < 1100; ++i) {
    if (i == 10) state = Advance(state, kStart, rule).state;
    for (const Event& event : {kFrame, kThermalFrame}) {
      const bool recording_before = state.phase == Phase::kRecording;
      const auto step = Advance(state, event, rule);
      state = step.state;
      for (const Action& a : step.actions) {
        if (a.kind == ActionKind::kEndRecording) ++ends;
        if (a.kind != ActionKind::kWriteFrame) continue;
        if (event.stream == StreamKind::kRgb) {
          ++rgb_writes;
        } else {
          ++thermal_writes;
          thermal_writes_outside += recording_before ? 0 : 1;
        }
      }
    }
  }

  EXPECT_EQ(rgb_writes, 900);
  EXPECT_EQ(ends, 1);
  EXPECT_EQ(state.written_frames, 900);
  EXPECT_EQ(state.phase, Phase::kPreview);
  // 녹화를 연 RGB 프레임 뒤의 열화상부터 녹화를 끝낸 RGB 프레임 앞의 열화상까지 899장입니다.
  EXPECT_EQ(thermal_writes, 899);
  EXPECT_EQ(thermal_writes_outside, 0);
}

// 열화상 프레임은 안정화 프레임 수와 목표 프레임 수에 들어가지 않습니다.
TEST(Flow, AuxiliaryFramesDoNotAdvanceSettleOrCount) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  EventLog run;
  run.Feed({EventKind::kSettingChanged, SettingKind::kExposure}, rule);
  run.Feed(kStart, rule);
  run.Feed(kThermalFrame, rule, 500);
  EXPECT_EQ(run.state.phase, Phase::kSettling);
  EXPECT_TRUE(run.actions.empty());

  run.Feed(kFrame, rule, 75);
  EXPECT_EQ(run.state.phase, Phase::kRecording);
  run.Feed(kThermalFrame, rule, 2000);

  EXPECT_EQ(run.state.written_frames, 1);
  EXPECT_EQ(run.state.phase, Phase::kRecording);
  EXPECT_EQ(run.Count(ActionKind::kWriteFrame), 1 + 2000);
  EXPECT_EQ(run.Count(ActionKind::kEndRecording), 0);
}

const Event kLight{EventKind::kSettingChanged, SettingKind::kLight};

// LED 변경은 미리보기에서만 받습니다.
TEST(Flow, LightChangeAllowedOnlyInPreview) {
  EXPECT_TRUE(hwlab::flow::AllowsLightChange(Phase::kPreview));
  EXPECT_FALSE(hwlab::flow::AllowsLightChange(Phase::kSettling));
  EXPECT_FALSE(hwlab::flow::AllowsLightChange(Phase::kRecording));
  EXPECT_FALSE(hwlab::flow::AllowsLightChange(Phase::kDisconnected));
}

// 미리보기 중 LED를 바꾸고 바로 촬영 버튼을 누르면(직접 지정) LED 변경 뒤 75번째 프레임부터 씁니다.
TEST(Flow, LightChangeRestartsSettle) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  EventLog run;
  run.Feed(kFrame, rule, 100);
  run.Feed(kLight, rule);  // 이벤트 100
  run.Feed(kStart, rule);  // 이벤트 101

  run.Feed(kFrame, rule, 80);

  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 101 + 75);
}

// 안정화 중에 LED 변경 사건이 오면 그 시점부터 75프레임을 다시 기다립니다.
TEST(Flow, LightChangeDuringSettlingRestartsWait) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  EventLog run;
  run.Feed({EventKind::kSettingChanged, SettingKind::kGain}, rule);
  run.Feed(kStart, rule);
  run.Feed(kFrame, rule, 30);
  run.Feed(kLight, rule);  // 이벤트 32
  run.Feed(kFrame, rule, 80);

  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 32 + 75);
}

// 녹화 중 LED 변경 사건은 다른 설정 변경처럼 녹화를 끝냅니다.
TEST(Flow, LightChangeDuringRecordingEnds) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  EventLog run;
  run.Feed(kStart, rule);
  run.Feed(kFrame, rule, 10);
  ASSERT_EQ(run.state.phase, Phase::kRecording);

  run.Feed(kLight, rule);

  EXPECT_EQ(run.LastEnd().kind, ActionKind::kEndRecording);
  EXPECT_EQ(run.LastEnd().reason, EndReason::kSettingChanged);
  EXPECT_EQ(run.state.phase, Phase::kPreview);
}

// 자동 맞춤에서 LED 변경이 아직 반영되지 않았으면 반영 프레임까지 잠금을 미루고, 잠금 뒤 75프레임을 기다립니다.
TEST(Flow, AutoLocksAfterLightApplied) {
  FlowRule rule = MakeRule(SettingMode::kAuto);
  rule.settle.light_applied_frame = 5;
  EventLog run;
  run.Feed(kFrame, rule, 10);
  run.Feed(kLight, rule);  // 이벤트 10
  run.Feed(kStart, rule);  // 이벤트 11
  EXPECT_EQ(run.Count(ActionKind::kLockSettings), 0);
  EXPECT_EQ(run.state.phase, Phase::kSettling);

  run.Feed(kFrame, rule, 10);  // 이벤트 12~21. LED 변경 뒤 5번째 프레임은 이벤트 16
  ASSERT_EQ(run.Count(ActionKind::kLockSettings), 1);
  EXPECT_EQ(run.FirstAt(ActionKind::kLockSettings), 16);

  run.Feed({EventKind::kLockApplied}, rule);  // 이벤트 22
  run.Feed(kFrame, rule, 80);
  // 잠금은 이벤트 16의 프레임에서 셉니다. 그 뒤 75번째 프레임은 이벤트 16 + 75 + 1(잠금 완료 사건 하나)
  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 16 + 75 + 1);
}

// LED 반영 프레임이 이미 지났으면 자동 맞춤은 촬영 버튼에서 바로 잠급니다.
TEST(Flow, AutoLocksImmediatelyWhenLightAlreadyApplied) {
  FlowRule rule = MakeRule(SettingMode::kAuto);
  rule.settle.light_applied_frame = 5;
  EventLog run;
  run.Feed(kLight, rule);
  run.Feed(kFrame, rule, 5);
  run.Feed(kStart, rule);  // 이벤트 6

  EXPECT_EQ(run.FirstAt(ActionKind::kLockSettings), 6);
}

// 잠금을 미룬 채 중지하면 잠그지 않고 미리보기로 돌아갑니다.
TEST(Flow, StopWhileLockDeferredNeverLocks) {
  FlowRule rule = MakeRule(SettingMode::kAuto);
  rule.settle.light_applied_frame = 5;
  EventLog run;
  run.Feed(kLight, rule);
  run.Feed(kStart, rule);
  run.Feed(kFrame, rule, 2);
  run.Feed(kStop, rule);

  run.Feed(kFrame, rule, 20);

  EXPECT_EQ(run.Count(ActionKind::kLockSettings), 0);
  EXPECT_EQ(run.state.phase, Phase::kPreview);
  EXPECT_FALSE(run.state.lock_deferred);
}

// PPG 안정 대기가 있으면 촬영 버튼 뒤 그 프레임 수째 RGB 프레임에서 녹화를 시작합니다.
TEST(Flow, ManualWaitsForPpgSettle) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  EventLog run;
  run.Feed(StartWithPpgSettle(360), rule);  // 이벤트 0
  run.Feed(kFrame, rule, 359);
  EXPECT_EQ(run.state.phase, Phase::kSettling);
  EXPECT_EQ(run.state.frames_until_ppg_settled, 1);

  run.Feed(kFrame, rule);  // 이벤트 360

  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 360);
  EXPECT_EQ(run.state.phase, Phase::kRecording);
}

// 열화상을 쓰는 자동 맞춤은 PPG 안정 대기가 75프레임 남을 때 잠그고, 잠금 뒤 75프레임과 PPG 안정 대기가 같은 프레임에서 끝납니다.
TEST(Flow, AutoWithThermalDefersLockUntilPpgNearlySettled) {
  FlowRule rule = MakeRule(SettingMode::kAuto);
  rule.prepare_thermal = true;
  EventLog run;
  run.Feed(StartWithPpgSettle(360), rule);  // 이벤트 0
  run.Feed(kFrame, rule, 284);  // 이벤트 1~284
  EXPECT_EQ(run.Count(ActionKind::kLockSettings), 0);
  EXPECT_TRUE(run.state.lock_deferred);

  run.Feed(kFrame, rule);  // 이벤트 285. PPG 안정 대기가 75프레임 남음
  EXPECT_EQ(run.FirstAt(ActionKind::kLockSettings), 285);

  run.Feed({EventKind::kLockApplied}, rule);  // 이벤트 286
  run.Feed(kFrame, rule, 100);
  // 360번째 RGB 프레임은 잠금 완료 사건 하나 뒤라 이벤트 361입니다
  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 361);
}

// 직접 지정의 열화상 준비도 PPG 안정 대기가 75프레임 남을 때 요청합니다.
TEST(Flow, ManualDefersThermalPrepareUntilPpgNearlySettled) {
  FlowRule rule = MakeRule(SettingMode::kManual);
  rule.prepare_thermal = true;
  EventLog run;
  run.Feed(StartWithPpgSettle(360), rule);  // 이벤트 0
  run.Feed(kFrame, rule, 284);
  EXPECT_EQ(run.Count(ActionKind::kPrepareThermal), 0);
  EXPECT_TRUE(run.state.thermal_deferred);

  run.Feed(kFrame, rule);  // 이벤트 285
  EXPECT_EQ(run.FirstAt(ActionKind::kPrepareThermal), 285);

  run.Feed({EventKind::kThermalPrepared}, rule);  // 이벤트 286
  run.Feed(kFrame, rule, 100);
  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 361);
}

// 셔터 교정 프레임 수가 잠금 뒤 안정화 프레임 수보다 길면, 직접 지정의 열화상 준비는 PPG 안정 대기가 그만큼 남을 때 요청하고
// 교정이 그 안에 끝나면 PPG 안정 대기가 끝난 프레임에서 녹화합니다.
TEST(Flow, ManualPreparesThermalCalibrationFramesBeforePpgSettled) {
  FlowRule rule = MakeRule(SettingMode::kManual);
  rule.prepare_thermal = true;
  rule.thermal_calibration_frames = 147;
  EventLog run;
  run.Feed(StartWithPpgSettle(450), rule);  // 이벤트 0
  run.Feed(kFrame, rule, 302);              // 이벤트 1~302
  EXPECT_EQ(run.Count(ActionKind::kPrepareThermal), 0);

  run.Feed(kFrame, rule);  // 이벤트 303. PPG 안정 대기가 147프레임 남음
  EXPECT_EQ(run.FirstAt(ActionKind::kPrepareThermal), 303);

  run.Feed(kFrame, rule, 140);                    // 이벤트 304~443. 교정 중
  run.Feed({EventKind::kThermalPrepared}, rule);  // 이벤트 444
  run.Feed(kFrame, rule, 10);
  // 450번째 RGB 프레임은 준비 완료 사건 하나 뒤라 이벤트 451입니다
  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 451);
}

// 자동 맞춤의 잠금도 PPG 안정 대기가 셔터 교정 프레임 수만큼 남을 때 요청하고, 교정이 대기보다 늦게 끝나면 끝난 뒤 녹화합니다.
TEST(Flow, AutoLocksCalibrationFramesBeforePpgSettledAndWaitsForLock) {
  FlowRule rule = MakeRule(SettingMode::kAuto);
  rule.prepare_thermal = true;
  rule.thermal_calibration_frames = 147;
  EventLog run;
  run.Feed(StartWithPpgSettle(450), rule);  // 이벤트 0
  run.Feed(kFrame, rule, 303);              // 이벤트 1~303
  EXPECT_EQ(run.FirstAt(ActionKind::kLockSettings), 303);

  run.Feed(kFrame, rule, 160);  // 이벤트 304~463. 대기는 450번째 프레임에서 끝났지만 잠금(교정)이 끝나지 않음
  EXPECT_EQ(run.Count(ActionKind::kBeginRecording), 0);

  run.Feed({EventKind::kLockApplied}, rule);  // 이벤트 464
  run.Feed(kFrame, rule);                     // 이벤트 465
  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 465);
}

// 열화상 준비를 미룬 채 중지하면 준비하지 않고 미리보기로 돌아갑니다.
TEST(Flow, StopWhileThermalDeferredNeverPrepares) {
  FlowRule rule = MakeRule(SettingMode::kManual);
  rule.prepare_thermal = true;
  EventLog run;
  run.Feed(StartWithPpgSettle(360), rule);
  run.Feed(kFrame, rule, 10);
  run.Feed(kStop, rule);
  run.Feed(kFrame, rule, 400);

  EXPECT_EQ(run.Count(ActionKind::kPrepareThermal), 0);
  EXPECT_FALSE(run.state.thermal_deferred);
  EXPECT_EQ(run.state.phase, Phase::kPreview);
}

// 열화상 셔터 교정이 없으면 미룰 까닭이 없어 자동 맞춤은 촬영 버튼에서 잠그고, PPG 안정 대기가 끝난 프레임에서 녹화합니다.
TEST(Flow, AutoWithoutThermalLocksAtStartDuringPpgSettle) {
  const FlowRule rule = MakeRule(SettingMode::kAuto);
  EventLog run;
  run.Feed(StartWithPpgSettle(360), rule);  // 이벤트 0. 잠금 요청
  EXPECT_EQ(run.FirstAt(ActionKind::kLockSettings), 0);
  EXPECT_FALSE(run.state.lock_deferred);

  run.Feed({EventKind::kLockApplied}, rule);  // 이벤트 1
  run.Feed(kFrame, rule, 400);
  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 361);
}

// 미룬 동안 앞 촬영의 늦은 열화상 준비 완료 사건이 와도, 이번 촬영의 준비가 끝나기 전에는 녹화하지 않습니다.
TEST(Flow, StaleThermalPreparedDuringDeferralKeepsWaiting) {
  FlowRule rule = MakeRule(SettingMode::kManual);
  rule.prepare_thermal = true;
  EventLog run;
  run.Feed(StartWithPpgSettle(360), rule);  // 이벤트 0
  run.Feed({EventKind::kThermalPrepared}, rule);  // 이벤트 1. 앞 촬영의 준비 완료
  run.Feed(kFrame, rule, 400);  // 이벤트 2~401. 준비 요청은 285번째 프레임(이벤트 286)
  EXPECT_EQ(run.FirstAt(ActionKind::kPrepareThermal), 286);
  EXPECT_EQ(run.Count(ActionKind::kBeginRecording), 0);

  run.Feed({EventKind::kThermalPrepared}, rule);  // 이벤트 402
  run.Feed(kFrame, rule);  // 이벤트 403
  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 403);
}

// 같은 사건이 자동 맞춤의 잠금 완료로 와도 이번 촬영의 잠금이 끝나기 전에는 녹화하지 않습니다.
TEST(Flow, StaleLockAppliedDuringDeferralKeepsWaiting) {
  FlowRule rule = MakeRule(SettingMode::kAuto);
  rule.prepare_thermal = true;
  EventLog run;
  run.Feed(StartWithPpgSettle(360), rule);  // 이벤트 0
  run.Feed({EventKind::kLockApplied}, rule);  // 이벤트 1. 앞 촬영의 잠금 완료
  run.Feed(kFrame, rule, 400);
  EXPECT_EQ(run.FirstAt(ActionKind::kLockSettings), 286);
  EXPECT_EQ(run.Count(ActionKind::kBeginRecording), 0);
}

// PPG 안정 대기가 잠금 뒤 안정화 프레임 수보다 짧으면 열화상을 써도 촬영 버튼에서 바로 잠그고, 안정화가 끝나야 녹화합니다.
TEST(Flow, AutoLocksAtStartWhenPpgSettleIsShort) {
  FlowRule rule = MakeRule(SettingMode::kAuto);
  rule.prepare_thermal = true;
  const Event start_short = StartWithPpgSettle(10);

  EventLog settle_later;
  settle_later.Feed(start_short, rule);  // 이벤트 0. 잠금 요청
  EXPECT_EQ(settle_later.FirstAt(ActionKind::kLockSettings), 0);
  settle_later.Feed({EventKind::kLockApplied}, rule);
  settle_later.Feed(kFrame, rule, 100);
  EXPECT_EQ(settle_later.FirstAt(ActionKind::kBeginRecording), 1 + 75);
}

// PPG 안정 대기 중 중지하면 남은 대기를 버리고, 다음 촬영은 그 촬영의 대기만 따릅니다.
TEST(Flow, StopDuringPpgSettleClearsWait) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  EventLog run;
  run.Feed(StartWithPpgSettle(360), rule);
  run.Feed(kFrame, rule, 100);
  run.Feed(kStop, rule);
  EXPECT_EQ(run.state.phase, Phase::kPreview);
  EXPECT_EQ(run.state.frames_until_ppg_settled, 0);

  run.Feed(kStart, rule);  // 이벤트 102
  run.Feed(kFrame, rule);  // 이벤트 103

  EXPECT_EQ(run.FirstAt(ActionKind::kBeginRecording), 103);
}

// 연결이 끊기면 PPG 안정 대기를 버립니다. 다시 연결된 뒤에는 촬영 버튼을 다시 눌러야 녹화합니다.
TEST(Flow, DisconnectDuringPpgSettleClearsWait) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  EventLog run;
  run.Feed(StartWithPpgSettle(360), rule);
  run.Feed(kFrame, rule, 10);
  run.Feed({EventKind::kDisconnected}, rule);

  EXPECT_EQ(run.state.phase, Phase::kDisconnected);
  EXPECT_EQ(run.state.frames_until_ppg_settled, 0);
  EXPECT_EQ(run.Count(ActionKind::kBeginRecording), 0);
}

// 열화상 프레임은 PPG 안정 대기를 줄이지 않습니다. 대기는 RGB 프레임으로만 셉니다.
TEST(Flow, PpgSettleCountsRgbFramesOnly) {
  const FlowRule rule = MakeRule(SettingMode::kManual);
  EventLog run;
  run.Feed(StartWithPpgSettle(5), rule);
  run.Feed(kThermalFrame, rule, 20);

  EXPECT_EQ(run.state.frames_until_ppg_settled, 5);
  EXPECT_EQ(run.state.phase, Phase::kSettling);
}

}  // namespace
