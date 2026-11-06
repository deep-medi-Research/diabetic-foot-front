#include "hwlab/timing/warmup.h"

#include <cmath>

namespace hwlab::timing {

namespace {

constexpr double kMicrosPerMinute = 60e6;

// 맨 앞에 window_us 이상 지난 값 중 가장 늦은 값만 남깁니다. 그 값이 지난 구간이 시작한 때의 값입니다.
void DropBeforeWindow(std::deque<WarmupSample>& samples, Timestamp now, int64_t window_us) {
  while (samples.size() >= 2 && IntervalUs(samples[1].time, now) >= window_us) samples.pop_front();
}

}  // namespace

WarmupState AfterCameraTemperature(WarmupState state, Timestamp time, double fpa, const WarmupRule& rule) {
  if (!state.samples.empty()) {
    const int64_t since_last = IntervalUs(state.samples.back().time, time);
    if (since_last < 0 || since_last > rule.max_gap_us) {
      state = WarmupState{};
    } else if (since_last < rule.sample_period_us) {
      return state;
    }
  }
  state.samples.push_back({time, fpa});
  DropBeforeWindow(state.samples, time, rule.window_us);
  const WarmupSample& first = state.samples.front();
  const int64_t span_us = IntervalUs(first.time, time);
  if (span_us < rule.window_us || span_us <= 0) {
    state.status = {WarmupPhase::kMeasuring, std::nullopt, rule.window_us - span_us};
    state.below_since.reset();
    return state;
  }
  const double rate = (fpa - first.fpa) / (static_cast<double>(span_us) / kMicrosPerMinute);
  if (rate >= rule.ready_fpa_per_min) {
    state.status = {WarmupPhase::kWarming, rate, 0};
    state.below_since.reset();
    return state;
  }
  if (!state.below_since) state.below_since = time;
  const int64_t held_us = IntervalUs(*state.below_since, time);
  state.status = held_us >= rule.hold_us ? WarmupStatus{WarmupPhase::kReady, rate, 0}
                                         : WarmupStatus{WarmupPhase::kSettling, rate, rule.hold_us - held_us};
  return state;
}

LensShadingFit JudgeLensShadingFit(const std::optional<LensShadingConditions>& made, std::optional<double> fpa_now,
                                   WarmupPhase now, const WarmupRule& rule) {
  if (!made) return LensShadingFit::kUnknown;
  if (made->warmup != WarmupPhase::kReady) return LensShadingFit::kMadeWhileWarming;
  if (now != WarmupPhase::kReady) return LensShadingFit::kWaitWarmup;
  if (!fpa_now) return LensShadingFit::kUnknown;
  return std::abs(ShutterCelsius(*fpa_now - made->fpa, rule)) > rule.lens_fit_max_c ? LensShadingFit::kTemperatureMoved
                                                                                    : LensShadingFit::kFits;
}

}  // namespace hwlab::timing
