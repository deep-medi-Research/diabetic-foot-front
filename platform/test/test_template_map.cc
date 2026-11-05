#include "hwlab/platform/template_map.h"

#include <gtest/gtest.h>

#include <string>

using hwlab::platform::DrawTemplateMap;
using hwlab::platform::NpyArray;
using hwlab::platform::PerfusionDetail;
using hwlab::platform::ReadPerfusionDetail;
using hwlab::platform::TemplateMapTop;
using hwlab::platform::TurboColor;

namespace {

// 시간 창 1개, 2 x 2 격자, 폭이 길이의 0.4인 사각 윤곽입니다. 왼쪽 위 칸만 계산하지 않습니다.
PerfusionDetail Square(double left_value, double right_value) {
  PerfusionDetail d;
  d.window_centers_s = {5.0};
  d.map_rows = 2;
  d.map_cols = 2;
  d.maps = {NAN, left_value, left_value, left_value, NAN, right_value, right_value, right_value};
  d.map_valid = {0, 1, 1, 1};
  d.outline_u = {0.0, 1.0};
  d.outline_left = {-0.2, -0.2};
  d.outline_right = {0.2, 0.2};
  return d;
}

const uint8_t* Pixel(const hwlab::platform::RgbaImage& image, int32_t x, int32_t y) {
  return image.rgba.data() + (static_cast<size_t>(y) * image.width + x) * 4;
}

TEST(TurboColor, RunsFromBlueToRed) {
  const auto low = TurboColor(0.02, 0.2);
  const auto high = TurboColor(0.2, 0.2);
  EXPECT_GT(low[2], low[0]);
  EXPECT_GT(high[0], high[2]);
  EXPECT_EQ(TurboColor(0.5, 0.2), high);  // 위끝보다 큰 값은 위끝 색
  EXPECT_EQ(TurboColor(NAN, 0.2), (std::array<uint8_t, 3>{128, 128, 128}));
  EXPECT_EQ(TurboColor(0.1, 0.0), (std::array<uint8_t, 3>{128, 128, 128}));
}

TEST(DrawTemplateMap, PlacesLeftFootLeftAndMirrorsRightFoot) {
  const PerfusionDetail d = Square(0.0, 0.2);
  const auto image = DrawTemplateMap(d, 0, 51, 0.2);
  ASSERT_TRUE(image);
  // 발 폭은 0.4 * 50 + 1 = 21화소, 두 발 사이는 4화소입니다.
  EXPECT_EQ(image->height, 51);
  EXPECT_EQ(image->width, 21 * 2 + 4);
  EXPECT_EQ(Pixel(*image, 22, 25)[3], 0);  // 두 발 사이는 투명
  // 왼발 왼쪽 위는 계산하지 않는 칸, 오른쪽 아래는 0.0의 색
  const auto gap = Pixel(*image, 2, 2);
  EXPECT_EQ((std::array<uint8_t, 3>{gap[0], gap[1], gap[2]}), (std::array<uint8_t, 3>{34, 38, 44}));
  const auto low = TurboColor(0.0, 0.2);
  const auto left = Pixel(*image, 18, 48);
  EXPECT_EQ((std::array<uint8_t, 3>{left[0], left[1], left[2]}), low);
  // 오른발은 좌우반전이라 계산하지 않는 칸이 오른쪽 위에 옵니다.
  const auto right_gap = Pixel(*image, image->width - 3, 2);
  EXPECT_EQ((std::array<uint8_t, 3>{right_gap[0], right_gap[1], right_gap[2]}), (std::array<uint8_t, 3>{34, 38, 44}));
  const auto right = Pixel(*image, 27, 48);
  EXPECT_EQ((std::array<uint8_t, 3>{right[0], right[1], right[2]}), TurboColor(0.2, 0.2));
}

TEST(DrawTemplateMap, RejectsMissingValuesAndBadWindow) {
  PerfusionDetail d = Square(0.1, 0.1);
  EXPECT_FALSE(DrawTemplateMap(d, 1, 51, 0.2));
  EXPECT_FALSE(DrawTemplateMap(d, 0, 1, 0.2));
  d.outline_u.clear();
  EXPECT_FALSE(DrawTemplateMap(d, 0, 51, 0.2));
  EXPECT_FALSE(TemplateMapTop(d));
}

TEST(TemplateMapTop, UsesValidCellsOnly) {
  PerfusionDetail d = Square(0.1, 0.3);
  d.maps[0] = 9.0;  // 계산하지 않는 칸의 값은 보지 않습니다
  const auto top = TemplateMapTop(d);
  ASSERT_TRUE(top);
  EXPECT_DOUBLE_EQ(*top, 0.3);
}

TEST(DrawTemplateMap, DrawsV8Result) {
  std::string error;
  const auto d = ReadPerfusionDetail(std::string(HWLAB_TEST_DATA_DIR) + "/160307_v8_perfusion.npz", &error);
  ASSERT_TRUE(d) << error;
  const auto top = TemplateMapTop(*d);
  ASSERT_TRUE(top);
  // numpy로 구한 같은 값: 모든 창과 두 발의 계산 칸 11396개 중 95번째 백분위수
  EXPECT_NEAR(*top, 0.22829335615099622, 1e-12);
  const auto image = DrawTemplateMap(*d, 5, 236, *top);
  ASSERT_TRUE(image);
  EXPECT_EQ(image->height, 236);
  size_t painted = 0;
  for (size_t i = 3; i < image->rgba.size(); i += 4) painted += image->rgba[i] != 0;
  // 윤곽 안 화소가 그림의 절반쯤입니다(발 모양이 사각형보다 좁음).
  EXPECT_GT(painted, image->rgba.size() / 4 / 3);
  EXPECT_LT(painted, image->rgba.size() / 4 * 3 / 4);
}

}  // namespace
