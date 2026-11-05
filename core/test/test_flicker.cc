#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "hwlab/pixel/brightness.h"

namespace {

using hwlab::pixel::Flicker;
using hwlab::pixel::SummarizeFlicker;

constexpr int64_t kPixels = 1920 * 1080;
constexpr double kFps = 30.0;

// 시각 0부터 t까지 PWM이 켜져 있던 시간입니다. 주기마다 앞쪽 duty만큼 켜집니다.
double OnTime(double t, double pwm_hz, double duty) {
  const double period = 1.0 / pwm_hz;
  const double cycles = std::floor(t / period);
  return cycles * duty * period + std::min(t - cycles * period, duty * period);
}

// 프레임 i의 노출 구간 [i / fps, i / fps + exposure)에 들어온 빛을 영역 밝기 합계로 만듭니다. 켜짐 100%가 밝기 200입니다.
std::vector<int64_t> RecordPwm(int frames, double pwm_hz, double duty, double exposure_s) {
  std::vector<int64_t> sums;
  for (int i = 0; i < frames; ++i) {
    const double start = i / kFps;
    const double lit = (OnTime(start + exposure_s, pwm_hz, duty) - OnTime(start, pwm_hz, duty)) / exposure_s;
    sums.push_back(std::llround(lit * 200.0 * static_cast<double>(kPixels)));
  }
  return sums;
}

// PWM 994Hz, 노출 10ms(PWM 주기의 9.94배)에서 30fps 900장은 994 - 33 x 30 = 4Hz, 7.5프레임 주기로 오르내립니다.
TEST(Flicker, FindsBeatPeriodWhenExposureMissesPwmPeriod) {
  const std::vector<int64_t> sums = RecordPwm(900, 994.0, 0.5, 0.010);

  const Flicker flicker = SummarizeFlicker(sums, kPixels, 0.5);

  ASSERT_TRUE(flicker.dominant_period_frames.has_value());
  EXPECT_NEAR(*flicker.dominant_period_frames, 7.5, 0.01);
  EXPECT_EQ(flicker.frame_count, 900);
  EXPECT_NEAR(flicker.mean, 100.0, 0.5);
  EXPECT_GT(flicker.peak_to_peak, 1.0);
  EXPECT_GT(flicker.stddev, 0.3);
}

// 정수가 아닌 주기(PWM 997Hz는 997 - 33 x 30 = 7Hz, 30 / 7 = 4.2857프레임)도 소수로 찾습니다.
TEST(Flicker, FindsFractionalBeatPeriod) {
  const std::vector<int64_t> sums = RecordPwm(900, 997.0, 0.3, 0.0123);

  const Flicker flicker = SummarizeFlicker(sums, kPixels, 0.5);

  ASSERT_TRUE(flicker.dominant_period_frames.has_value());
  EXPECT_NEAR(*flicker.dominant_period_frames, 30.0 / 7.0, 0.01);
}

// 노출 10ms가 PWM 1000Hz 주기의 정확히 10배이면 프레임마다 켜짐 시간이 같아 주기가 없습니다.
TEST(Flicker, NoPeriodWhenExposureIsPwmMultiple) {
  const std::vector<int64_t> sums = RecordPwm(900, 1000.0, 0.5, 0.010);

  const Flicker flicker = SummarizeFlicker(sums, kPixels, 0.5);

  EXPECT_FALSE(flicker.dominant_period_frames.has_value());
  EXPECT_NEAR(flicker.mean, 100.0, 1e-6);
  EXPECT_LT(flicker.peak_to_peak, 1e-5);
}

// 모든 프레임의 밝기가 같으면 주기가 없고 표준편차와 최대 최소 차가 0입니다.
TEST(Flicker, NoPeriodForConstantBrightness) {
  const std::vector<int64_t> sums(900, 120 * kPixels);

  const Flicker flicker = SummarizeFlicker(sums, kPixels, 0.5);

  EXPECT_FALSE(flicker.dominant_period_frames.has_value());
  EXPECT_DOUBLE_EQ(flicker.mean, 120.0);
  EXPECT_DOUBLE_EQ(flicker.stddev, 0.0);
  EXPECT_DOUBLE_EQ(flicker.peak_to_peak, 0.0);
}

// 주기 없는 잡음만 있으면 가장 큰 성분의 비율이 낮아 주기로 보지 않습니다.
TEST(Flicker, NoPeriodForNoise) {
  std::vector<int64_t> sums;
  uint32_t state = 12345;
  for (int i = 0; i < 900; ++i) {
    state = state * 1664525u + 1013904223u;
    sums.push_back(120 * kPixels + static_cast<int64_t>(state >> 16) - 32768);
  }

  const Flicker flicker = SummarizeFlicker(sums, kPixels, 0.5);

  EXPECT_FALSE(flicker.dominant_period_frames.has_value());
  EXPECT_GT(flicker.stddev, 0.0);
  EXPECT_LT(flicker.periodic_fraction, 0.1);
}

// 직선으로 밝아지는 추세 위의 주기도 추세에 가리지 않고 찾습니다.
TEST(Flicker, IgnoresLinearDrift) {
  std::vector<int64_t> sums = RecordPwm(900, 994.0, 0.5, 0.010);
  for (size_t i = 0; i < sums.size(); ++i) sums[i] += static_cast<int64_t>(i) * kPixels / 100;

  const Flicker flicker = SummarizeFlicker(sums, kPixels, 0.5);

  ASSERT_TRUE(flicker.dominant_period_frames.has_value());
  EXPECT_NEAR(*flicker.dominant_period_frames, 7.5, 0.01);
}

// 이웃한 프레임끼리 번갈아 밝고 어두우면 2프레임 주기입니다.
TEST(Flicker, FindsTwoFramePeriod) {
  std::vector<int64_t> sums;
  for (int i = 0; i < 900; ++i) sums.push_back((i % 2 == 0 ? 110 : 100) * kPixels);

  const Flicker flicker = SummarizeFlicker(sums, kPixels, 0.5);

  ASSERT_TRUE(flicker.dominant_period_frames.has_value());
  EXPECT_NEAR(*flicker.dominant_period_frames, 2.0, 1e-6);
  EXPECT_NEAR(flicker.periodic_fraction, 1.0, 1e-4);
  EXPECT_DOUBLE_EQ(flicker.peak_to_peak, 10.0);
}

// 4장보다 적으면 요약하지 않습니다.
TEST(Flicker, TooFewFrames) {
  const std::vector<int64_t> sums{1, 2, 3};

  const Flicker flicker = SummarizeFlicker(sums, 1, 0.5);

  EXPECT_EQ(flicker.frame_count, 3);
  EXPECT_FALSE(flicker.dominant_period_frames.has_value());
}

using hwlab::pixel::JudgeLightResponse;
using hwlab::pixel::LightResponse;
using hwlab::pixel::LightResponseRule;

// 시험용 기준입니다. 실측값이 아닙니다.
constexpr LightResponseRule kResponseRule{.applied_frame = 2, .window_frames = 3, .min_mean_change = 2.0};

// 밝기를 올렸을 때 반영 뒤 평균이 기준 이상 오르면 반영으로 봅니다. 명령 전은 마지막 3장, 반영 뒤는 처음 3장만 씁니다.
TEST(LightResponse, AppliedWhenBrightnessFollowsCommand) {
  const std::vector<double> before = {10.0, 50.0, 60.0, 60.0, 60.0};
  const std::vector<double> after = {63.0, 62.0, 62.0, 0.0};

  EXPECT_EQ(JudgeLightResponse(before, after, 20, kResponseRule), LightResponse::kApplied);
}

// 밝기를 내렸으면 평균이 내려가야 반영입니다.
TEST(LightResponse, AppliedWhenDimmingLowersBrightness) {
  const std::vector<double> before = {80.0, 80.0, 80.0};
  const std::vector<double> after = {70.0, 71.0, 72.0};

  EXPECT_EQ(JudgeLightResponse(before, after, -30, kResponseRule), LightResponse::kApplied);
}

// 변화가 기준보다 작거나 반대 방향이면 보이지 않은 것으로 봅니다.
TEST(LightResponse, NotSeenWhenChangeTooSmallOrOpposite) {
  const std::vector<double> before = {60.0, 60.0, 60.0};
  const std::vector<double> small = {61.0, 61.0, 61.0};
  const std::vector<double> darker = {50.0, 50.0, 50.0};

  EXPECT_EQ(JudgeLightResponse(before, small, 10, kResponseRule), LightResponse::kNotSeen);
  EXPECT_EQ(JudgeLightResponse(before, darker, 10, kResponseRule), LightResponse::kNotSeen);
}

// 기준값이 없거나 프레임이 모자라거나 보낸 변화가 0이면 판정하지 않습니다.
TEST(LightResponse, UnknownWithoutRuleFramesOrChange) {
  const std::vector<double> before = {60.0, 60.0, 60.0};
  const std::vector<double> after = {90.0, 90.0, 90.0};

  EXPECT_EQ(JudgeLightResponse(before, after, 10, LightResponseRule{}), LightResponse::kUnknown);
  EXPECT_EQ(JudgeLightResponse(std::vector<double>{60.0, 60.0}, after, 10, kResponseRule), LightResponse::kUnknown);
  EXPECT_EQ(JudgeLightResponse(before, std::vector<double>{90.0}, 10, kResponseRule), LightResponse::kUnknown);
  EXPECT_EQ(JudgeLightResponse(before, after, 0, kResponseRule), LightResponse::kUnknown);
}

}  // namespace
