#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <vector>

#include "hwlab/timing/clock_map.h"

namespace {

using hwlab::IntervalUs;
using hwlab::Timestamp;
using hwlab::timing::ClockMap;
using hwlab::timing::ClockSample;
using hwlab::timing::ClockSegment;
using hwlab::timing::FitClockMap;
using hwlab::timing::FitClockSegments;
using hwlab::timing::NearestFrames;
using hwlab::timing::ToCommonTime;

constexpr int64_t kOffsetUs = 1'234'000;
constexpr double kRate = 1.0001;  // 장치 시계가 PC 시계보다 100ppm 느림

// 촬영 순간의 PC 시각입니다.
int64_t TrueHostUs(int64_t device_us) {
  return kOffsetUs + static_cast<int64_t>(static_cast<double>(device_us) * kRate);
}

// 30fps 프레임 count장에 수신 지연 0~8ms를 붙입니다. 분포 객체는 표준 라이브러리마다 결과가 달라 엔진 출력을 직접 씁니다.
std::vector<ClockSample> MakeSamples(int64_t start_us, int count, uint32_t seed) {
  std::mt19937 engine(seed);
  std::vector<ClockSample> samples;
  for (int i = 0; i < count; ++i) {
    const int64_t device_us = start_us + int64_t{i} * 33'333;
    const int64_t delay_us = static_cast<int64_t>(engine() % 8'001);
    samples.push_back({{device_us}, {TrueHostUs(device_us) + delay_us}});
  }
  return samples;
}

constexpr double kPpgRate = 1.005;  // PPG 장치 시계가 PC 시계보다 약 0.5% 느림(실측)
constexpr int64_t kPpgMinDelayUs = 2'000;

// PPG 표본의 측정 순간을 PC 시각으로 나타냅니다. extra_us는 장치가 보내지 않은 시간만큼 뒤로 밀린 양입니다.
int64_t PpgCaptureUs(int64_t device_us, int64_t extra_us) {
  return kOffsetUs + extra_us + static_cast<int64_t>(static_cast<double>(device_us) * kPpgRate);
}

// 측정 데이터(표본 24개, 48ms)마다 마지막 표본의 장치 시각과 PC 수신 시각입니다. 수신 지연은 2~12ms입니다.
// 앞 early개를 받은 뒤 장치가 extra_us 동안 보내지 않습니다(다시 시작 직후의 수신 빈틈).
std::vector<ClockSample> MakePpgSamples(int messages, int early, int64_t extra_us, uint32_t seed) {
  std::mt19937 engine(seed);
  std::vector<ClockSample> samples;
  for (int m = 0; m < messages; ++m) {
    const int64_t device_us = int64_t{m} * 48'000 + 46'000;
    const int64_t delay_us = kPpgMinDelayUs + static_cast<int64_t>(engine() % 10'001);
    samples.push_back({{device_us}, {PpgCaptureUs(device_us, m >= early ? extra_us : 0) + delay_us}});
  }
  return samples;
}

// 오프셋 1.234초, 속도 차이 100ppm, 수신 지연 0~8ms인 900장에서 공통 시각 오차가 1ms 이내입니다.
TEST(ClockMap, FitsOffsetAndRateWithinOneMillisecond) {
  const std::vector<ClockSample> samples = MakeSamples(0, 900, 20261019);

  const ClockMap map = FitClockMap(samples);

  EXPECT_NEAR(map.rate, kRate, 5e-6);
  for (const ClockSample& s : samples) {
    const int64_t error_us = IntervalUs({TrueHostUs(s.device.us)}, ToCommonTime(map, s.device));
    EXPECT_GE(error_us, -1'000);
    EXPECT_LE(error_us, 1'000);
    // 하한선은 표본 아래를 지나므로 공통 시각은 수신 시각보다 늦지 않습니다(반올림 1us 허용).
    EXPECT_LE(ToCommonTime(map, s.device).us, s.host.us + 1);
  }
  EXPECT_GT(map.max_residual_us, 0);
  EXPECT_LE(map.max_residual_us, 8'100);
}

// 장치 시각이 부팅 뒤 수일(10^11 마이크로초 이상)이어도 같은 정확도로 맞춥니다.
TEST(ClockMap, FitsLargeDeviceTimes) {
  const std::vector<ClockSample> samples = MakeSamples(400'000'000'000, 900, 7);

  const ClockMap map = FitClockMap(samples);

  for (const ClockSample& s : samples) {
    const int64_t error_us = IntervalUs({TrueHostUs(s.device.us)}, ToCommonTime(map, s.device));
    EXPECT_GE(error_us, -1'000);
    EXPECT_LE(error_us, 1'000);
  }
}

// 열화상처럼 PC 수신 시각을 장치 시각으로 쓰면 항등식이 나옵니다.
TEST(ClockMap, HostTimesAsDeviceTimesGiveIdentity) {
  std::vector<ClockSample> samples;
  for (int64_t i = 0; i < 100; ++i) samples.push_back({{5'000 + i * 33'000}, {5'000 + i * 33'000}});

  const ClockMap map = FitClockMap(samples);

  EXPECT_EQ(map.offset_us, 0);
  EXPECT_DOUBLE_EQ(map.rate, 1.0);
  EXPECT_EQ(map.max_residual_us, 0);
}

// 표본이 없으면 항등식입니다.
TEST(ClockMap, EmptyGivesIdentity) {
  EXPECT_EQ(FitClockMap({}), ClockMap{});
  EXPECT_EQ(ToCommonTime(ClockMap{}, {123}).us, 123);
}

// 표본이 2개 이하이면 속도는 1로 두고 가장 짧은 수신 지연에 맞춥니다.
TEST(ClockMap, TwoOrFewerSamplesFitOffsetOnly) {
  const std::vector<ClockSample> one = {{{1'000}, {6'000}}};
  const ClockMap one_map = FitClockMap(one);
  EXPECT_EQ(one_map, (ClockMap{5'000, 1.0, 0}));

  const std::vector<ClockSample> two = {{{1'000}, {6'000}}, {{2'000}, {7'500}}};
  const ClockMap two_map = FitClockMap(two);
  EXPECT_EQ(two_map, (ClockMap{5'000, 1.0, 500}));
}

// 장치 시각이 거꾸로 가면(장치 시계 초기화) 속도는 1로 두고 가장 짧은 수신 지연에 맞춥니다.
TEST(ClockMap, BackwardDeviceTimeFitsOffsetOnly) {
  const std::vector<ClockSample> samples = {
      {{10'000}, {20'000}}, {{43'333}, {53'500}}, {{0}, {86'666}}, {{33'333}, {120'000}}};

  const ClockMap map = FitClockMap(samples);

  EXPECT_DOUBLE_EQ(map.rate, 1.0);
  EXPECT_EQ(map.offset_us, 10'000);
  EXPECT_EQ(map.max_residual_us, 120'000 - 33'333 - 10'000);
}

// 장치 시각이 모두 같으면 기울기를 정할 수 없어 속도 1로 맞춥니다.
TEST(ClockMap, EqualDeviceTimesFitOffsetOnly) {
  const std::vector<ClockSample> samples = {{{1'000}, {3'000}}, {{1'000}, {2'500}}, {{1'000}, {4'000}}};

  const ClockMap map = FitClockMap(samples);

  EXPECT_EQ(map, (ClockMap{1'500, 1.0, 1'500}));
}

// PC 수신 시각이 줄어드는 입력(구한 속도가 0 이하)은 속도 1로 맞춥니다.
TEST(ClockMap, NonPositiveRateFitsOffsetOnly) {
  const std::vector<ClockSample> samples = {{{0}, {30'000}}, {{10'000}, {20'000}}, {{20'000}, {10'000}}};

  const ClockMap map = FitClockMap(samples);

  EXPECT_DOUBLE_EQ(map.rate, 1.0);
  EXPECT_EQ(map.offset_us, -10'000);
}

// 수신 빈틈이 없으면 시계 구간은 하나이고 FitClockMap 결과와 같습니다.
TEST(ClockSegments, NoGapIsSingleFit) {
  const std::vector<ClockSample> samples = MakeSamples(0, 900, 20261021);

  const std::vector<ClockSegment> segments = FitClockSegments(samples, 50'000);

  ASSERT_EQ(segments.size(), 1u);
  EXPECT_EQ(segments[0].map, FitClockMap(samples));
  EXPECT_EQ(segments[0].last_device, samples.back().device);
}

// 실측 모양(다시 시작 직후 측정 데이터 1~2개 뒤 평소보다 95~128ms 늦게 다음 데이터, 35초)입니다. 한 식으로 맞추면
// 녹화 시작 무렵(3초) 공통 시각이 50ms 넘게 이르고, 시계 구간으로 나누면 빈틈 뒤 표본이 1ms 안에서 맞습니다.
TEST(ClockSegments, RestartGapKeepsCommonTimeWithinOneMillisecond) {
  struct Case {
    int early;
    int64_t extra_us;
  };
  for (const Case c : {Case{1, 95'000}, Case{1, 128'000}, Case{2, 128'000}}) {
    SCOPED_TRACE(testing::Message() << "early " << c.early << ", extra " << c.extra_us);
    const std::vector<ClockSample> samples = MakePpgSamples(730, c.early, c.extra_us, 20261020);

    const std::vector<ClockSegment> segments = FitClockSegments(samples, 50'000);

    ASSERT_EQ(segments.size(), 2u);
    EXPECT_EQ(segments[0].last_device, samples[static_cast<size_t>(c.early) - 1].device);
    EXPECT_NEAR(segments[1].map.rate, kPpgRate, 5e-6);
    EXPECT_DOUBLE_EQ(segments[0].map.rate, segments[1].map.rate);
    for (size_t i = static_cast<size_t>(c.early); i < samples.size(); ++i) {
      const int64_t truth = PpgCaptureUs(samples[i].device.us, c.extra_us) + kPpgMinDelayUs;
      const int64_t error_us = ToCommonTime(segments, samples[i].device).us - truth;
      EXPECT_GE(error_us, -1'000);
      EXPECT_LE(error_us, 1'000);
    }
    // 빈틈 앞 표본은 수신 시각보다 늦지 않고, 수신 지연의 흔들림(10ms) 안에서 맞습니다.
    for (int i = 0; i < c.early; ++i) {
      const ClockSample& s = samples[static_cast<size_t>(i)];
      const int64_t error_us = ToCommonTime(segments, s.device).us - (PpgCaptureUs(s.device.us, 0) + kPpgMinDelayUs);
      EXPECT_GE(error_us, -1);
      EXPECT_LE(error_us, 10'001);
      EXPECT_LE(ToCommonTime(segments, s.device).us, s.host.us + 1);
    }

    const ClockMap single = FitClockMap(samples);
    const Timestamp at_3s{62 * 48'000 + 46'000};
    EXPECT_LT(ToCommonTime(single, at_3s).us - (PpgCaptureUs(at_3s.us, c.extra_us) + kPpgMinDelayUs), -50'000);
  }
}

// 장치 시각마다 그 시각이 든 시계 구간의 식을 쓰고, 마지막 구간보다 뒤는 마지막 구간, 구간이 없으면 장치 시각 그대로입니다.
TEST(ClockSegments, ToCommonTimePicksSegmentByDeviceTime) {
  const std::vector<ClockSegment> segments = {{{1'000}, ClockMap{100, 1.0, 0}}, {{5'000}, ClockMap{300, 1.0, 0}}};

  EXPECT_EQ(ToCommonTime(segments, {0}).us, 100);
  EXPECT_EQ(ToCommonTime(segments, {1'000}).us, 1'100);
  EXPECT_EQ(ToCommonTime(segments, {1'002}).us, 1'302);
  EXPECT_EQ(ToCommonTime(segments, {9'000}).us, 9'300);
  EXPECT_EQ(ToCommonTime(std::vector<ClockSegment>{}, {123}).us, 123);
}

// 빈틈 판정 시간이 0 이하이면 나누지 않고, 표본이 없으면 빈 목록이며, 장치 시각이 거꾸로 가면 FitClockMap 결과 하나입니다.
TEST(ClockSegments, CasesThatDoNotSplit) {
  const std::vector<ClockSample> samples = MakePpgSamples(100, 1, 128'000, 3);
  const std::vector<ClockSegment> unsplit = FitClockSegments(samples, 0);
  ASSERT_EQ(unsplit.size(), 1u);
  EXPECT_EQ(unsplit[0].map, FitClockMap(samples));

  EXPECT_TRUE(FitClockSegments({}, 50'000).empty());

  const std::vector<ClockSample> backward = {
      {{10'000}, {20'000}}, {{43'333}, {53'500}}, {{0}, {286'666}}, {{33'333}, {320'000}}};
  const std::vector<ClockSegment> fallback = FitClockSegments(backward, 50'000);
  ASSERT_EQ(fallback.size(), 1u);
  EXPECT_EQ(fallback[0].map, FitClockMap(backward));
  EXPECT_EQ(fallback[0].last_device, Timestamp{43'333});
}

// 기준 프레임마다 가장 가까운 다른 스트림 위치를 고르고, 거리가 같으면 앞 위치입니다.
TEST(NearestFrames, PicksClosestAndEarlierOnTie) {
  const std::vector<Timestamp> rgb = {{0}, {33'333}, {66'666}, {100'000}, {200'000}};
  const std::vector<Timestamp> thermal = {{-5'000}, {30'000}, {50'000}, {70'000}};

  const std::vector<size_t> nearest = NearestFrames(rgb, thermal);

  EXPECT_EQ(nearest, (std::vector<size_t>{0, 1, 3, 3, 3}));

  const std::vector<Timestamp> tie = {{0}, {20}};
  EXPECT_EQ(NearestFrames(std::vector<Timestamp>{{10}}, tie), (std::vector<size_t>{0}));
}

// 다른 스트림이 비어 있으면 빈 목록, 기준이 비어 있어도 빈 목록입니다.
TEST(NearestFrames, EmptyInputsGiveEmpty) {
  const std::vector<Timestamp> rgb = {{0}, {33'333}};
  EXPECT_TRUE(NearestFrames(rgb, {}).empty());
  EXPECT_TRUE(NearestFrames({}, rgb).empty());
}

// RGB 30fps 900장과 열화상 30Hz를 공통 시간축으로 옮기면 RGB 프레임마다 반 주기 안의 열화상 프레임을 찾습니다.
TEST(NearestFrames, MatchesStreamsOnCommonTime) {
  const std::vector<ClockSample> rgb_samples = MakeSamples(0, 900, 11);
  const ClockMap rgb_map = FitClockMap(rgb_samples);
  std::vector<Timestamp> rgb;
  for (const ClockSample& s : rgb_samples) rgb.push_back(ToCommonTime(rgb_map, s.device));
  // 열화상은 PC 수신 시각을 그대로 쓰며 RGB보다 10ms 늦게 시작합니다.
  std::vector<Timestamp> thermal;
  for (int64_t i = 0; i < 905; ++i) thermal.push_back({kOffsetUs - 10'000 + i * 33'333});

  const std::vector<size_t> nearest = NearestFrames(rgb, thermal);

  ASSERT_EQ(nearest.size(), rgb.size());
  for (size_t i = 0; i < rgb.size(); ++i) {
    const int64_t distance_us = IntervalUs(rgb[i], thermal[nearest[i]]);
    EXPECT_LE(distance_us < 0 ? -distance_us : distance_us, 16'667);
  }
}

}  // namespace
