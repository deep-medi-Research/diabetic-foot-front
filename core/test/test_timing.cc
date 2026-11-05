#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "hwlab/timing/frame_gap.h"
#include "hwlab/timing/settle.h"

namespace {

using hwlab::SettingKind;
using hwlab::Timestamp;
using hwlab::timing::AfterFrame;
using hwlab::timing::AfterSettingChange;
using hwlab::timing::CountMissingFrames;
using hwlab::timing::FindFrameGaps;
using hwlab::timing::IsApplied;
using hwlab::timing::IsSettled;
using hwlab::timing::SettleRule;
using hwlab::timing::SettleState;

constexpr int64_t kPeriod30fpsUs = 33333;

// See3CAM_24CUG 실측값(설계 문서 표 5).
constexpr SettleRule kSee3Cam{
    .exposure_applied_frame = 4,
    .gain_applied_frame = 3,
    .white_balance_applied_frame = 3,
    .black_level_settled_frame = 75,
};

std::vector<Timestamp> EvenTimes(int count, int64_t period_us) {
  std::vector<Timestamp> times;
  for (int i = 0; i < count; ++i) {
    times.push_back({i * period_us});
  }
  return times;
}

// 받은 프레임마다 판정 결과를 기록해, n번째 프레임(1부터)의 결과를 꺼낼 수 있게 합니다.
struct Trace {
  std::vector<bool> applied;
  std::vector<bool> settled;
};

Trace RunFrames(SettleState state, int frames) {
  Trace trace;
  for (int i = 0; i < frames; ++i) {
    state = AfterFrame(state);
    trace.applied.push_back(IsApplied(state));
    trace.settled.push_back(IsSettled(state));
  }
  return trace;
}

// 간격이 고른 프레임에는 누락이 없습니다.
TEST(FrameGap, FindsNothingWhenIntervalsAreEven) {
  const auto times = EvenTimes(900, kPeriod30fpsUs);

  const auto gaps = FindFrameGaps(times, kPeriod30fpsUs);

  EXPECT_TRUE(gaps.empty());
}

// 60fps 실측에서 본 117ms 간격은 6프레임 누락입니다.
TEST(FrameGap, Counts117msGapAtSixtyFpsAsSixMissing) {
  constexpr int64_t kPeriod60fpsUs = 16667;
  auto times = EvenTimes(10, kPeriod60fpsUs);
  for (size_t i = 5; i < times.size(); ++i) {
    times[i].us += 100000;
  }

  const auto gaps = FindFrameGaps(times, kPeriod60fpsUs);

  ASSERT_EQ(gaps.size(), 1u);
  EXPECT_EQ(gaps[0].before, 4u);
  EXPECT_EQ(gaps[0].interval_us, 116667);
  EXPECT_EQ(gaps[0].missing_frames, 6);
  EXPECT_EQ(CountMissingFrames(gaps), 6);
}

// 실측 녹화(1080p 60fps 3분)에서 lab 분석과 같은 누락(26곳, 160프레임)을 찾습니다.
TEST(FrameGap, MatchesMeasuredSee3CamRecording) {
  std::ifstream file(std::string(HWLAB_TEST_DATA_DIR) + "/see3cam_1080p60_3min_sample_us.txt");
  ASSERT_TRUE(file.is_open());
  std::vector<Timestamp> times;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line[0] != '#') {
      times.push_back({std::stoll(line)});
    }
  }
  ASSERT_EQ(times.size(), 10800u);

  const auto gaps = FindFrameGaps(times, 16667);

  EXPECT_EQ(gaps.size(), 26u);
  EXPECT_EQ(CountMissingFrames(gaps), 160);
}

// 주기의 1.5배까지의 흔들림은 누락이 아닙니다.
TEST(FrameGap, IgnoresJitterUpToOneAndHalfPeriods) {
  const std::vector<Timestamp> times{{0}, {32000}, {80000}, {112000}};

  const auto gaps = FindFrameGaps(times, 32000);

  EXPECT_TRUE(gaps.empty());
}

// 주기를 모르면 판정하지 않습니다.
TEST(FrameGap, ReturnsNothingForNonPositivePeriod) {
  const auto times = EvenTimes(10, kPeriod30fpsUs);

  EXPECT_TRUE(FindFrameGaps(times, 0).empty());
}

// 간격 중앙값은 누락 한 곳에 끌려가지 않습니다.
TEST(FrameGap, MedianIgnoresSingleGap) {
  auto times = EvenTimes(31, kPeriod30fpsUs);
  for (size_t i = 10; i < times.size(); ++i) times[i].us += 100000;

  EXPECT_EQ(hwlab::timing::MedianIntervalUs(times), kPeriod30fpsUs);
}

// 2026-10-16 실기: 어두운 장면의 자동 맞춤은 15fps(66.7ms)였고 30fps 선언과 맞지 않습니다.
TEST(FrameGap, DetectsHalvedFrameRate) {
  const auto times = EvenTimes(31, 66751);

  EXPECT_FALSE(hwlab::timing::MatchesPeriod(hwlab::timing::MedianIntervalUs(times), kPeriod30fpsUs, 5));
  EXPECT_TRUE(hwlab::timing::MatchesPeriod(33332, kPeriod30fpsUs, 5));
}

