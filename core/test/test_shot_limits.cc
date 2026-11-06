#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "hwlab/record/shot.h"
#include "hwlab/record/shot_limits.h"

namespace {

using hwlab::record::EstimateShotBytes;
using hwlab::record::FileRole;
using hwlab::record::FreeSpace;
using hwlab::record::JudgeFreeSpace;
using hwlab::record::kSidecarBytesPerFrame;
using hwlab::record::LongestShotPathLength;
using hwlab::record::ShotLayout;
using hwlab::record::ShotSizePlan;

// 폴더 홍길동_19551103/40/20260915_142233(31자)에 파일 두 개를 둔 배치입니다.
ShotLayout MakeLayout() {
  ShotLayout layout;
  layout.folder = "홍길동_19551103/40/20260915_142233";
  layout.stem = "20260915_142233";
  layout.files = {{FileRole::kManifest, "20260915_142233_shot.json"},
                  {FileRole::kThermalRaw, "20260915_142233_thermal.raw16"}};
  return layout;
}

// 기본 촬영(좌우 발 640x1200, 900장, 열화상 640x480을 111111us 주기로)의 추정량입니다.
TEST(ShotLimits, EstimatesFeetAndThermal) {
  ShotSizePlan plan;
  plan.rgb_frames = 900;
  plan.rgb_frame_rate = 30;
  plan.rgb_videos = {{640, 1200}, {640, 1200}};
  plan.thermal = true;
  plan.thermal_size = {640, 480};
  plan.thermal_record_period_us = 111111;

  // 30초 / 111111us = 270장, 첫 장 1을 더해 271장입니다.
  const int64_t rgb = 2 * 900 * (640LL * 1200 * 2 + kSidecarBytesPerFrame);
  const int64_t thermal = 271 * (640LL * 480 * 2 * 3 + kSidecarBytesPerFrame);
  EXPECT_EQ(EstimateShotBytes(plan), rgb + thermal);
}

// 기록 주기가 0이면 장치 최대 Hz로 장 수를 세고, 열화상이 없으면 RGB만 셉니다.
TEST(ShotLimits, ThermalFramesFollowRecordPeriod) {
  ShotSizePlan plan;
  plan.rgb_frames = 300;
  plan.rgb_frame_rate = 30;
  plan.rgb_videos = {{1920, 1080}};
  const int64_t rgb = 300 * (1920LL * 1080 * 2 + kSidecarBytesPerFrame);
  EXPECT_EQ(EstimateShotBytes(plan), rgb);

  plan.thermal = true;
  plan.thermal_size = {4, 2};
  plan.thermal_max_rate_hz = 30;
  EXPECT_EQ(EstimateShotBytes(plan), rgb + 301 * (4 * 2 * 2 * 3 + kSidecarBytesPerFrame));

  plan.thermal_max_rate_hz = 0;
  EXPECT_EQ(EstimateShotBytes(plan), rgb);
}

// 여유가 추정량과 예비량의 합 이상이어야 시작하고, 읽지 못하면 모름입니다. 경고 기준 미만이면 시작은 하되 낮음입니다.
TEST(ShotLimits, JudgesFreeSpace) {
  EXPECT_EQ(JudgeFreeSpace(1500, 1000, 500, 0).verdict, FreeSpace::kEnough);
  EXPECT_EQ(JudgeFreeSpace(1499, 1000, 500, 0).verdict, FreeSpace::kShort);
  EXPECT_EQ(JudgeFreeSpace(1499, 1000, 500, 0).required_bytes, 1500);
  EXPECT_EQ(JudgeFreeSpace(std::nullopt, 1000, 500, 0).verdict, FreeSpace::kUnknown);
  EXPECT_EQ(JudgeFreeSpace(0, 0, -5, 0).verdict, FreeSpace::kEnough);
}

TEST(ShotLimits, WarnsWhenFreeSpaceIsBelowLowBoundary) {
  EXPECT_EQ(JudgeFreeSpace(10'000, 1000, 500, 10'000).verdict, FreeSpace::kEnough);
  EXPECT_EQ(JudgeFreeSpace(9999, 1000, 500, 10'000).verdict, FreeSpace::kLow);
  EXPECT_EQ(JudgeFreeSpace(1500, 1000, 500, 10'000).verdict, FreeSpace::kLow);
  EXPECT_EQ(JudgeFreeSpace(1499, 1000, 500, 10'000).verdict, FreeSpace::kShort);
  EXPECT_EQ(JudgeFreeSpace(std::nullopt, 1000, 500, 10'000).verdict, FreeSpace::kUnknown);
  // 필요한 양이 경고 기준보다 크면 부족과 충분 사이에 낮음 구간이 없습니다.
  EXPECT_EQ(JudgeFreeSpace(12'000, 20'000, 500, 10'000).verdict, FreeSpace::kShort);
  EXPECT_EQ(JudgeFreeSpace(20'500, 20'000, 500, 10'000).verdict, FreeSpace::kEnough);
}

// 분석 이름이 촬영 폴더 이름이면 가장 긴 경로는 촬영 폴더 안의 분석 결과 그림 perfusion/v11/20260915_142233_report_propagation.png로
// 40 + 52 = 92자입니다. results 폴더로 모은 결과(87자, 아래 테스트)보다 깁니다. 한글은 UTF-16 한 단위로 셉니다.
TEST(ShotLimits, LongestPathCountsAnalysisResults) {
  const ShotLayout layout = MakeLayout();
  // "C:\\data"(7) + 구분자(1) + 폴더(31) + 구분자(1) = 40
  EXPECT_EQ(LongestShotPathLength("C:\\data", layout, "20260915_142233"), 92);
  EXPECT_EQ(LongestShotPathLength("C:\\data\\", layout, "20260915_142233"), 92);
  // 분석 이름이 없으면 가장 긴 파일 20260915_142233_thermal.raw16(29자)입니다.
  EXPECT_EQ(LongestShotPathLength("C:\\data", layout, ""), 40 + 29);
}

// 분석결과 폴더로 모은 결과 경로(재분석 번호 99까지)도 상한 안에 들어가는지 셉니다.
TEST(ShotLimits, LongestPathCoversCollectedResults) {
  const ShotLayout layout = MakeLayout();
  // "C:\\data"(7) + 구분자(1) + results(7) + 구분자(1) + 앞부분(31) + "_reanalysis99"(13) + "_v11"(4) + "_report_propagation.png"(23) = 87
  EXPECT_EQ(hwlab::record::CollectedResultPrefix(layout, hwlab::record::kMaxReanalysisNumber),
            "홍길동_19551103_40_20260915_142233_reanalysis99");
  EXPECT_GE(LongestShotPathLength("C:\\data", layout, "20260915_142233"), 87);
  // 분석 이름이 한 글자이면 촬영 폴더 안 결과 경로(40 + 9 + 1 + 3 + 1 + 1 + 23 = 78)보다 모은 결과 경로(87)가 깁니다.
  EXPECT_EQ(LongestShotPathLength("C:\\data", layout, "a"), 87);
}

// U+10000 이상 문자는 UTF-16 두 단위입니다. 한글 저장 위치는 한 글자 한 단위입니다.
TEST(ShotLimits, PathLengthUsesUtf16Units) {
  const ShotLayout layout = MakeLayout();
  const int32_t base = LongestShotPathLength("D:", layout, "");
  EXPECT_EQ(LongestShotPathLength("D:\xF0\x9F\x98\x80", layout, ""), base + 2);
  EXPECT_EQ(LongestShotPathLength("D:촬영", layout, ""), base + 2);
}

}  // namespace
