#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "hwlab/pixel/brightness.h"
#include "hwlab/pixel/convert.h"
#include "hwlab/pixel/crop.h"

namespace {

using hwlab::FrameView;
using hwlab::PixelFormat;
using hwlab::pixel::FindPercentileRange;
using hwlab::pixel::FindRange;
using hwlab::pixel::Gray8ToUyvy;
using hwlab::pixel::NextPalette;
using hwlab::pixel::Palette;
using hwlab::pixel::PaletteColor;
using hwlab::pixel::Rgb8;
using hwlab::pixel::Y16Range;
using hwlab::pixel::Y16ToGray8;
using hwlab::pixel::Y16ToRgbPreview;

// 열화상 640x480처럼 화소 2바이트 리틀 엔디언인 원시값 프레임입니다.
struct Y16Frame {
  std::vector<uint8_t> bytes;
  FrameView view;
};

Y16Frame MakeY16(int32_t width, int32_t height, const std::vector<uint16_t>& values) {
  Y16Frame f;
  for (const uint16_t v : values) {
    f.bytes.push_back(static_cast<uint8_t>(v & 0xFF));
    f.bytes.push_back(static_cast<uint8_t>(v >> 8));
  }
  f.view.format = PixelFormat::kY16;
  f.view.width = width;
  f.view.height = height;
  f.view.plane[0] = f.bytes.data();
  f.view.row_stride[0] = width * 2;
  return f;
}

// 원시값 범위를 찾고 그 구간을 0~255로 폅니다.
TEST(Convert, StretchesRangeToGray) {
  const Y16Frame f = MakeY16(4, 1, {7000, 7100, 7200, 7050});

  const Y16Range range = FindRange(f.view, {0, 0, 4, 1});
  std::vector<uint8_t> gray(4);
  ASSERT_TRUE(Y16ToGray8(f.view, range, gray));

  EXPECT_EQ(range.low, 7000);
  EXPECT_EQ(range.high, 7200);
  EXPECT_EQ(gray, (std::vector<uint8_t>{0, 128, 255, 64}));
}

// 범위 밖 값은 양 끝으로 자릅니다.
TEST(Convert, ClampsOutsideRange) {
  const Y16Frame f = MakeY16(2, 1, {100, 60000});
  std::vector<uint8_t> gray(2);

  ASSERT_TRUE(Y16ToGray8(f.view, {1000, 2000}, gray));

  EXPECT_EQ(gray, (std::vector<uint8_t>{0, 255}));
}

// 모든 값이 같으면 중간 회색입니다.
TEST(Convert, FlatFrameIsMidGray) {
  const Y16Frame f = MakeY16(2, 1, {5000, 5000});
  std::vector<uint8_t> gray(2);

  ASSERT_TRUE(Y16ToGray8(f.view, FindRange(f.view, {0, 0, 2, 1}), gray));

  EXPECT_EQ(gray, (std::vector<uint8_t>{128, 128}));
}

// 회색을 UYVY로 바꾸면 Y만 남고 색차는 128이라, 밝기 측정에서 같은 값이 나옵니다.
TEST(Convert, GrayToUyvyKeepsLuma) {
  const std::vector<uint8_t> gray{10, 20, 30, 40};
  std::vector<uint8_t> uyvy(8);

  ASSERT_TRUE(Gray8ToUyvy(gray, 2, 2, uyvy));

  EXPECT_EQ(uyvy, (std::vector<uint8_t>{128, 10, 128, 20, 128, 30, 128, 40}));
  FrameView v;
  v.format = PixelFormat::kUyvy;
  v.width = 2;
  v.height = 2;
  v.plane[0] = uyvy.data();
  v.row_stride[0] = 4;
  EXPECT_EQ(hwlab::pixel::MeasureBrightness(v, {0, 0, 2, 2}, 255).sum, 100);
}

// raw16 저장은 기존 자르기로 행 사이 빈 바이트 없이 원시값을 그대로 담습니다.
TEST(Convert, Raw16CopyKeepsRawBytes) {
  const Y16Frame f = MakeY16(2, 2, {1, 2, 3, 0xABCD});
  std::vector<uint8_t> out(hwlab::pixel::PackedBytes(PixelFormat::kY16, {0, 0, 2, 2}));

  ASSERT_TRUE(hwlab::pixel::CopyRect(f.view, {0, 0, 2, 2}, out));

  EXPECT_EQ(out, f.bytes);
}

// 미리보기 변환: 색차가 없는 Y 16과 235는 검정과 흰색이고, 2화소마다 하나씩 골라 크기가 절반이 됩니다.
TEST(Convert, UyvyPreviewMapsBlackAndWhite) {
  // 같은 행 두 줄입니다. 2화소마다 고르면 2x1이 됩니다.
  const std::vector<uint8_t> uyvy{128, 16, 128, 16, 128, 235, 128, 235, 128, 16, 128, 16, 128, 235, 128, 235};
  FrameView v;
  v.format = PixelFormat::kUyvy;
  v.width = 4;
  v.height = 2;
  v.plane[0] = uyvy.data();
  v.row_stride[0] = 8;
  std::vector<uint8_t> rgb(2 * 1 * 3);

  ASSERT_TRUE(hwlab::pixel::UyvyToRgbPreview(v, 2, rgb));

  EXPECT_EQ(rgb, (std::vector<uint8_t>{0, 0, 0, 255, 255, 255}));
}

// 평면 4:2:2로 옮기면 Y, U, V가 제자리에 가고, 평면의 행 간격이 넓어도 됩니다.
TEST(Convert, UyvyToPlanarSplitsComponents) {
  const std::vector<uint8_t> uyvy{10, 1, 20, 2, 11, 3, 21, 4};  // U0 Y0 V0 Y1 U1 Y2 V1 Y3
  FrameView v;
  v.format = PixelFormat::kUyvy;
  v.width = 4;
  v.height = 1;
  v.plane[0] = uyvy.data();
  v.row_stride[0] = 8;
  std::vector<uint8_t> y(8, 0), u(4, 0), w(4, 0);

  ASSERT_TRUE(hwlab::pixel::UyvyToPlanar422(v, y.data(), 8, u.data(), 4, w.data(), 4));

  EXPECT_EQ(y, (std::vector<uint8_t>{1, 2, 3, 4, 0, 0, 0, 0}));
  EXPECT_EQ(u, (std::vector<uint8_t>{10, 11, 0, 0}));
  EXPECT_EQ(w, (std::vector<uint8_t>{20, 21, 0, 0}));
}

// 형식이나 크기가 맞지 않으면 쓰지 않습니다.
TEST(Convert, RejectsWrongInput) {
  const Y16Frame f = MakeY16(2, 1, {1, 2});
  std::vector<uint8_t> small(1);
  std::vector<uint8_t> uyvy(6);

  EXPECT_FALSE(Y16ToGray8(f.view, {0, 10}, small));
  EXPECT_FALSE(Gray8ToUyvy(std::vector<uint8_t>(3), 3, 1, uyvy));
}

// 회색 색상표를 step 1로 쓰면 R, G, B가 모두 Y16ToGray8 결과와 같습니다.
TEST(Convert, GrayPreviewMatchesGray8) {
  std::vector<uint16_t> values;
  for (int32_t i = 0; i < 6 * 4; ++i) values.push_back(static_cast<uint16_t>(6900 + i * 17));
  const Y16Frame f = MakeY16(6, 4, values);
  const Y16Range range{7000, 7200};
  std::vector<uint8_t> gray(6 * 4);
  std::vector<uint8_t> rgb(6 * 4 * 3);

  ASSERT_TRUE(Y16ToGray8(f.view, range, gray));
  ASSERT_TRUE(Y16ToRgbPreview(f.view, range, Palette::kGray, 1, rgb));

  for (size_t i = 0; i < gray.size(); ++i) {
    EXPECT_EQ(rgb[i * 3 + 0], gray[i]) << i;
    EXPECT_EQ(rgb[i * 3 + 1], gray[i]) << i;
    EXPECT_EQ(rgb[i * 3 + 2], gray[i]) << i;
  }
}

// kIron의 양 끝은 검정과 흰색이고, 구간이 한 값이면 0과 255 사이 색입니다.
TEST(Convert, IronPaletteEndsAreBlackAndWhite) {
  const Y16Frame f = MakeY16(3, 1, {7000, 7200, 9000});
  std::vector<uint8_t> rgb(3 * 3);

  ASSERT_TRUE(Y16ToRgbPreview(f.view, {7000, 7200}, Palette::kIron, 1, rgb));

  EXPECT_EQ(rgb, (std::vector<uint8_t>{0, 0, 0, 255, 255, 255, 255, 255, 255}));

  std::vector<uint8_t> flat(3 * 3);
  ASSERT_TRUE(Y16ToRgbPreview(f.view, {7000, 7000}, Palette::kIron, 1, flat));
  EXPECT_EQ(flat[0], flat[3]);
  EXPECT_NE(flat, (std::vector<uint8_t>(9, 0)));
  EXPECT_NE(flat, (std::vector<uint8_t>(9, 255)));
}

// step 화소마다 하나씩 골라 출력 크기가 (width / step) x (height / step)입니다. 나머지 화소는 버립니다.
TEST(Convert, Y16PreviewStepSetsSize) {
  // 5x3에서 step 2이면 2x1이고, 고르는 화소는 (0, 0)과 (2, 0)입니다.
  const Y16Frame f = MakeY16(5, 3, {0, 1, 100, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1});
  std::vector<uint8_t> rgb(2 * 1 * 3);

  ASSERT_TRUE(Y16ToRgbPreview(f.view, {0, 100}, Palette::kGray, 2, rgb));

  EXPECT_EQ(rgb, (std::vector<uint8_t>{0, 0, 0, 255, 255, 255}));

  std::vector<uint8_t> by3(1 * 1 * 3);
  EXPECT_TRUE(Y16ToRgbPreview(f.view, {0, 100}, Palette::kIron, 3, by3));
}

// 출력 크기, 형식, step이 맞지 않으면 쓰지 않습니다.
TEST(Convert, Y16PreviewRejectsWrongInput) {
  const Y16Frame f = MakeY16(4, 2, std::vector<uint16_t>(8, 5));
  std::vector<uint8_t> full(4 * 2 * 3, 7);
  std::vector<uint8_t> small(4 * 2 * 3 - 1, 7);
  std::vector<uint8_t> half(2 * 1 * 3, 7);

  EXPECT_FALSE(Y16ToRgbPreview(f.view, {0, 10}, Palette::kGray, 1, small));
  EXPECT_FALSE(Y16ToRgbPreview(f.view, {0, 10}, Palette::kGray, 1, half));
  EXPECT_FALSE(Y16ToRgbPreview(f.view, {0, 10}, Palette::kGray, 0, full));
  EXPECT_FALSE(Y16ToRgbPreview(f.view, {0, 10}, Palette::kGray, 5, full));
  FrameView uyvy = f.view;
  uyvy.format = PixelFormat::kUyvy;
  EXPECT_FALSE(Y16ToRgbPreview(uyvy, {0, 10}, Palette::kGray, 1, full));
  EXPECT_EQ(small, std::vector<uint8_t>(small.size(), 7));
  EXPECT_EQ(full, std::vector<uint8_t>(full.size(), 7));
}

// 천분위 범위는 값 순서의 양 끝 일부 화소를 뺍니다. 1000화소 중 이상값 화소 3개를 뺀 범위입니다.
TEST(Convert, PercentileRangeSkipsOutliers) {
  // 40x25 = 1000화소. 값은 7000~7099가 10번씩이고, 3화소는 60000(위쪽 이상값), 3화소는 100(아래쪽 이상값)입니다.
  std::vector<uint16_t> values;
  for (int32_t i = 0; i < 1000; ++i) values.push_back(static_cast<uint16_t>(7000 + i % 100));
  values[10] = 60000;
  values[500] = 60000;
  values[999] = 60000;
  values[20] = 100;
  values[400] = 100;
  values[800] = 100;
  const Y16Frame f = MakeY16(40, 25, values);
  const hwlab::pixel::Rect all{0, 0, 40, 25};

  const Y16Range full = FindPercentileRange(f.view, all, 0, 1000);
  EXPECT_EQ(full.low, FindRange(f.view, all).low);
  EXPECT_EQ(full.high, FindRange(f.view, all).high);

  // 순위 round(999 * p / 1000): 995는 순위 994, 5는 순위 5라 양쪽 3화소가 빠집니다.
  const Y16Range trimmed = FindPercentileRange(f.view, all, 5, 995);
  EXPECT_EQ(trimmed.low, 7000);
  EXPECT_EQ(trimmed.high, 7099);

  // 999는 가장 큰 1화소만 뺍니다. 이상값 화소가 3개면 남은 2개가 위 끝입니다.
  EXPECT_EQ(FindPercentileRange(f.view, all, 1, 999).high, 60000);
}

// 이상값 화소가 1개이면 999천분위가 그 값을 뺍니다.
TEST(Convert, PercentileRangeSkipsSingleHotPixel) {
  std::vector<uint16_t> values(1000, 7000);
  for (int32_t i = 0; i < 1000; ++i) values[i] = static_cast<uint16_t>(7000 + i % 50);
  values[123] = 60000;
  const Y16Frame f = MakeY16(40, 25, values);

  const Y16Range range = FindPercentileRange(f.view, {0, 0, 40, 25}, 0, 999);

  EXPECT_EQ(range.low, 7000);
  EXPECT_EQ(range.high, 7049);
}

// 천분위가 범위 밖이거나 영역이 프레임 밖이면 {0, 0}입니다. 영역 일부만 셉니다.
TEST(Convert, PercentileRangeChecksInput) {
  const Y16Frame f = MakeY16(4, 1, {10, 20, 30, 40});

  EXPECT_EQ(FindPercentileRange(f.view, {0, 0, 4, 1}, -1, 1000).high, 0);
  EXPECT_EQ(FindPercentileRange(f.view, {0, 0, 4, 1}, 0, 1001).high, 0);
  EXPECT_EQ(FindPercentileRange(f.view, {0, 0, 4, 1}, 600, 400).high, 0);
  EXPECT_EQ(FindPercentileRange(f.view, {0, 0, 5, 1}, 0, 1000).high, 0);
  const Y16Range part = FindPercentileRange(f.view, {1, 0, 2, 1}, 0, 1000);
  EXPECT_EQ(part.low, 20);
  EXPECT_EQ(part.high, 30);
}

// 색상표 4종의 양 끝 색은 각 기준점 출처의 첫 색과 마지막 색입니다.
TEST(Convert, PaletteEndColors) {
  struct Case {
    Palette palette;
    Rgb8 low;
    Rgb8 high;
  };
  const Case cases[] = {
      {Palette::kGray, {0, 0, 0}, {255, 255, 255}},
      {Palette::kIron, {0, 0, 0}, {255, 255, 255}},
      {Palette::kRainbow, {0, 0, 128}, {128, 0, 0}},
      {Palette::kHighContrast, {0, 0, 0}, {255, 255, 255}},
  };
  for (const Case& c : cases) {
    EXPECT_EQ(PaletteColor(c.palette, 0), c.low) << static_cast<int>(c.palette);
    EXPECT_EQ(PaletteColor(c.palette, 255), c.high) << static_cast<int>(c.palette);
  }
  // 레인보우의 3/8 지점은 청록, 7/8 지점은 빨강입니다(MATLAB jet).
  EXPECT_EQ(PaletteColor(Palette::kRainbow, 96), (Rgb8{0, 255, 255}));
  EXPECT_EQ(PaletteColor(Palette::kRainbow, 223), (Rgb8{255, 0, 0}));
}

// 미리보기는 PaletteColor와 같은 표를 씁니다. 구간 아래 끝, 위 끝, 구간 밖 값을 칠해 확인합니다.
TEST(Convert, PreviewUsesPaletteTable) {
  const Y16Frame f = MakeY16(4, 1, {6000, 7000, 7200, 9000});
  for (const Palette palette : {Palette::kGray, Palette::kIron, Palette::kRainbow, Palette::kHighContrast}) {
    std::vector<uint8_t> rgb(4 * 3);
    ASSERT_TRUE(Y16ToRgbPreview(f.view, {7000, 7200}, palette, 1, rgb));
    const Rgb8 low = PaletteColor(palette, 0);
    const Rgb8 high = PaletteColor(palette, 255);
    EXPECT_EQ(rgb, (std::vector<uint8_t>{low.r, low.g, low.b, low.r, low.g, low.b, high.r, high.g, high.b, high.r,
                                         high.g, high.b}))
        << static_cast<int>(palette);
  }
}

// 고대비 색상표는 256단계에서 같은 색이 두 번 나오지 않습니다.
TEST(Convert, HighContrastColorsAreDistinct) {
  std::vector<Rgb8> seen;
  for (int32_t level = 0; level < 256; ++level) {
    const Rgb8 color = PaletteColor(Palette::kHighContrast, static_cast<uint8_t>(level));
    for (const Rgb8& other : seen) ASSERT_FALSE(other == color) << level;
    seen.push_back(color);
  }
}

// 영상을 누를 때 아이언, 흑백, 레인보우, 고대비 순서로 바뀌고 고대비 다음은 아이언입니다.
TEST(Convert, NextPaletteCyclesInOrder) {
  EXPECT_EQ(NextPalette(Palette::kIron), Palette::kGray);
  EXPECT_EQ(NextPalette(Palette::kGray), Palette::kRainbow);
  EXPECT_EQ(NextPalette(Palette::kRainbow), Palette::kHighContrast);
  EXPECT_EQ(NextPalette(Palette::kHighContrast), Palette::kIron);
}

}  // namespace
