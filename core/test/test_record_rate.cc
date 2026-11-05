#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

#include "hwlab/timing/frame_gap.h"
#include "hwlab/timing/record_rate.h"

namespace {

using hwlab::Timestamp;
using hwlab::timing::AdvanceRecordRate;
using hwlab::timing::MedianIntervalUs;
using hwlab::timing::RecordRateRule;
using hwlab::timing::RecordRateState;

constexpr int64_t kNineHzUs = 111'111;
constexpr int64_t kSecondUs = 1'000'000;

// 수신 시각 목록을 받은 순서대로 넣고 기록한 시각을 돌려줍니다. 중앙값은 platform처럼 최근 31장으로 잽니다.
std::vector<int64_t> Recorded(const std::vector<int64_t>& arrivals, int64_t period_us = kNineHzUs) {
  const RecordRateRule rule{period_us};
  RecordRateState state;
  std::deque<Timestamp> recent;
  std::vector<int64_t> out;
  for (const int64_t t : arrivals) {
    recent.push_back(Timestamp{t});
    if (recent.size() > 31) recent.pop_front();
    const int64_t median = MedianIntervalUs(std::vector<Timestamp>(recent.begin(), recent.end()));
    const auto step = AdvanceRecordRate(state, Timestamp{t}, median, rule);
    state = step.state;
    if (step.record) out.push_back(t);
  }
  return out;
}

// 장치 주기에 0 이상 jitter_us 이하의 수신 지연을 더한 도착 시각입니다. 같은 seed에는 같은 순서가 나옵니다.
std::vector<int64_t> Arrivals(int64_t period_us, int64_t span_us, int64_t jitter_us, uint32_t seed = 7) {
  std::vector<int64_t> arrivals;
  int64_t last = -1;
  for (int64_t ideal = 1'000'000'000; ideal < 1'000'000'000 + span_us; ideal += period_us) {
    seed = seed * 1664525u + 1013904223u;
    const int64_t delay = jitter_us > 0 ? static_cast<int64_t>(seed >> 8) % (jitter_us + 1) : 0;
    last = std::max(last + 1, ideal + delay);
    arrivals.push_back(last);
  }
  return arrivals;
}

// 첫 프레임은 바로 기록합니다.
TEST(RecordRate, RecordsFirstFrame) {
  const auto step = AdvanceRecordRate({}, Timestamp{5'000'000}, 0, {kNineHzUs});
  EXPECT_TRUE(step.record);
  EXPECT_EQ(step.state.next.us, 5'000'000 + kNineHzUs);
}

// 30Hz 장치를 30초 받으면 9.0Hz로 약 270장을 기록해 관류 분석의 하한(225장)을 넘습니다.
TEST(RecordRate, ThirtyHzForThirtySecondsKeepsAbout270) {
  const auto out = Recorded(Arrivals(33'333, 30 * kSecondUs, 3'000));
  EXPECT_GE(out.size(), 225u);
  EXPECT_GE(out.size(), 268u);
  EXPECT_LE(out.size(), 271u);
  const double mean_hz = static_cast<double>(out.size() - 1) * 1e6 / static_cast<double>(out.back() - out.front());
  EXPECT_NEAR(mean_hz, 9.0, 0.05);
}

// 고를 때 기록 간격은 기록 주기에서 도착 간격 하나를 뺀 값보다 짧아지지 않습니다.
TEST(RecordRate, IntervalsStayNearPeriod) {
  const auto out = Recorded(Arrivals(33'333, 10 * kSecondUs, 0));
  ASSERT_GT(out.size(), 2u);
  for (size_t i = 1; i < out.size(); ++i) EXPECT_GE(out[i] - out[i - 1], kNineHzUs - 33'333);
}

// 9Hz 장치는 도착 간격이 변해도 받은 프레임을 모두 기록합니다.
TEST(RecordRate, NineHzDeviceWithJitterRecordsAll) {
  const auto arrivals = Arrivals(kNineHzUs, 30 * kSecondUs, 20'000);
  EXPECT_EQ(Recorded(arrivals).size(), arrivals.size());
}

// 7.5Hz 장치는 기록 주기보다 느리므로 받은 프레임을 모두 기록합니다.
TEST(RecordRate, SevenAndHalfHzDeviceRecordsAll) {
  const auto arrivals = Arrivals(133'333, 30 * kSecondUs, 20'000);
  EXPECT_EQ(Recorded(arrivals).size(), arrivals.size());
  EXPECT_GE(arrivals.size(), 225u);
}

// 실측 간격 중앙값이 기록 주기의 0.8배보다 짧을 때만 고릅니다.
TEST(RecordRate, ThinsOnlyBelowEightyPercentOfPeriod) {
  const RecordRateRule rule{100'000};
  RecordRateState state = AdvanceRecordRate({}, Timestamp{0}, 0, rule).state;
  // 다음 기록 시각(100000)보다 이른 프레임입니다.
  EXPECT_TRUE(AdvanceRecordRate(state, Timestamp{60'000}, 80'000, rule).record);
  EXPECT_FALSE(AdvanceRecordRate(state, Timestamp{60'000}, 79'999, rule).record);
}

// 장치가 2초 멈춘 뒤 다시 보내도 밀린 장을 이어서 기록하지 않습니다.
TEST(RecordRate, DoesNotBurstAfterStall) {
  std::vector<int64_t> arrivals = Arrivals(33'333, kSecondUs, 0);
  const int64_t resume = arrivals.back() + 2 * kSecondUs;
  for (int64_t t = resume; t < resume + kSecondUs; t += 33'333) arrivals.push_back(t);

  const auto out = Recorded(arrivals);

  ASSERT_GT(out.size(), 10u);
  for (size_t i = 1; i < out.size(); ++i) EXPECT_GE(out[i] - out[i - 1], kNineHzUs - 33'333);
  const auto after = std::count_if(out.begin(), out.end(), [&](int64_t t) { return t >= resume; });
  EXPECT_LE(after, 10);
}

// 기록 주기가 0이면 거르지 않습니다.
TEST(RecordRate, ZeroPeriodRecordsAll) {
  const auto arrivals = Arrivals(33'333, kSecondUs, 0);
  EXPECT_EQ(Recorded(arrivals, 0).size(), arrivals.size());
}

}  // namespace
