#include "hwlab/platform/perfusion_detail.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

using hwlab::platform::NpyArray;
using hwlab::platform::NpzContents;
using hwlab::platform::PerfusionDetailOf;
using hwlab::platform::PerfusionDetailPath;
using hwlab::platform::ReadPerfusionDetail;

namespace {

NpyArray Vector(const std::vector<double>& values) {
  return NpyArray{{static_cast<int64_t>(values.size())}, values};
}

// 관류 분석 프로그램 출력과 같은 이름의 배열을 시간 창 3개, 영상 6장 크기로 만듭니다.
NpzContents Minimal() {
  NpzContents c;
  c.arrays["time_s"] = Vector({5.0, 7.0, 9.0});
  c.arrays["template_left"] = Vector({0, 1, 0, -1, 0, 1});
  c.arrays["template_right"] = Vector({0, 1, 0, -1, 0, 1});
  return c;
}

TEST(PerfusionDetailPath, MirrorsGifPath) {
  EXPECT_EQ(PerfusionDetailPath("out", "154007"), "out/154007_perfusion.npz");
  EXPECT_EQ(PerfusionDetailPath("out/", "a"), "out/a_perfusion.npz");
  EXPECT_EQ(PerfusionDetailPath("C:\\out\\", "a"), "C:\\out\\a_perfusion.npz");
}

TEST(PerfusionDetailOf, ReadsRequiredArraysOnly) {
  std::string error;
  const auto d = PerfusionDetailOf(Minimal(), &error);
  ASSERT_TRUE(d) << error;
  EXPECT_EQ(d->window_centers_s, (std::vector<double>{5.0, 7.0, 9.0}));
  EXPECT_EQ(d->template_left.size(), 6u);
  EXPECT_TRUE(std::isnan(d->fps));
  EXPECT_TRUE(d->template_corr.empty());
  EXPECT_TRUE(d->light_ref_count.empty());
  EXPECT_TRUE(d->left_series.empty());
  EXPECT_TRUE(d->right_series.empty());
}

TEST(PerfusionDetailOf, SplitsMetricsByFoot) {
  NpzContents c = Minimal();
  c.arrays["fps"] = NpyArray{{}, {30.0}};
  c.arrays["template_corr"] = Vector({0.9, 0.8, 0.7});
  c.arrays["n_light_ref"] = Vector({6, 0, 6});
  c.arrays["pi_left"] = Vector({0.1, 0.2, 0.3});
  c.arrays["pi_right"] = Vector({0.4, 0.5, 0.6});
  c.arrays["pi_toes_left"] = Vector({1, NAN, 3});
  c.arrays["maps"] = NpyArray{{2, 3, 1, 1}, std::vector<double>(6, 0.0)};
  std::string error;
  const auto d = PerfusionDetailOf(c, &error);
  ASSERT_TRUE(d) << error;
  EXPECT_EQ(d->fps, 30.0);
  EXPECT_EQ(d->template_corr, (std::vector<double>{0.9, 0.8, 0.7}));
  EXPECT_EQ(d->light_ref_count, (std::vector<int32_t>{6, 0, 6}));
  ASSERT_EQ(d->left_series.size(), 2u);
  EXPECT_EQ(d->left_series.at("pi"), (std::vector<double>{0.1, 0.2, 0.3}));
  EXPECT_TRUE(std::isnan(d->left_series.at("pi_toes")[1]));
  ASSERT_EQ(d->right_series.size(), 1u);
  EXPECT_EQ(d->right_series.at("pi"), (std::vector<double>{0.4, 0.5, 0.6}));
}

TEST(PerfusionDetailOf, RejectsMissingOrInconsistentArrays) {
  std::string error;
  NpzContents c = Minimal();
  c.arrays.erase("time_s");
  EXPECT_FALSE(PerfusionDetailOf(c, &error));
  EXPECT_NE(error.find("time_s"), std::string::npos) << error;

  c = Minimal();
  c.arrays.erase("template_right");
  EXPECT_FALSE(PerfusionDetailOf(c, &error));
  EXPECT_NE(error.find("template"), std::string::npos) << error;

  c = Minimal();
  c.arrays["template_right"] = Vector({0, 1, 0});
  EXPECT_FALSE(PerfusionDetailOf(c, &error));
  EXPECT_NE(error.find("템플릿 길이"), std::string::npos) << error;

  c = Minimal();
  c.arrays["pi_left"] = Vector({0.1, 0.2});
  EXPECT_FALSE(PerfusionDetailOf(c, &error));
  EXPECT_NE(error.find("pi_left"), std::string::npos) << error;

  c = Minimal();
  c.arrays["template_corr"] = Vector({0.9});
  EXPECT_FALSE(PerfusionDetailOf(c, &error));
  EXPECT_NE(error.find("template_corr"), std::string::npos) << error;
}

TEST(ReadPerfusionDetail, ReadsAnalyzerOutputFile) {
  std::string error;
  const auto d = ReadPerfusionDetail(std::string(HWLAB_TEST_DATA_DIR) + "/154007_perfusion.npz", &error);
  ASSERT_TRUE(d) << error;
  ASSERT_EQ(d->window_centers_s.size(), 11u);
  EXPECT_EQ(d->window_centers_s.front(), 5.0);
  EXPECT_EQ(d->window_centers_s.back(), 25.0);
  EXPECT_EQ(d->template_left.size(), 900u);
  EXPECT_EQ(d->template_right.size(), 900u);
  EXPECT_EQ(d->fps, 30.0);
  EXPECT_EQ(d->template_corr.size(), 11u);
  EXPECT_NEAR(d->template_corr[0], 0.95329, 1e-5);
  EXPECT_EQ(d->light_ref_count, std::vector<int32_t>(11, 6));
  const std::vector<std::string> keys = {"pi",          "pi_fore",    "pi_green_legacy", "pi_heel", "pi_mid",
                                         "pi_pos_lockin", "pi_pos_std", "pi_ref_std",    "pi_toes"};
  for (const auto* series : {&d->left_series, &d->right_series}) {
    ASSERT_EQ(series->size(), keys.size());
    for (const auto& key : keys) {
      ASSERT_EQ(series->count(key), 1u) << key;
      EXPECT_EQ(series->at(key).size(), 11u) << key;
    }
  }
  EXPECT_NEAR(d->left_series.at("pi")[0], 0.0116510, 1e-6);
  EXPECT_NEAR(d->right_series.at("pi_toes")[10], 0.0192766, 1e-6);
  // 관류 지도: 좌우 2 x 창 11 x 32 x 16 격자. 발 밖은 NaN이고 첫 창 왼발의 7행 11열은 발 안입니다.
  EXPECT_EQ(d->map_rows, 32);
  EXPECT_EQ(d->map_cols, 16);
  EXPECT_EQ(d->maps.size(), 2u * 11u * 32u * 16u);
  EXPECT_TRUE(std::isnan(d->MapAt(0, 0, 0, 0)));
  EXPECT_GT(d->MapAt(0, 0, 7, 11), 0.02);
  EXPECT_TRUE(std::isnan(d->MapAt(2, 0, 7, 11)));
  EXPECT_TRUE(std::isnan(d->MapAt(0, 11, 7, 11)));
}

// v8의 perfusion.npz: 관류 지도가 표준 발 틀 40x15이고, 창별 값은 pi와 부위 네 개뿐입니다. 지도 칸 수(map_valid)와
// 발 윤곽(outline)은 _left/_right로 끝나지 않아 창별 값으로 읽지 않습니다.
TEST(ReadPerfusionDetail, ReadsV8TemplateMaps) {
  std::string error;
  const auto d = ReadPerfusionDetail(std::string(HWLAB_TEST_DATA_DIR) + "/160307_v8_perfusion.npz", &error);
  ASSERT_TRUE(d) << error;
  ASSERT_EQ(d->window_centers_s.size(), 11u);
  EXPECT_EQ(d->template_left.size(), 900u);
  EXPECT_NEAR(d->template_corr[0], 0.5628579592469367, 1e-12);
  EXPECT_EQ(d->light_ref_count, std::vector<int32_t>(11, 6));
  const std::vector<std::string> keys = {"pi", "pi_fore", "pi_heel", "pi_mid", "pi_toes"};
  for (const auto* series : {&d->left_series, &d->right_series}) {
    ASSERT_EQ(series->size(), keys.size());
    for (const auto& key : keys) {
      ASSERT_EQ(series->count(key), 1u) << key;
      EXPECT_EQ(series->at(key).size(), 11u) << key;
    }
  }
  EXPECT_NEAR(d->left_series.at("pi")[0], 0.0376581592247184, 1e-12);
  EXPECT_NEAR(d->right_series.at("pi_toes")[10], 0.12070892124406714, 1e-12);
  EXPECT_EQ(d->map_rows, 40);
  EXPECT_EQ(d->map_cols, 15);
  EXPECT_TRUE(std::isnan(d->MapAt(0, 0, 0, 0)));
  EXPECT_NEAR(d->MapAt(0, 0, 20, 7), 0.030002709958432044, 1e-12);
  // 표준 발 틀: 계산 칸 518개와 윤곽 41점(관류 분석 프로그램 v8 foot_cells.OUTLINE_*)
  ASSERT_TRUE(d->HasTemplateMap());
  ASSERT_EQ(d->map_valid.size(), 600u);
  size_t valid = 0;
  for (const uint8_t v : d->map_valid) valid += v;
  EXPECT_EQ(valid, 518u);
  ASSERT_EQ(d->outline_u.size(), 41u);
  EXPECT_DOUBLE_EQ(d->outline_u.front(), 0.0);
  EXPECT_DOUBLE_EQ(d->outline_u.back(), 1.0);
  EXPECT_NEAR(d->outline_left[20], -0.137, 1e-3);
  EXPECT_NEAR(d->outline_right[20], 0.187, 1e-3);
}

TEST(PerfusionDetailOf, SkipsTemplateOutlineThatDoesNotFitMaps) {
  NpzContents c = Minimal();
  c.arrays["maps"] = NpyArray{{2, 3, 2, 2}, std::vector<double>(24, 0.1)};
  c.arrays["map_valid"] = NpyArray{{3, 2}, std::vector<double>(6, 1)};
  c.arrays["outline"] = NpyArray{{3, 2}, {0, 1, -0.1, -0.1, 0.1, 0.1}};
  std::string error;
  const auto d = PerfusionDetailOf(c, &error);
  ASSERT_TRUE(d) << error;
  EXPECT_TRUE(d->map_valid.empty());
  EXPECT_EQ(d->outline_u.size(), 2u);
  EXPECT_FALSE(d->HasTemplateMap());
}

TEST(ReadPerfusionDetail, ReportsMissingFile) {
  std::string error;
  EXPECT_FALSE(ReadPerfusionDetail(std::string(HWLAB_TEST_DATA_DIR) + "/없는_perfusion.npz", &error));
  EXPECT_FALSE(error.empty());
}

}  // namespace
