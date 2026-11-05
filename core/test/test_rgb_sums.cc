#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "hwlab/pixel/convert.h"
#include "hwlab/pixel/rgb_sums.h"

namespace {

using hwlab::FrameView;
using hwlab::PixelFormat;
using hwlab::pixel::MeasureRgb;
using hwlab::pixel::Rect;
using hwlab::pixel::RgbSums;

// UYVY 프레임과 그 바이트입니다. 행 끝에 빈 바이트를 두어 행 간격을 따르는지도 봅니다.
struct UyvyFrame {
  std::vector<uint8_t> bytes;
  FrameView view;
};

constexpr int32_t kPadBytes = 8;

UyvyFrame MakeUyvy(int32_t width, int32_t height, uint32_t seed) {
  UyvyFrame f;
  const int32_t stride = width * 2 + kPadBytes;
  f.bytes.resize(static_cast<size_t>(stride) * static_cast<size_t>(height));
  // 같은 값이 반복되지 않게 선형 합동 수열로 채웁니다. 0~255 전 범위가 나와 위아래로 잘리는 화소도 생깁니다.
  for (uint8_t& b : f.bytes) {
    seed = seed * 1664525u + 1013904223u;
    b = static_cast<uint8_t>(seed >> 24);
  }
  f.view.format = PixelFormat::kUyvy;
  f.view.width = width;
  f.view.height = height;
  f.view.plane[0] = f.bytes.data();
  f.view.row_stride[0] = stride;
  return f;
}

UyvyFrame MakeFlat(int32_t width, int32_t height, uint8_t u, uint8_t y, uint8_t v) {
  UyvyFrame f;
  for (int32_t i = 0; i < width / 2 * height; ++i) {
    f.bytes.insert(f.bytes.end(), {u, y, v, y});
  }
  f.view.format = PixelFormat::kUyvy;
  f.view.width = width;
  f.view.height = height;
  f.view.plane[0] = f.bytes.data();
  f.view.row_stride[0] = width * 2;
  return f;
}

// 미리보기 변환(step 1)으로 만든 RGB에서 영역의 step 화소마다 직접 더한 값입니다.
RgbSums SumFromPreview(const FrameView& view, const Rect& rect, int32_t step) {
  std::vector<uint8_t> rgb(static_cast<size_t>(view.width) * static_cast<size_t>(view.height) * 3);
  EXPECT_TRUE(hwlab::pixel::UyvyToRgbPreview(view, 1, rgb));
  RgbSums out;
  for (int32_t y = rect.y; y < rect.y + rect.height; y += step) {
    for (int32_t x = rect.x; x < rect.x + rect.width; x += step) {
      const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(view.width) + static_cast<size_t>(x)) * 3;
      for (size_t c = 0; c < 3; ++c) {
        out.sum[c] += rgb[i + c];
        out.saturated[c] += rgb[i + c] == 255 ? 1 : 0;
      }
      ++out.pixel_count;
    }
  }
  return out;
}

void ExpectSame(const RgbSums& a, const RgbSums& b) {
  EXPECT_EQ(a.pixel_count, b.pixel_count);
  EXPECT_EQ(a.sum, b.sum);
  EXPECT_EQ(a.saturated, b.saturated);
}

// 모든 화소를 읽으면 미리보기 RGB를 더한 값과 같습니다. 화면 색과 패널 숫자가 같은 계산이라는 계약입니다.
TEST(RgbSums, MatchesPreviewConversion) {
  const UyvyFrame f = MakeUyvy(16, 6, 1);
  const Rect whole{0, 0, 16, 6};

  const RgbSums sums = MeasureRgb(f.view, whole, 1);

  EXPECT_EQ(sums.pixel_count, 16 * 6);
  ExpectSame(sums, SumFromPreview(f.view, whole, 1));
  EXPECT_GT(sums.saturated[0] + sums.saturated[1] + sums.saturated[2], 0);
}

// 홀수 시작 위치와 step 2, 3도 미리보기 RGB에서 같은 화소를 고른 값과 같습니다.
TEST(RgbSums, StepAndOddStartPickSamePixels) {
  const UyvyFrame f = MakeUyvy(20, 9, 7);
  for (const Rect& rect : {Rect{1, 1, 13, 7}, Rect{3, 0, 16, 9}, Rect{0, 2, 1, 1}}) {
    for (const int32_t step : {1, 2, 3}) {
      ExpectSame(MeasureRgb(f.view, rect, step), SumFromPreview(f.view, rect, step));
    }
  }
  // 13 x 7 영역을 step 2로 읽으면 가로 7개, 세로 4개입니다.
  EXPECT_EQ(MeasureRgb(f.view, {1, 1, 13, 7}, 2).pixel_count, 7 * 4);
}

// 색차가 없는 Y 235는 흰색이라 세 채널이 모두 포화이고, Y 16은 검정이라 포화가 없습니다.
TEST(RgbSums, CountsSaturatedPerChannel) {
  const UyvyFrame white = MakeFlat(4, 2, 128, 235, 128);
  const RgbSums w = MeasureRgb(white.view, {0, 0, 4, 2}, 1);
  EXPECT_EQ(w.sum, (std::array<int64_t, 3>{255 * 8, 255 * 8, 255 * 8}));
  EXPECT_EQ(w.saturated, (std::array<int64_t, 3>{8, 8, 8}));

  // V가 크면 R만 255에 닿습니다. Y 180, U 128, V 240: R = (298 * 164 + 409 * 112 + 128) >> 8 > 255
  const UyvyFrame red = MakeFlat(4, 2, 128, 180, 240);
  const RgbSums r = MeasureRgb(red.view, {0, 0, 4, 2}, 1);
  EXPECT_EQ(r.saturated, (std::array<int64_t, 3>{8, 0, 0}));
  EXPECT_EQ(r.sum[0], 255 * 8);
  EXPECT_LT(r.sum[1], 255 * 8);

  const UyvyFrame black = MakeFlat(4, 2, 128, 16, 128);
  const RgbSums b = MeasureRgb(black.view, {0, 0, 4, 2}, 1);
  EXPECT_EQ(b.sum, (std::array<int64_t, 3>{0, 0, 0}));
  EXPECT_EQ(b.saturated, (std::array<int64_t, 3>{0, 0, 0}));
}

// 형식, 영역, step이 맞지 않으면 화소 수가 0입니다.
TEST(RgbSums, RejectsWrongInput) {
  UyvyFrame f = MakeUyvy(8, 4, 3);
  EXPECT_EQ(MeasureRgb(f.view, {0, 0, 9, 4}, 1).pixel_count, 0);
  EXPECT_EQ(MeasureRgb(f.view, {-1, 0, 4, 4}, 1).pixel_count, 0);
  EXPECT_EQ(MeasureRgb(f.view, {0, 0, 0, 4}, 1).pixel_count, 0);
  EXPECT_EQ(MeasureRgb(f.view, {0, 0, 8, 4}, 0).pixel_count, 0);
  f.view.format = PixelFormat::kY16;
  EXPECT_EQ(MeasureRgb(f.view, {0, 0, 8, 4}, 1).pixel_count, 0);
}

}  // namespace
