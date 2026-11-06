#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

#include "hwlab/timing/warmup.h"

namespace {

using hwlab::Timestamp;
using hwlab::timing::AfterCameraTemperature;
using hwlab::timing::JudgeLensShadingFit;
using hwlab::timing::LensShadingConditions;
using hwlab::timing::LensShadingFit;
using hwlab::timing::WarmupPhase;
using hwlab::timing::WarmupRule;
using hwlab::timing::WarmupState;

constexpr int64_t kSecondUs = 1'000'000;
constexpr int64_t kMinuteUs = 60 * kSecondUs;

constexpr int64_t kFramePeriodUs = 33'333;  // 30Hz

constexpr WarmupRule kRule{.window_us = 10 * kMinuteUs,
                           .sample_period_us = 10 * kSecondUs,
                           .max_gap_us = kMinuteUs,
                           .ready_fpa_per_min = 0.034,
                           .hold_us = 15 * kMinuteUs,
                           .shutter_c_per_fpa = 1.46,
                           .lens_fit_max_c = 0.7};
constexpr double kWindowMin = 10.0;

// 식은 카메라처럼 FPA 값이 88.6에서 98.2로 다가가는 곡선입니다(이전 레포 이슈 #49, 2026-10-28 셔터 24.2 °C에서 38.2 °C).
double ColdCurve(int64_t t) { return 98.2 - 9.6 * std::exp(-static_cast<double>(t) / kMinuteUs / 15.0); }

// 장치처럼 0.061 단위로 반올림한 값입니다.
double Stepped(double fpa) { return std::round(fpa / 0.061) * 0.061; }

// from_us부터 1초씩 보며, 지난 10분 동안 오른 양을 10으로 나눈 속도(이전 레포 이슈 #49의 정의)가 처음 기준 아래로
// 내려오는 시각입니다.
int64_t FirstBelowUs(const std::function<double(int64_t)>& fpa, int64_t from_us = kRule.window_us) {
  int64_t t = from_us;
  while ((fpa(t) - fpa(t - kRule.window_us)) / kWindowMin >= kRule.ready_fpa_per_min) t += kSecondUs;
  return t;
}

WarmupState Feed(WarmupState state, int64_t begin_us, int64_t end_us, int64_t step_us,
                 const std::function<double(int64_t)>& fpa,
                 const std::function<void(int64_t, const WarmupState&)>& check = nullptr) {
  for (int64_t t = begin_us; t < end_us; t += step_us) {
    state = AfterCameraTemperature(std::move(state), Timestamp{t}, fpa(t), kRule);
    if (check) check(t, state);
  }
  return state;
}

double Flat(int64_t) { return 97.0; }

TEST(Warmup, MeasuresUntilTenMinutesOfValues) {
  const WarmupState state = Feed({}, 0, 10 * kMinuteUs, kSecondUs, Flat, [](int64_t t, const WarmupState& s) {
    EXPECT_EQ(s.status.phase, WarmupPhase::kMeasuring) << t;
    EXPECT_FALSE(s.status.rate_fpa_per_min) << t;
  });
  EXPECT_EQ(state.status.remaining_us, 10 * kSecondUs);
  const WarmupState after = AfterCameraTemperature(state, Timestamp{10 * kMinuteUs}, 97.0, kRule);
  EXPECT_NE(after.status.phase, WarmupPhase::kMeasuring);
  ASSERT_TRUE(after.status.rate_fpa_per_min);
  EXPECT_DOUBLE_EQ(*after.status.rate_fpa_per_min, 0.0);
}

TEST(Warmup, RateIsRiseOverTheLastTenMinutes) {
  const auto curve = [](int64_t t) {
    const double minutes = static_cast<double>(t) / kMinuteUs;
    return 90.0 + 0.001 * minutes * minutes;
  };
  const int64_t end_us = 12 * kMinuteUs;
  const WarmupState state = Feed({}, 0, end_us + 1, kFramePeriodUs, curve);
  ASSERT_TRUE(state.status.rate_fpa_per_min);
  const double expected = (curve(end_us) - curve(end_us - kRule.window_us)) / kWindowMin;
  // 구간 시작 값은 남긴 값 중에서 고르므로 sample_period_us만큼 이를 수 있습니다.
  EXPECT_NEAR(*state.status.rate_fpa_per_min, expected, 3e-4);
}

TEST(Warmup, WarmCameraIsReadyAfterWindowPlusHold) {
  const WarmupState state = Feed({}, 0, 25 * kMinuteUs, 10 * kSecondUs, Flat, [](int64_t t, const WarmupState& s) {
    if (t < 10 * kMinuteUs) return;
    EXPECT_EQ(s.status.phase, WarmupPhase::kSettling) << t;
    EXPECT_EQ(s.status.remaining_us, 25 * kMinuteUs - t) << t;
  });
  const WarmupState ready = AfterCameraTemperature(state, Timestamp{25 * kMinuteUs}, 97.0, kRule);
  EXPECT_EQ(ready.status.phase, WarmupPhase::kReady);
  EXPECT_EQ(ready.status.remaining_us, 0);
}

TEST(Warmup, ColdCameraCurveIsReadyHoldAfterRateCrossesThreshold) {
  const int64_t cross_us = FirstBelowUs(ColdCurve);
  std::optional<int64_t> first_below;
  std::optional<int64_t> first_ready;
  Feed({}, 0, 120 * kMinuteUs, kSecondUs, ColdCurve, [&](int64_t t, const WarmupState& s) {
    if (!first_below && (s.status.phase == WarmupPhase::kSettling || s.status.phase == WarmupPhase::kReady)) first_below = t;
    if (!first_ready && s.status.phase == WarmupPhase::kReady) first_ready = t;
    if (first_below) EXPECT_NE(s.status.phase, WarmupPhase::kWarming) << "기준 아래로 내려온 뒤 다시 오르지 않음 " << t;
  });
  ASSERT_TRUE(first_below);
  ASSERT_TRUE(first_ready);
  EXPECT_GE(*first_below, cross_us - kRule.sample_period_us);
  EXPECT_LE(*first_below, cross_us + kRule.sample_period_us);
  EXPECT_GE(*first_ready, *first_below + kRule.hold_us);
  EXPECT_LE(*first_ready, *first_below + kRule.hold_us + kRule.sample_period_us);
}

TEST(Warmup, SteppedColdCameraIsReadyWithinMinutesOfSmoothCurve) {
  // 0.061 단위에서는 지난 10분 동안 오른 양이 5단위(분당 0.0305)와 6단위(분당 0.0366) 사이를 오가며 기준(분당 0.034)을
  // 넘나들어, 머문 시간을 몇 번 다시 셉니다.
  const auto stepped = [](int64_t t) { return Stepped(ColdCurve(t)); };
  const int64_t smooth_ready_us = FirstBelowUs(ColdCurve) + kRule.hold_us;
  std::optional<int64_t> first_ready;
  Feed({}, 0, 120 * kMinuteUs, kFramePeriodUs * 30, stepped, [&](int64_t t, const WarmupState& s) {
    if (!first_ready && s.status.phase == WarmupPhase::kReady) first_ready = t;
  });
  ASSERT_TRUE(first_ready);
  EXPECT_GE(*first_ready, smooth_ready_us - kRule.sample_period_us);
  EXPECT_LE(*first_ready, smooth_ready_us + 5 * kMinuteUs);
}

TEST(Warmup, RiseDuringHoldRestartsTheHold) {
  const auto fpa = [](int64_t t) {
    const int64_t rising_us = std::clamp<int64_t>(t, 20 * kMinuteUs, 25 * kMinuteUs) - 20 * kMinuteUs;
    return 97.0 + 0.1 * static_cast<double>(rising_us) / kMinuteUs;
  };
  WarmupState state = Feed({}, 0, 20 * kMinuteUs, 10 * kSecondUs, fpa);
  EXPECT_EQ(state.status.phase, WarmupPhase::kSettling);
  state = Feed(std::move(state), 20 * kMinuteUs, 25 * kMinuteUs, 10 * kSecondUs, fpa);
  EXPECT_EQ(state.status.phase, WarmupPhase::kWarming);
  std::optional<int64_t> below;
  std::optional<int64_t> ready;
  Feed(std::move(state), 25 * kMinuteUs, 60 * kMinuteUs, 10 * kSecondUs, fpa, [&](int64_t t, const WarmupState& s) {
    if (!below && s.status.phase != WarmupPhase::kWarming) below = t;
    if (!ready && s.status.phase == WarmupPhase::kReady) ready = t;
  });
  const int64_t expected_below = FirstBelowUs(fpa, 25 * kMinuteUs);
  ASSERT_TRUE(below);
  ASSERT_TRUE(ready);
  EXPECT_GE(*below, expected_below);
  EXPECT_LE(*below, expected_below + kRule.sample_period_us);
  EXPECT_EQ(*ready, *below + kRule.hold_us);
}

TEST(Warmup, ReadyCameraThatStartsRisingIsWarmingAgain) {
  WarmupState state = Feed({}, 0, 30 * kMinuteUs, 10 * kSecondUs, Flat);
  ASSERT_EQ(state.status.phase, WarmupPhase::kReady);
  const auto rising = [](int64_t t) { return 97.0 + 0.1 * static_cast<double>(t - 30 * kMinuteUs) / kMinuteUs; };
  state = Feed(std::move(state), 30 * kMinuteUs, 40 * kMinuteUs, 10 * kSecondUs, rising);
  EXPECT_EQ(state.status.phase, WarmupPhase::kWarming);
}

TEST(Warmup, GapLongerThanLimitStartsOver) {
  WarmupState state = Feed({}, 0, 30 * kMinuteUs, 10 * kSecondUs, Flat);
  ASSERT_EQ(state.status.phase, WarmupPhase::kReady);
  state = AfterCameraTemperature(std::move(state), Timestamp{30 * kMinuteUs + 50 * kSecondUs}, 97.0, kRule);
  EXPECT_EQ(state.status.phase, WarmupPhase::kReady);
  state = AfterCameraTemperature(std::move(state), Timestamp{40 * kMinuteUs}, 97.0, kRule);
  EXPECT_EQ(state.status.phase, WarmupPhase::kMeasuring);
  EXPECT_EQ(state.status.remaining_us, 10 * kMinuteUs);
}

TEST(Warmup, EarlierTimeStartsOver) {
  WarmupState state = Feed({}, 0, 30 * kMinuteUs, 10 * kSecondUs, Flat);
  state = AfterCameraTemperature(std::move(state), Timestamp{kMinuteUs}, 97.0, kRule);
  EXPECT_EQ(state.status.phase, WarmupPhase::kMeasuring);
}

TEST(LensShadingFit, JudgesRecordAgainstNow) {
  const LensShadingConditions ready{37.0, 97.0, WarmupPhase::kReady};
  const LensShadingConditions warming{35.0, 95.0, WarmupPhase::kSettling};

  EXPECT_EQ(JudgeLensShadingFit(std::nullopt, 97.0, WarmupPhase::kReady, kRule), LensShadingFit::kUnknown);
  EXPECT_EQ(JudgeLensShadingFit(warming, 95.0, WarmupPhase::kReady, kRule), LensShadingFit::kMadeWhileWarming);
  EXPECT_EQ(JudgeLensShadingFit(warming, 95.0, WarmupPhase::kWarming, kRule), LensShadingFit::kMadeWhileWarming);
  EXPECT_EQ(JudgeLensShadingFit(ready, 97.0, WarmupPhase::kMeasuring, kRule), LensShadingFit::kWaitWarmup);
  EXPECT_EQ(JudgeLensShadingFit(ready, 97.0, WarmupPhase::kSettling, kRule), LensShadingFit::kWaitWarmup);
  EXPECT_EQ(JudgeLensShadingFit(ready, std::nullopt, WarmupPhase::kReady, kRule), LensShadingFit::kUnknown);
  EXPECT_EQ(JudgeLensShadingFit(ready, 97.4, WarmupPhase::kReady, kRule), LensShadingFit::kFits);
  EXPECT_EQ(JudgeLensShadingFit(ready, 96.6, WarmupPhase::kReady, kRule), LensShadingFit::kFits);
  EXPECT_EQ(JudgeLensShadingFit(ready, 97.5, WarmupPhase::kReady, kRule), LensShadingFit::kTemperatureMoved);
  EXPECT_EQ(JudgeLensShadingFit(ready, 96.5, WarmupPhase::kReady, kRule), LensShadingFit::kTemperatureMoved);
}

}  // namespace
