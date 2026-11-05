#include "hwlab/timing/settle.h"

#include <algorithm>

namespace hwlab::timing {

namespace {

int32_t AppliedFrame(SettingKind kind, const SettleRule& rule) {
  switch (kind) {
    case SettingKind::kExposure:
      return rule.exposure_applied_frame;
    case SettingKind::kGain:
      return rule.gain_applied_frame;
    case SettingKind::kWhiteBalance:
      return rule.white_balance_applied_frame;
    case SettingKind::kLight:
      return rule.light_applied_frame;
  }
  return 0;
}

int32_t SettledFrame(SettingKind kind, const SettleRule& rule) {
  // 색온도와 LED 변경 뒤 블랙 레벨 변화는 측정하지 않아, 노출과 게인처럼 기다립니다.
  return std::max(AppliedFrame(kind, rule), rule.black_level_settled_frame);
}

}  // namespace

SettleState AfterSettingChange(SettleState state, SettingKind kind, const SettleRule& rule) {
  state.frames_until_applied = std::max(state.frames_until_applied, AppliedFrame(kind, rule));
  state.frames_until_settled = std::max(state.frames_until_settled, SettledFrame(kind, rule));
  return state;
}

SettleState AfterFrame(SettleState state) {
  state.frames_until_applied = std::max(state.frames_until_applied - 1, 0);
  state.frames_until_settled = std::max(state.frames_until_settled - 1, 0);
  return state;
}

}  // namespace hwlab::timing
