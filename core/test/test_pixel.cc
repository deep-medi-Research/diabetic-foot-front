#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "hwlab/pixel/brightness.h"
#include "hwlab/pixel/crop.h"
#include "hwlab/pixel/rect.h"

namespace {

using hwlab::FrameView;
using hwlab::PixelFormat;
using hwlab::pixel::Brightness;
using hwlab::pixel::CopyRect;
using hwlab::pixel::FitToGrid;
using hwlab::pixel::IsAligned;
using hwlab::pixel::MeanOf;
using hwlab::pixel::MeasureBrightness;
using hwlab::pixel::PackedBytes;
using hwlab::pixel::Rect;
using hwlab::pixel::SplitLeftRight;

// 화소 (x, y)의 Y를 x + 10 * y로 채운 UYVY 프레임입니다. 행 끝에 빈 바이트 pad를 둡니다.
struct UyvyFrame {
  std::vector<uint8_t> bytes;
  FrameView view;
};

UyvyFrame MakeUyvy(int32_t width, int32_t height, int32_t pad) {
  UyvyFrame frame;
  const int32_t stride = width * 2 + pad;
  frame.bytes.assign(static_cast<size_t>(stride) * height, 0xEE);
  for (int32_t y = 0; y < height; ++y) {
    for (int32_t x = 0; x < width; ++x) {
      uint8_t* pair = &frame.bytes[static_cast<size_t>(y) * stride + (x / 2) * 4];
      pair[x % 2 == 0 ? 1 : 3] = static_cast<uint8_t>(x + 10 * y);
      pair[x % 2 == 0 ? 0 : 2] = static_cast<uint8_t>(x % 2 == 0 ? 128 : 129);
    }
  }
  frame.view.format = PixelFormat::kUyvy;
  frame.view.width = width;
  frame.view.height = height;
  frame.view.plane[0] = frame.bytes.data();
  frame.view.row_stride[0] = stride;
  return frame;
}

// 색차 격자 밖으로 나간 시작점은 안쪽으로 올리고 끝은 내립니다.
TEST(Rect, FitToGridMovesInwardForNv12) {
  const Rect fitted = FitToGrid({1, 1, 6, 6}, PixelFormat::kNv12, 16, 16);

  EXPECT_EQ(fitted, (Rect{2, 2, 4, 4}));
}

// 프레임을 벗어난 부분은 잘라냅니다.
TEST(Rect, FitToGridClipsToFrame) {
  const Rect fitted = FitToGrid({-4, 10, 30, 30}, PixelFormat::kUyvy, 16, 16);

  EXPECT_EQ(fitted, (Rect{0, 10, 16, 6}));
}

// 열화상 원시값은 화소마다 독립이라 홀수 좌표도 그대로 둡니다.
TEST(Rect, Y16KeepsOddCoordinates) {
  EXPECT_TRUE(IsAligned({3, 5, 7, 9}, PixelFormat::kY16, 640, 480));
  EXPECT_FALSE(IsAligned({3, 5, 7, 9}, PixelFormat::kUyvy, 640, 480));
}

// UYVY 밝기는 U, V가 아니라 Y만 읽고, 행 끝 빈 바이트는 읽지 않습니다.
TEST(Brightness, ReadsOnlyLumaFromUyvy) {
  const UyvyFrame frame = MakeUyvy(8, 4, 6);

  const Brightness b = MeasureBrightness(frame.view, {1, 1, 3, 2}, 255);

  // Y = x + 10y, x는 1..3, y는 1..2
  EXPECT_EQ(b.pixel_count, 6);
  EXPECT_EQ(b.sum, (11 + 12 + 13) + (21 + 22 + 23));
  EXPECT_DOUBLE_EQ(MeanOf(b), 17.0);
}

// 설정 변경 직후 블랙 레벨이 모든 화소에서 약 7 오른 것을 평균으로 구분합니다.
TEST(Brightness, DetectsBlackLevelOffsetOfSeven) {
  UyvyFrame dark = MakeUyvy(16, 8, 0);
  UyvyFrame lifted = MakeUyvy(16, 8, 0);
  // Y 최댓값이 15 + 70이라 7을 더해도 255를 넘지 않습니다.
  for (size_t i = 1; i < lifted.bytes.size(); i += 2) {
    lifted.bytes[i] = static_cast<uint8_t>(lifted.bytes[i] + 7);
  }
  const Rect whole{0, 0, 16, 8};

  const double lifted_mean = MeanOf(MeasureBrightness(lifted.view, whole, 255));
  const double dark_mean = MeanOf(MeasureBrightness(dark.view, whole, 255));

  EXPECT_DOUBLE_EQ(lifted_mean - dark_mean, 7.0);
}

// clip_level 이상인 화소만 셉니다.
TEST(Brightness, CountsClippedPixels) {
  const UyvyFrame frame = MakeUyvy(8, 4, 0);

  // Y가 30 이상인 화소는 y = 3 행의 8개입니다.
  const Brightness b = MeasureBrightness(frame.view, {0, 0, 8, 4}, 30);

  EXPECT_EQ(b.clipped_pixels, 8);
}

// 열화상 원시값은 리틀 엔디언 16비트로 읽습니다.
TEST(Brightness, ReadsLittleEndianY16) {
  const std::vector<uint8_t> bytes{0x34, 0x12, 0xFF, 0xFF};
  FrameView view;
  view.format = PixelFormat::kY16;
  view.width = 2;
  view.height = 1;
  view.plane[0] = bytes.data();
  view.row_stride[0] = 4;

  const Brightness b = MeasureBrightness(view, {0, 0, 2, 1}, 0xFFFF);

  EXPECT_EQ(b.sum, 0x1234 + 0xFFFF);
  EXPECT_EQ(b.clipped_pixels, 1);
}

// 프레임을 벗어난 영역은 재지 않습니다.
TEST(Brightness, RejectsRectOutsideFrame) {
  const UyvyFrame frame = MakeUyvy(8, 4, 0);

  const Brightness b = MeasureBrightness(frame.view, {6, 0, 4, 4}, 255);

  EXPECT_EQ(b.pixel_count, 0);
  EXPECT_DOUBLE_EQ(MeanOf(b), 0.0);
}

// 잘라낸 UYVY는 행 사이 빈 바이트 없이 원래 바이트 순서 그대로 담깁니다.
TEST(Crop, CopiesUyvyRowsWithoutPadding) {
  const UyvyFrame frame = MakeUyvy(8, 4, 6);
  const Rect rect{2, 1, 4, 2};
  std::vector<uint8_t> out(PackedBytes(PixelFormat::kUyvy, rect));

  ASSERT_TRUE(CopyRect(frame.view, rect, out));

  ASSERT_EQ(out.size(), 16u);
  const std::vector<uint8_t> expected{128, 12, 129, 13, 128, 14, 129, 15,
                                      128, 22, 129, 23, 128, 24, 129, 25};
  EXPECT_EQ(out, expected);
}

// NV12는 Y 행 뒤에 절반 높이의 UV 행이 붙습니다.
TEST(Crop, CopiesNv12PlanesInOrder) {
  const int32_t width = 4;
  const int32_t height = 4;
  std::vector<uint8_t> luma(width * height);
  for (size_t i = 0; i < luma.size(); ++i) {
    luma[i] = static_cast<uint8_t>(i);
  }
  const std::vector<uint8_t> chroma{100, 101, 102, 103, 110, 111, 112, 113};
  FrameView view;
  view.format = PixelFormat::kNv12;
  view.width = width;
  view.height = height;
  view.plane[0] = luma.data();
  view.plane[1] = chroma.data();
  view.row_stride[0] = width;
  view.row_stride[1] = width;
  const Rect rect{2, 2, 2, 2};
  std::vector<uint8_t> out(PackedBytes(PixelFormat::kNv12, rect));

  ASSERT_TRUE(CopyRect(view, rect, out));

  const std::vector<uint8_t> expected{10, 11, 14, 15, 112, 113};
  EXPECT_EQ(out, expected);
}

// 색차 격자에 맞지 않는 영역은 자르지 않습니다. U, V 순서가 바뀌기 때문입니다.
TEST(Crop, RefusesOddUyvyStart) {
  const UyvyFrame frame = MakeUyvy(8, 4, 0);
  const Rect rect{1, 0, 2, 1};
  std::vector<uint8_t> out(PackedBytes(PixelFormat::kUyvy, rect));

  EXPECT_FALSE(CopyRect(frame.view, rect, out));
}

// 출력 버퍼 크기가 맞지 않으면 쓰지 않습니다.
TEST(Crop, RefusesWrongOutputSize) {
  const UyvyFrame frame = MakeUyvy(8, 4, 0);
  std::vector<uint8_t> out(3);

  EXPECT_FALSE(CopyRect(frame.view, {0, 0, 2, 1}, out));
}

// 두 영역은 폭이 같고, 가운데 선에서 맞닿습니다.
TEST(Split, KeepsMiddleLineAndEqualWidths) {
  const auto [left, right] = SplitLeftRight({100, 50, 1000, 400}, PixelFormat::kUyvy, 32);

  EXPECT_EQ(left.x + left.width, 600);
  EXPECT_EQ(right.x, 600);
  EXPECT_EQ(left.width, right.width);
  EXPECT_EQ(left.width % 32, 0);
  EXPECT_LE(left.x, 600 - 480);
  EXPECT_GE(left.x, 100);
  EXPECT_EQ(right.y, 50);
  EXPECT_EQ(right.height, 400);
}

// 줄인 결과가 원래 영역을 벗어나지 않습니다.
TEST(Split, StaysInsideOriginalRect) {
  const Rect rect{10, 0, 202, 10};

  const auto [left, right] = SplitLeftRight(rect, PixelFormat::kUyvy, 32);

  EXPECT_GE(left.x, rect.x);
  EXPECT_LE(right.x + right.width, rect.x + rect.width);
  EXPECT_EQ(left.width, 96);
}

// 반폭이 width_step보다 좁으면 나누지 않습니다.
TEST(Split, ReturnsEmptyWhenTooNarrow) {
  const auto [left, right] = SplitLeftRight({0, 0, 40, 10}, PixelFormat::kUyvy, 32);

  EXPECT_EQ(left, Rect{});
  EXPECT_EQ(right, Rect{});
}

// width_step이 색차 격자의 배수가 아니면 나누지 않습니다.
TEST(Split, RejectsStepOffChromaGrid) {
  const auto [left, right] = SplitLeftRight({0, 0, 200, 10}, PixelFormat::kUyvy, 31);

  EXPECT_EQ(left, Rect{});
}

// 높이가 폭보다 커야 세로형입니다. 정사각형과 빈 영역은 세로형이 아닙니다.
TEST(Rect, IsPortraitRequiresHeightAboveWidth) {
  EXPECT_TRUE(hwlab::pixel::IsPortrait(Rect{0, 0, 960, 1080}));
  EXPECT_FALSE(hwlab::pixel::IsPortrait(Rect{0, 0, 1920, 1080}));
  EXPECT_FALSE(hwlab::pixel::IsPortrait(Rect{0, 0, 540, 540}));
  EXPECT_FALSE(hwlab::pixel::IsPortrait(Rect{0, 0, 0, 1080}));
}

}  // namespace