// 30fps로 찍다가 2초 동안 25fps(40ms)로 떨어지면 누락은 없고 느린 간격 50개가 한 구간으로 묶입니다.
TEST(FrameRate, FindsTwoSecondsAtTwentyFiveFps) {
  std::vector<Timestamp> times;
  int64_t t = 0;
  for (int i = 0; i < 300; ++i) times.push_back({t += kPeriod30fpsUs});
  for (int i = 0; i < 50; ++i) times.push_back({t += 40000});
  for (int i = 0; i < 300; ++i) times.push_back({t += kPeriod30fpsUs});

  EXPECT_TRUE(FindFrameGaps(times, kPeriod30fpsUs).empty());
  const auto check = hwlab::timing::CheckFrameRate(times, kPeriod30fpsUs);
  EXPECT_EQ(check.slow_intervals, 50);
  ASSERT_EQ(check.spans.size(), 1u);
  EXPECT_EQ(check.spans[0].start_us, 299 * kPeriod30fpsUs);
  EXPECT_EQ(check.spans[0].end_us, 299 * kPeriod30fpsUs + 50 * 40000);
  EXPECT_EQ(check.spans[0].intervals, 50);
  EXPECT_NEAR(check.min_fps, 25.0, 0.01);
}

// 고른 30fps는 느린 간격이 없고 최저 fps가 30입니다. 실측 흔들림(33.15~33.50ms)도 세지 않습니다.
TEST(FrameRate, IgnoresEvenThirtyFpsAndJitter) {
  auto times = EvenTimes(900, kPeriod30fpsUs);
  times[100].us += 170;
  const auto check = hwlab::timing::CheckFrameRate(times, kPeriod30fpsUs);
  EXPECT_EQ(check.slow_intervals, 0);
  EXPECT_TRUE(check.spans.empty());
  EXPECT_NEAR(check.min_fps, 30.0, 0.2);
}

// 한 장 누락(간격 2배)은 느린 간격이 아니라 누락으로 세고, 최저 fps에는 반영합니다. 1초 넘게 떨어진 느린 간격은 다른 구간입니다.
TEST(FrameRate, KeepsGapsSeparateAndSplitsDistantSpans) {
  std::vector<Timestamp> times;
  int64_t t = 0;
  const auto add = [&](int count, int64_t step) {
    for (int i = 0; i < count; ++i) times.push_back({t += step});
  };
  add(60, kPeriod30fpsUs);
  add(1, 2 * kPeriod30fpsUs);  // 누락
  add(60, kPeriod30fpsUs);
  add(3, 41000);
  add(60, kPeriod30fpsUs);     // 2초 떨어짐
  add(2, 42000);
  add(30, kPeriod30fpsUs);

  EXPECT_EQ(FindFrameGaps(times, kPeriod30fpsUs).size(), 1u);
  const auto check = hwlab::timing::CheckFrameRate(times, kPeriod30fpsUs);
  EXPECT_EQ(check.slow_intervals, 5);
  ASSERT_EQ(check.spans.size(), 2u);
  EXPECT_EQ(check.spans[0].intervals, 3);
  EXPECT_EQ(check.spans[1].intervals, 2);
  EXPECT_LT(check.min_fps, 30.0);
  EXPECT_TRUE(hwlab::timing::CheckFrameRate(times, 0).spans.empty());
}

// 노출은 바꾼 뒤 4번째 프레임부터 새 값으로 찍힙니다.
TEST(Settle, ExposureAppliesFromFourthFrame) {
  const SettleState state = AfterSettingChange({}, SettingKind::kExposure, kSee3Cam);

  const Trace trace = RunFrames(state, 5);

  EXPECT_FALSE(trace.applied[2]);  // 3번째 프레임
  EXPECT_TRUE(trace.applied[3]);   // 4번째 프레임
}

// 게인은 바꾼 뒤 3번째 프레임부터 새 값으로 찍힙니다.
TEST(Settle, GainAppliesFromThirdFrame) {
  const SettleState state = AfterSettingChange({}, SettingKind::kGain, kSee3Cam);

  const Trace trace = RunFrames(state, 4);

  EXPECT_FALSE(trace.applied[1]);
  EXPECT_TRUE(trace.applied[2]);
}

// 노출을 바꾼 뒤 75번째 프레임에서 안정화가 끝납니다.
TEST(Settle, ExposureSettlesAtSeventyFifthFrame) {
  const SettleState state = AfterSettingChange({}, SettingKind::kExposure, kSee3Cam);

  const Trace trace = RunFrames(state, 80);

  EXPECT_FALSE(trace.settled[73]);  // 74번째 프레임
  EXPECT_TRUE(trace.settled[74]);   // 75번째 프레임
}

// 안정화 도중 설정을 다시 바꾸면 그 시점부터 75프레임을 다시 기다립니다.
TEST(Settle, ChangeDuringSettlingRestartsWait) {
  SettleState state = AfterSettingChange({}, SettingKind::kGain, kSee3Cam);
  for (int i = 0; i < 50; ++i) {
    state = AfterFrame(state);
  }

  state = AfterSettingChange(state, SettingKind::kExposure, kSee3Cam);
  const Trace trace = RunFrames(state, 80);

  EXPECT_FALSE(trace.settled[73]);
  EXPECT_TRUE(trace.settled[74]);
}

// 같은 시점에 노출과 게인을 바꾸면 더 늦은 노출 기준으로 반영됩니다.
TEST(Settle, SimultaneousChangesWaitForLatest) {
  SettleState state = AfterSettingChange({}, SettingKind::kGain, kSee3Cam);
  state = AfterSettingChange(state, SettingKind::kExposure, kSee3Cam);

  const Trace trace = RunFrames(state, 5);

  EXPECT_FALSE(trace.applied[2]);
  EXPECT_TRUE(trace.applied[3]);
}

// 설정을 바꾼 적이 없으면 처음부터 반영되고 안정된 상태입니다.
TEST(Settle, NoChangeMeansSettled) {
  const Trace trace = RunFrames({}, 1);

  EXPECT_TRUE(trace.applied[0]);
  EXPECT_TRUE(trace.settled[0]);
}

}  // namespace
