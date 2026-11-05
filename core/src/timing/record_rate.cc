#include "hwlab/timing/record_rate.h"

namespace hwlab::timing {

RecordRateStep AdvanceRecordRate(RecordRateState state, Timestamp time, int64_t median_interval_us,
                                 const RecordRateRule& rule) {
  if (rule.period_us <= 0) return {state, true};
  const bool slow_device =
      median_interval_us > 0 && median_interval_us * 100 >= rule.period_us * rule.thin_below_percent;
  if (slow_device || !state.started) {
    // 모두 기록하는 동안에도 다음 기록 시각을 옮겨 두어야, 장치가 빨라졌을 때 바로 앞 기록에서 한 주기 뒤부터 고릅니다.
    state.started = true;
    state.next = Timestamp{time.us + rule.period_us};
    return {state, true};
  }
  if (time < state.next) return {state, false};
  state.next.us += rule.period_us;
  if (state.next <= time) state.next = Timestamp{time.us + rule.period_us};
  return {state, true};
}

}  // namespace hwlab::timing
