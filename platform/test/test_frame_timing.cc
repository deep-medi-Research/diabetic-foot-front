#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "hwlab/platform/frame_timing.h"

namespace {

using hwlab::Timestamp;
using hwlab::platform::AnalysisWindow;
using hwlab::platform::BuildFrameTimingCsv;
using hwlab::platform::BuildFrameWindowsCsv;
using hwlab::platform::SummarizeFrameWindows;

constexpr int64_t kPeriodUs = 33333;

// 30fps 900장 중 프레임 300~350이 25fps(40ms)인 녹화입니다.
std::vector<Timestamp> TimesWithSlowSpan() {
  std::vector<Timestamp> times;
  int64_t t = 1'000'000;
  for (int i = 0; i < 900; ++i) {
    times.push_back({t});
    t += (i >= 300 && i < 350) ? 40000 : kPeriodUs;
  }
  return times;
}

// 느린 간격이 든 분석 구간만 느린 간격 수와 최저 fps가 달라집니다. 구간은 프레임 번호 / fps로 프레임에 대응합니다.
TEST(FrameTiming, SummarizesEachAnalysisWindow) {
  const auto times = TimesWithSlowSpan();
  const std::vector<AnalysisWindow> windows = {{"pi", 5, 10}, {"pi", 11, 10}, {"cuff_level", 25, 10}};
  const auto rows = SummarizeFrameWindows(times, 30, windows);
  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(rows[0].first_frame, 0);
  EXPECT_EQ(rows[0].frames, 300);
  EXPECT_EQ(rows[0].slow_intervals, 0);
  EXPECT_NEAR(rows[0].min_fps, 30, 0.1);
  EXPECT_EQ(rows[1].first_frame, 180);
  EXPECT_EQ(rows[1].slow_intervals, 50);
  EXPECT_NEAR(rows[1].min_fps, 25, 0.1);
  EXPECT_GT(rows[1].elapsed_s, rows[0].elapsed_s);
  EXPECT_EQ(rows[2].slow_intervals, 0);
  EXPECT_EQ(rows[2].missing_frames, 0);
  EXPECT_TRUE(SummarizeFrameWindows(times, 0, windows).empty());
}

// 프레임별 CSV는 첫 프레임 간격을 비우고, 1.2배 초과는 slow, 1.5배 초과는 gap입니다.
TEST(FrameTiming, WritesFrameRowsWithKind) {
  const std::vector<Timestamp> times = {{0}, {33333}, {73333}, {140000}};
  const std::string csv = BuildFrameTimingCsv(times, 30);
  EXPECT_EQ(csv,
            "frame,analysis_s,elapsed_s,common_time_us,interval_ms,kind\n"
            "0,0.0000,0.000000,0,,\n"
            "1,0.0333,0.033333,33333,33.333,\n"
            "2,0.0667,0.073333,73333,40.000,slow\n"
            "3,0.1000,0.140000,140000,66.667,gap\n");
}

TEST(FrameTiming, WritesWindowRows) {
  const auto rows = SummarizeFrameWindows(TimesWithSlowSpan(), 30, {{"cuff_level", 15, 10}});
  const std::string csv = BuildFrameWindowsCsv(rows);
  EXPECT_EQ(csv.substr(0, csv.find('\n')),
            "kind,centre_s,start_s,end_s,first_frame,frames,elapsed_s,slow_intervals,missing_frames,min_fps");
  EXPECT_NE(csv.find("cuff_level,15.000,10.000,20.000,300,300,"), std::string::npos);
  EXPECT_NE(csv.find(",50,0,25.0\n"), std::string::npos);
}

}  // namespace
