#include "hwlab/pixel/foot_warmth.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using hwlab::pixel::FootWarmthCheck;
using hwlab::pixel::kWarmContrastMin;
using hwlab::pixel::kWarmFrames;
using hwlab::pixel::ThermalContrast;
using hwlab::pixel::Warmth;

namespace {

// 20 x 10 Y16 프레임. 왼쪽 10열은 배경 1000에 발 화소 25개를 foot_left로, 오른쪽 10열은 배경 1000에 발 화소 25개를
// foot_right로 둡니다.
std::vector<uint16_t> Frame(uint16_t foot_left, uint16_t foot_right) {
  std::vector<uint16_t> px(200, 1000);
  for (int y = 0; y < 10; ++y) {
    for (int x = 0; x < 20; ++x) {
      const bool foot = y >= 5 && (x % 10) < 5;  // 상자마다 25화소
      if (foot) px[static_cast<size_t>(y * 20 + x)] = x < 10 ? foot_left : foot_right;
    }
  }
  return px;
}

hwlab::FrameView View(const std::vector<uint16_t>& px) {
  hwlab::FrameView v;
  v.format = hwlab::PixelFormat::kY16;
  v.width = 20;
  v.height = 10;
  v.plane[0] = reinterpret_cast<const uint8_t*>(px.data());
  v.row_stride[0] = 40;
  return v;
}

TEST(ThermalContrast, IsTopTenthMinusLowerQuarter) {
  const auto px = Frame(1100, 1030);
  // 상자 100화소 중 75화소가 배경이라 하위 25%는 배경, 상위 10%는 발 화소입니다.
  EXPECT_EQ(ThermalContrast(View(px), {0, 0, 10, 10}), 100);
  EXPECT_EQ(ThermalContrast(View(px), {10, 0, 10, 10}), 30);
  EXPECT_EQ(ThermalContrast(View(px), {0, 0, 0, 0}), 0);
}

TEST(FootWarmthCheck, NeedsFullWindowBeforeJudging) {
  FootWarmthCheck check;
  for (int i = 0; i + 1 < kWarmFrames; ++i) check.Push({80, 30});
  EXPECT_EQ(check.State(0), Warmth::kChecking);
  EXPECT_EQ(check.State(1), Warmth::kChecking);
  check.Push({80, 30});
  EXPECT_EQ(check.State(0), Warmth::kWarm);
  EXPECT_EQ(check.State(1), Warmth::kCold);
  EXPECT_EQ(check.Lowest(1), 30);
}

TEST(FootWarmthCheck, OneLowFrameKeepsFootColdUntilItLeavesTheWindow) {
  FootWarmthCheck check;
  for (int i = 0; i < kWarmFrames; ++i) check.Push({kWarmContrastMin, 90});
  EXPECT_EQ(check.State(0), Warmth::kWarm);  // 기준과 같은 값은 따뜻함
  check.Push({kWarmContrastMin - 1, 90});
  EXPECT_EQ(check.State(0), Warmth::kCold);
  for (int i = 0; i + 1 < kWarmFrames; ++i) check.Push({70, 90});
  EXPECT_EQ(check.State(0), Warmth::kCold);  // 낮은 장이 아직 최근 kWarmFrames장 안에 있음
  check.Push({70, 90});
  EXPECT_EQ(check.State(0), Warmth::kWarm);
  check.Reset();
  EXPECT_EQ(check.State(0), Warmth::kChecking);
  EXPECT_EQ(check.Lowest(0), 0);
}

}  // namespace
