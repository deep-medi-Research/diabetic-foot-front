#include <gtest/gtest.h>

#include <vector>

#include "hwlab/pixel/side_regions.h"

namespace {

using hwlab::PixelFormat;
using hwlab::pixel::CheckSideRegions;
using hwlab::pixel::DefaultSideRegions;
using hwlab::pixel::Rect;
using hwlab::pixel::ShiftInside;
using hwlab::pixel::Side;
using hwlab::pixel::SideFinding;
using hwlab::pixel::SideProblem;
using hwlab::pixel::SideRegions;

// 1920x1200(센서 전체) 기본 영역은 높이만 1200이고 위치와 폭은 1080p와 같습니다.
TEST(SideRegions, DefaultsAtFullSensor) {
  const SideRegions regions = DefaultSideRegions(1920, 1200, 640, PixelFormat::kUyvy);
  EXPECT_EQ(regions.left, (Rect{320, 0, 640, 1200}));
  EXPECT_EQ(regions.right, (Rect{960, 0, 640, 1200}));
  EXPECT_TRUE(CheckSideRegions(regions, PixelFormat::kUyvy, 1920, 1200).empty());
}

// 1080p 기본 영역은 가운데 세로선에 맞닿은 폭 640, 높이 1080의 세로형 두 칸입니다.
TEST(SideRegions, DefaultSplitsAtCenterWithMaxWidth) {
  const SideRegions regions = DefaultSideRegions(1920, 1080, 640, PixelFormat::kUyvy);

  EXPECT_EQ(regions.left, (Rect{320, 0, 640, 1080}));
  EXPECT_EQ(regions.right, (Rect{960, 0, 640, 1080}));
  EXPECT_TRUE(CheckSideRegions(regions, PixelFormat::kUyvy, 1920, 1080).empty());
}

// 최대 폭을 주지 않으면 프레임을 가운데에서 반씩 나눕니다(열화상 기본값).
TEST(SideRegions, DefaultWithoutMaxWidthUsesHalves) {
  const SideRegions regions = DefaultSideRegions(640, 480, 0, PixelFormat::kY16);

  EXPECT_EQ(regions.left, (Rect{0, 0, 320, 480}));
  EXPECT_EQ(regions.right, (Rect{320, 0, 320, 480}));
}

// UYVY에서 가운데 세로선과 폭은 짝수로 내립니다.
TEST(SideRegions, DefaultAlignsCenterToChromaGrid) {
  const SideRegions regions = DefaultSideRegions(1922, 1080, 641, PixelFormat::kUyvy);

  EXPECT_EQ(regions.left, (Rect{320, 0, 640, 1080}));
  EXPECT_EQ(regions.right, (Rect{960, 0, 640, 1080}));
}

// 나눌 수 없는 프레임은 빈 영역입니다.
TEST(SideRegions, DefaultOfTinyFrameIsEmpty) {
  EXPECT_EQ(DefaultSideRegions(1, 10, 0, PixelFormat::kY16), SideRegions{});
}

// 프레임 밖으로 끈 영역은 크기를 유지한 채 안으로 들어오고, UYVY의 x는 짝수로 내립니다.
TEST(SideRegions, ShiftInsideKeepsSizeAndAlignment) {
  EXPECT_EQ(ShiftInside({-50, -3, 640, 1080}, PixelFormat::kUyvy, 1920, 1080), (Rect{0, 0, 640, 1080}));
  EXPECT_EQ(ShiftInside({1500, 10, 640, 1080}, PixelFormat::kUyvy, 1920, 1080), (Rect{1280, 0, 640, 1080}));
  EXPECT_EQ(ShiftInside({101, 7, 301, 500}, PixelFormat::kUyvy, 1920, 1080), (Rect{100, 7, 300, 500}));
}

// 프레임보다 큰 영역은 프레임 크기로 줄이고, 빈 영역은 빈 영역으로 둡니다.
TEST(SideRegions, ShiftInsideClampsOversizeAndEmpty) {
  EXPECT_EQ(ShiftInside({0, 0, 4000, 2000}, PixelFormat::kUyvy, 1920, 1080), (Rect{0, 0, 1920, 1080}));
  EXPECT_EQ(ShiftInside({5, 5, 0, 10}, PixelFormat::kUyvy, 1920, 1080), Rect{});
}

// 가로로 긴 영역은 세로형이 아니라는 문제로 알립니다.
TEST(SideRegions, LandscapeRegionIsReported) {
  const SideRegions regions{{0, 0, 800, 600}, {960, 0, 640, 1080}};

  const auto findings = CheckSideRegions(regions, PixelFormat::kUyvy, 1920, 1080);

  EXPECT_EQ(findings, (std::vector<SideFinding>{{SideProblem::kNotPortrait, Side::kLeft}}));
}

// x가 홀수이거나 프레임 밖인 영역은 격자 문제로 알립니다.
TEST(SideRegions, MisalignedOrOutsideRegionIsReported) {
  const SideRegions regions{{1, 0, 640, 1080}, {1300, 0, 640, 1080}};

  const auto findings = CheckSideRegions(regions, PixelFormat::kUyvy, 1920, 1080);

  EXPECT_EQ(findings, (std::vector<SideFinding>{{SideProblem::kNotAligned, Side::kLeft},
                                                {SideProblem::kNotAligned, Side::kRight}}));
}

// 화소 왼쪽 영역이 오른쪽 영역보다 오른쪽에 있으면 순서 문제이고, 같은 화소를 포함하면 교차 문제입니다.
TEST(SideRegions, OrderAndIntersectionAreReported) {
  const SideRegions swapped{{960, 0, 640, 1080}, {320, 0, 640, 1080}};
  EXPECT_EQ(CheckSideRegions(swapped, PixelFormat::kUyvy, 1920, 1080),
            (std::vector<SideFinding>{{SideProblem::kWrongOrder, Side::kLeft}, {SideProblem::kWrongOrder, Side::kRight}}));

  const SideRegions crossing{{320, 0, 642, 1080}, {960, 0, 640, 1080}};
  EXPECT_EQ(CheckSideRegions(crossing, PixelFormat::kUyvy, 1920, 1080),
            (std::vector<SideFinding>{{SideProblem::kIntersecting, Side::kLeft},
                                      {SideProblem::kIntersecting, Side::kRight}}));
}

// 맞닿은 두 영역(기본값)은 교차가 아닙니다.
TEST(SideRegions, TouchingRegionsDoNotIntersect) {
  const SideRegions regions{{0, 0, 320, 480}, {320, 0, 320, 480}};

  EXPECT_TRUE(CheckSideRegions(regions, PixelFormat::kY16, 640, 480).empty());
}

}  // namespace
