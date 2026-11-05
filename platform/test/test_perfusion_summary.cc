#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "hwlab/platform/perfusion_summary.h"
#include "hwlab/platform/shot_review.h"
#include "hwlab/record/analysis_inputs.h"
#include "hwlab/record/sidecar.h"

namespace {

using hwlab::record::FootSide;
using hwlab::record::ShotLayout;
using hwlab::platform::CollectPerfusionResults;
using hwlab::platform::DescribeSpeedupSkip;
using hwlab::platform::DescribeThermalSkip;
using hwlab::platform::IsCollectedResultFile;
using hwlab::platform::PadVerdict;
using hwlab::platform::PadVerdictOf;
using hwlab::platform::PerfusionGifPath;
using hwlab::platform::ReadPerfusionSummary;
using hwlab::platform::ReadRaw16Frame;
using hwlab::platform::ReadShotManifest;
using hwlab::platform::ReadSidecarFrames;

std::string ReadText(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

// 한글 경로를 ANSI 코드 페이지로 바꾸지 않고 엽니다(Windows).
std::string ReadText(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

// 분석결과 앞부분을 만드는 데 쓰는 폴더만 채운 배치입니다.
ShotLayout FolderLayout(const std::string& folder) {
  ShotLayout layout;
  layout.folder = folder;
  return layout;
}

std::string Data(const char* name) { return ReadText(std::string(HWLAB_TEST_DATA_DIR) + "/" + name); }

std::string Replace(std::string text, const std::string& from, const std::string& to) {
  const size_t at = text.find(from);
  EXPECT_NE(at, std::string::npos) << from;
  if (at != std::string::npos) text.replace(at, from.size(), to);
  return text;
}

// 실제 촬영 102528(2026-10-20, 열화상 포함)의 요약: 멀티모달 판정 PAD-like 0.60, 왼발 PI가 더 낮습니다.
TEST(PerfusionSummary, ReadsRealMultimodalSample) {
  std::string error;
  const auto s = ReadPerfusionSummary(Data("102528_summary.json"), &error);
  ASSERT_TRUE(s.has_value()) << error;
  EXPECT_NEAR(s->pi_left, 0.08121029419302435, 1e-12);
  EXPECT_NEAR(s->pi_right, 0.09915272544778957, 1e-12);
  EXPECT_NEAR(s->pi_ratio, 0.8190424804387945, 1e-12);
  EXPECT_EQ(s->lower_foot, FootSide::kLeft);
  ASSERT_TRUE(s->classified);
  EXPECT_NEAR(s->p_pad, 0.6006581783294678, 1e-12);
  EXPECT_NEAR(s->threshold, 0.4475051118604218, 1e-12);
  EXPECT_EQ(s->decision, "PAD-like");
  EXPECT_EQ(s->model, "multimodal (RGB + thermal)");
  EXPECT_EQ(s->affected_foot, FootSide::kLeft);
  EXPECT_EQ(s->window_probs.size(), 5u);
  EXPECT_TRUE(s->thermal_in_verdict);
  EXPECT_EQ(s->thermal_skipped, "");
  EXPECT_EQ(s->window_count, 11);
  EXPECT_EQ(s->window_us, 10'000'000);
  EXPECT_EQ(s->window_stride_us, 2'000'000);
  EXPECT_EQ(s->fps, 30.0);
  EXPECT_EQ(s->frames, 900);
  EXPECT_EQ(s->upstream_commit, "5e5178d3836cdb1f19f0bbf701434ea923bbc5a6");
  EXPECT_EQ(PadVerdictOf(*s), PadVerdict::kPadLike);
  // 부위별 관류지수, 발 인식 품질, 판정 구간 시각입니다.
  EXPECT_NEAR(s->left.pi_toes, 0.08801492118000426, 1e-12);
  EXPECT_NEAR(s->right.pi_heel, 0.07309784076001892, 1e-12);
  EXPECT_EQ(s->left.mask_method, "segmentation");
  EXPECT_NEAR(s->left.detection_score, 0.8087337613105774, 1e-12);
  EXPECT_NEAR(s->right.mask_iou, 0.9942265748977661, 1e-12);
  EXPECT_EQ(s->left.blocks, 152);
  EXPECT_NEAR(s->left.thermal_mask_dice, 0.935, 1e-12);
  EXPECT_NEAR(s->right.thermal_mask_dice, 0.921, 1e-12);
  EXPECT_NEAR(s->template_corr, 0.7033094760293708, 1e-12);
  EXPECT_EQ(s->light_ref_min, 6);
  EXPECT_EQ(s->window_centres_s, (std::vector<double>{5.0, 10.0, 15.0, 20.0, 25.0}));
}

// 같은 촬영을 RGB 판정 모델로 본 요약: normal 0.06, 판정 측 발 null, 관류 분석 프로그램 기록 필드 dfr(worker_version, upstream_commit, thermal_skipped) 없음.
TEST(PerfusionSummary, ReadsRgbOnlySample) {
  const auto s = ReadPerfusionSummary(Data("102528_rgb_only_summary.json"));
  ASSERT_TRUE(s.has_value());
  ASSERT_TRUE(s->classified);
  EXPECT_EQ(s->decision, "normal");
  EXPECT_EQ(s->model, "RGB logistic regression");
  EXPECT_FALSE(s->affected_foot.has_value());
  EXPECT_FALSE(s->thermal_in_verdict);
  EXPECT_EQ(s->thermal_skipped, "");
  EXPECT_EQ(s->upstream_commit, "");
  EXPECT_EQ(PadVerdictOf(*s), PadVerdict::kNormal);
}

// 이진 판정을 끄고 커프 단계만 낸 요약(맥 실측 185205, 2026-10-26, 자동 노출이라 참고용): 판정 없음, 커프 단계 0%.
TEST(PerfusionSummary, ReadsCuffLevelOnlySample) {
  std::string error;
  const auto s = ReadPerfusionSummary(Data("185205_cuff_level_summary.json"), &error);
  ASSERT_TRUE(s.has_value()) << error;
  EXPECT_FALSE(s->classified);  // "classification": null
  EXPECT_EQ(PadVerdictOf(*s), PadVerdict::kNone);
  EXPECT_NEAR(s->pi_left, 0.027625533520584944, 1e-12);
  ASSERT_TRUE(s->cuff_level.has_value());
  const auto& c = *s->cuff_level;
  EXPECT_EQ(c.level_pct, 0);
  EXPECT_NEAR(c.e, 0.8996127605457414, 1e-12);
  EXPECT_NEAR(c.p_ge40, 0.631820766754919, 1e-12);
  EXPECT_NEAR(c.p_ge80, 0.25753951859565205, 1e-12);
  EXPECT_NEAR(c.p_120, 0.01025247519517023, 1e-12);
  ASSERT_EQ(c.cuts.size(), 3u);
  EXPECT_NEAR(c.cuts[0], 1.0604571884017018, 1e-12);
  EXPECT_NEAR(c.cuts[2], 2.4231874752371527, 1e-12);
  EXPECT_FALSE(c.applicable);
  EXPECT_EQ(c.setting_mode, "auto");
  EXPECT_TRUE(std::isnan(c.exposure_step));  // null
  EXPECT_EQ(c.gain, 4);
  EXPECT_TRUE(c.window_e.empty());  // 구간별 값을 내기 전의 요약입니다
  EXPECT_EQ(s->thermal_skipped, "");
}

// 고정 카메라 설정이면 applicable이 true이고, 이전 결과(판정만 있는 요약)에는 커프 단계가 없습니다.
TEST(PerfusionSummary, ReadsApplicableCuffLevelAndOldSummaryWithout) {
  const auto fixed = ReadPerfusionSummary(R"({"pi_left": 0.1, "pi_right": 0.2,
      "cuff_level": {"level_pct_sbp": 80, "E": 2.0, "applicable": true,
                     "setting": {"mode": "manual", "exposure_step": -5, "gain": 1},
                     "window_E": [1.5, "x", 2.5], "window_centres_s": [5.0, 10.0, 15.0]}})");
  ASSERT_TRUE(fixed.has_value());
  ASSERT_TRUE(fixed->cuff_level.has_value());
  EXPECT_EQ(fixed->cuff_level->level_pct, 80);
  EXPECT_TRUE(fixed->cuff_level->applicable);
  EXPECT_EQ(fixed->cuff_level->setting_mode, "manual");
  EXPECT_EQ(fixed->cuff_level->exposure_step, -5);
  EXPECT_TRUE(fixed->cuff_level->cuts.empty());
  // 숫자가 아닌 칸은 NaN으로 두어 시각과의 짝을 지킵니다.
  ASSERT_EQ(fixed->cuff_level->window_e.size(), 3u);
  EXPECT_EQ(fixed->cuff_level->window_e[0], 1.5);
  EXPECT_TRUE(std::isnan(fixed->cuff_level->window_e[1]));
  EXPECT_EQ(fixed->cuff_level->window_centres_s, (std::vector<double>{5.0, 10.0, 15.0}));
  const auto old = ReadPerfusionSummary(Data("102528_summary.json"));
  ASSERT_TRUE(old.has_value());
  EXPECT_FALSE(old->cuff_level.has_value());
  EXPECT_TRUE(old->classified);
  const auto null_level = ReadPerfusionSummary(R"({"pi_left": 0.1, "pi_right": 0.2, "cuff_level": null})");
  ASSERT_TRUE(null_level.has_value());
  EXPECT_FALSE(null_level->cuff_level.has_value());
}

// 경계별 규칙 판정(v7)의 요약(맥 실측 185205, 2026-10-28): 강한 발 맥박 SNR 12.4 dB가 기준 14 dB보다 낮아
// level_pct_sbp가 null입니다. 항목은 있으므로 특징 값, 기준값, 품질 이유를 읽습니다.
TEST(PerfusionSummary, ReadsRuleCuffLevelWithoutLevel) {
  std::string error;
  const auto s = ReadPerfusionSummary(Data("185205_rule_summary.json"), &error);
  ASSERT_TRUE(s.has_value()) << error;
  EXPECT_EQ(s->variant, "v7");
  EXPECT_EQ(s->upstream_commit, "1f47dea7a3a34ad87cb04fa270f6b5d432f0c6fa");
  ASSERT_TRUE(s->cuff_level.has_value());
  const auto& c = *s->cuff_level;
  EXPECT_FALSE(c.has_level);
  EXPECT_FALSE(c.quality_ok);
  EXPECT_EQ(c.level_if_quality_ignored, 0);
  EXPECT_EQ(c.rule, "none of the cuff rules");
  EXPECT_NEAR(c.snr_db, 12.3912, 1e-9);
  EXPECT_NEAR(c.h1re_30, 0.7897, 1e-9);
  EXPECT_NEAR(c.delay2_30_ms, -140.3551, 1e-9);
  EXPECT_NEAR(c.pi_ratio, 0.7219, 1e-9);
  EXPECT_EQ(c.lower_side, "right");
  EXPECT_NEAR(c.h1re_30_below, 0.2639, 1e-12);
  EXPECT_NEAR(c.delay2_30_above_ms, 28.8, 1e-12);
  EXPECT_NEAR(c.pi_ratio_below, 0.64, 1e-12);
  EXPECT_NEAR(c.quality_snr_db, 14.0, 1e-12);
  EXPECT_FALSE(c.applicable);
  EXPECT_EQ(c.gain, 4);
  EXPECT_TRUE(std::isnan(c.e));  // 로지스틱 회귀의 값은 없습니다
  EXPECT_TRUE(c.cuts.empty());
}

// v8의 요약(노트북 측정 커프 40%, 맥에서 v8로 분석, 2026-10-28): 발 분할, 발 셀, 규칙 판정.
// 발 셀 수와 분할 방법을 읽고, 커프 단계는 v7과 같은 키로 읽습니다.
TEST(PerfusionSummary, ReadsV8FootCellSummary) {
  std::string error;
  const auto s = ReadPerfusionSummary(Data("160307_v8_summary.json"), &error);
  ASSERT_TRUE(s.has_value()) << error;
  EXPECT_EQ(s->variant, "v8");
  EXPECT_EQ(s->upstream_commit, "92c1416d962b20423f9d3b0ac140c880c7b6e927");
  EXPECT_NEAR(s->pi_left, 0.03493811350089473, 1e-12);
  EXPECT_NEAR(s->pi_right, 0.10729357930758017, 1e-12);
  EXPECT_EQ(s->left.mask_method, "footseg_dl");
  EXPECT_NEAR(s->left.detection_score, 0.9846497774124146, 1e-12);
  EXPECT_EQ(s->left.cells, 100);
  EXPECT_EQ(s->cells_used, 95);
  EXPECT_EQ(s->cells_common, 95);
  EXPECT_NEAR(s->left.pi_heel, 0.025689333154924866, 1e-12);
  ASSERT_TRUE(s->cuff_level.has_value());
  const auto& c = *s->cuff_level;
  EXPECT_TRUE(c.has_level);
  EXPECT_EQ(c.level_pct, 40);
  EXPECT_EQ(c.rule, "pi_ratio 0.36 < 0.69");
  EXPECT_TRUE(c.quality_ok);
  EXPECT_NEAR(c.pi_ratio, 0.3591, 1e-12);
  EXPECT_NEAR(c.pi_ratio_below, 0.689, 1e-12);
  EXPECT_NEAR(c.quality_snr_db, 11.5, 1e-12);
  EXPECT_FALSE(c.applicable);  // 이 판은 노출 -5만 고정 설정으로 인정했습니다
  EXPECT_FALSE(s->classified);
  EXPECT_FALSE(s->quality.has_value());  // 이 판에는 qc가 없습니다
}

// v8 갱신판으로 같은 촬영을 다시 분석한 요약(맥, 2026-10-29): 측정 품질 안내(qc)와 노출 -9 인정.
TEST(PerfusionSummary, ReadsMeasurementQuality) {
  std::string error;
  const auto s = ReadPerfusionSummary(Data("160307_v8_qc_summary.json"), &error);
  ASSERT_TRUE(s.has_value()) << error;
  ASSERT_TRUE(s->cuff_level.has_value());
  EXPECT_TRUE(s->cuff_level->applicable);
  ASSERT_TRUE(s->quality.has_value());
  const auto& q = *s->quality;
  EXPECT_EQ(q.status, "check");
  EXPECT_EQ(q.status_ko, "측정 완료 (확인 필요)");
  EXPECT_EQ(q.action_ko, "양말을 신고 5~10분 기다린 뒤 측정하세요.");
  ASSERT_EQ(q.items.size(), 8u);
  EXPECT_EQ(q.items[0].code, "camera_setting");
  EXPECT_EQ(q.items[0].value, "manual, 노출 -9, 게인 1");
  EXPECT_EQ(q.items[2].code, "common_cells");
  EXPECT_EQ(q.items[2].value, "95");
  EXPECT_EQ(q.items[2].unit, "개");
  const auto& warmth = q.items[6];
  EXPECT_EQ(warmth.code, "foot_warmth");
  EXPECT_EQ(warmth.severity, "warn");
  EXPECT_EQ(warmth.value, "59 / 66");
  EXPECT_EQ(warmth.reason, "subject");
  EXPECT_EQ(warmth.message_ko, "왼발이 차갑습니다(배경과 온도 차이가 작음). 혈류 신호가 약해질 수 있습니다.");
  EXPECT_EQ(q.items[7].severity, "na");
  EXPECT_EQ(q.items[7].value, "");  // null은 빈 문자열
  EXPECT_EQ(q.items[7].reason, "");
  EXPECT_NEAR(q.thermal_contrast_left, 59.3, 1e-12);
  EXPECT_NEAR(q.thermal_contrast_right, 66.2, 1e-12);
  EXPECT_EQ(q.WorstReason(), "subject");
}

// v9의 요약: 2026-10-30 17:35:35 촬영(수동, 노출 -9, 게인 1, 손가락 PPG, 커프 없음)을
// 분석한 파일입니다. 상태는 문자이고 level_pct_sbp가 없으며, qc version 2에 맥박 타이밍 기준과 1 Hz 깜빡임 항목이 있습니다.
TEST(PerfusionSummary, ReadsCuffStateSummary) {
  std::string error;
  const auto s = ReadPerfusionSummary(Data("173535_v9_summary.json"), &error);
  ASSERT_TRUE(s.has_value()) << error;
  EXPECT_EQ(s->variant, "v9");
  ASSERT_TRUE(s->cuff_level.has_value());
  const auto& c = *s->cuff_level;
  EXPECT_TRUE(c.states);
  EXPECT_TRUE(c.has_level);
  EXPECT_EQ(c.level_code, "0");
  EXPECT_EQ(c.level_ko, "정상");
  EXPECT_EQ(c.level_pct, 0);
  ASSERT_TRUE(c.pad.has_value());
  EXPECT_FALSE(*c.pad);
  EXPECT_NEAR(c.pad_probability, 0.0001, 1e-12);
  EXPECT_NEAR(c.p_state_0, 0.7972, 1e-12);
  EXPECT_NEAR(c.p_state_v, 0.2027, 1e-12);
  EXPECT_NEAR(c.p_state_d, 0.0001, 1e-12);
  EXPECT_NEAR(c.p_state_o, 0.0, 1e-12);
  EXPECT_EQ(c.level_code_if_quality_ignored, "0");
  EXPECT_EQ(c.level_if_quality_ignored, -1);
  EXPECT_TRUE(c.applicable);
  EXPECT_TRUE(std::isnan(c.pi_ratio_below));  // v9 요약에는 규칙 기준값이 없습니다
  EXPECT_NEAR(c.snr_db, 17.9772, 1e-12);
  ASSERT_TRUE(s->quality.has_value());
  const auto& items = s->quality->items;
  ASSERT_EQ(items.size(), 10u);
  EXPECT_EQ(items[8].code, "pulse_timing");
  EXPECT_EQ(items[8].value, "v106w-foot right");
  EXPECT_EQ(items[9].code, "light_flicker");
  EXPECT_EQ(items[9].severity, "warn");
  EXPECT_EQ(items[9].value, "0.2845");
  EXPECT_EQ(items[9].reason, "equipment");
}

// v9에서 강한 발의 맥박 SNR이 기준보다 낮으면 상태, 이름, pad는 null이고, PAD 확률과 품질을 뺀 상태 문자는 남습니다.
// 같은 요약에서 cuff_dl.predict_v5와 plantar_perfusion.cuff_level이 품질 미달 때 쓰는 값으로 바꾼 파일입니다.
TEST(PerfusionSummary, ReadsCuffStateWithoutQuality) {
  std::string error;
  const auto s = ReadPerfusionSummary(Data("173535_v9_weak_summary.json"), &error);
  ASSERT_TRUE(s.has_value()) << error;
  ASSERT_TRUE(s->cuff_level.has_value());
  const auto& c = *s->cuff_level;
  EXPECT_TRUE(c.states);
  EXPECT_FALSE(c.has_level);
  EXPECT_TRUE(c.level_code.empty());
  EXPECT_TRUE(c.level_ko.empty());
  EXPECT_FALSE(c.pad.has_value());
  EXPECT_FALSE(c.quality_ok);
  EXPECT_FALSE(c.applicable);
  EXPECT_EQ(c.level_code_if_quality_ignored, "0");
  EXPECT_NEAR(c.pad_probability, 0.0001, 1e-12);
}

// v8까지의 요약은 커프 상태가 아닙니다.
TEST(PerfusionSummary, PercentSummaryIsNotCuffStates) {
  std::string error;
  const auto s = ReadPerfusionSummary(Data("160307_v8_qc_summary.json"), &error);
  ASSERT_TRUE(s.has_value()) << error;
  ASSERT_TRUE(s->cuff_level.has_value());
  EXPECT_FALSE(s->cuff_level->states);
  EXPECT_TRUE(s->cuff_level->level_code.empty());
  EXPECT_FALSE(s->cuff_level->pad.has_value());
  EXPECT_EQ(s->cuff_level->level_if_quality_ignored, 40);
}

// qc가 null이거나 없으면 nullopt이고, 가장 심한 항목은 fail이 warn보다 앞섭니다.
TEST(PerfusionSummary, MeasurementQualityWorstReason) {
  const auto none = ReadPerfusionSummary(R"({"pi_left": 0.1, "pi_right": 0.1, "qc": null})");
  ASSERT_TRUE(none.has_value());
  EXPECT_FALSE(none->quality.has_value());
  hwlab::platform::MeasurementQuality q;
  q.items = {{"brightness", "", "warn", "", "", "", "", "", "equipment"},
             {"pulse", "", "fail", "", "", "", "", "", "subject"},
             {"foot_found", "", "fail", "", "", "", "", "", "posture"}};
  EXPECT_EQ(q.WorstReason(), "subject");
  q.items = {{"pulse", "", "ok", "", "", "", "", "", ""}};
  EXPECT_EQ(q.WorstReason(), "");
}

// v7까지의 요약에는 발 셀이 없어 셀 수가 NaN입니다. 결과 창은 이 값으로 v8 결과인지 가립니다.
TEST(PerfusionSummary, OlderSummaryHasNoFootCells) {
  std::string error;
  const auto s = ReadPerfusionSummary(Data("185205_rule_summary.json"), &error);
  ASSERT_TRUE(s.has_value()) << error;
  EXPECT_TRUE(std::isnan(s->cells_used));
  EXPECT_TRUE(std::isnan(s->left.cells));
}

// 규칙 판정이 단계를 내면 has_level이 true이고, 로지스틱 회귀 요약은 rule이 비고 품질은 통과로 봅니다.
TEST(PerfusionSummary, ReadsRuleCuffLevelWithLevel) {
  const auto rule = ReadPerfusionSummary(R"({"pi_left": 0.1, "pi_right": 0.2,
      "cuff_level": {"level_pct_sbp": 120, "level_if_quality_ignored": 120, "rule": "h1re_30 < 0.2639",
                     "quality_ok": true, "applicable": true, "features": {"h1re_30": 0.1, "hi_snr1_30": 20.5}}})");
  ASSERT_TRUE(rule.has_value() && rule->cuff_level.has_value());
  EXPECT_TRUE(rule->cuff_level->has_level);
  EXPECT_EQ(rule->cuff_level->level_pct, 120);
  EXPECT_EQ(rule->cuff_level->rule, "h1re_30 < 0.2639");
  EXPECT_TRUE(rule->cuff_level->quality_ok);
  EXPECT_EQ(rule->cuff_level->snr_db, 20.5);
  EXPECT_TRUE(std::isnan(rule->cuff_level->pi_ratio));
  const auto logistic = ReadPerfusionSummary(Data("185205_cuff_level_summary.json"));
  ASSERT_TRUE(logistic.has_value() && logistic->cuff_level.has_value());
  EXPECT_TRUE(logistic->cuff_level->has_level);
  EXPECT_TRUE(logistic->cuff_level->rule.empty());
  EXPECT_TRUE(logistic->cuff_level->quality_ok);
  EXPECT_EQ(logistic->cuff_level->level_if_quality_ignored, -1);
  EXPECT_EQ(logistic->variant, "");
}

// 관류 분석 프로그램이 열화상을 뺀 이유 코드를 읽고 문장으로 바꿉니다.
TEST(PerfusionSummary, ReadsThermalSkipReason) {
  const std::string json =
      Replace(Data("102528_summary.json"), "\"thermal_skipped\": null", "\"thermal_skipped\": \"thermal_too_short\"");
  const auto s = ReadPerfusionSummary(json);
  ASSERT_TRUE(s.has_value());
  EXPECT_EQ(s->thermal_skipped, "thermal_too_short");
  EXPECT_NE(DescribeThermalSkip(s->thermal_skipped).find("7.5Hz"), std::string::npos);
  EXPECT_NE(DescribeThermalSkip("new_code").find("new_code"), std::string::npos);
  EXPECT_EQ(DescribeThermalSkip(""), "");
}

// 녹화 중 미리 분할, 미리 읽기를 쓰지 못한 이유 코드를 읽습니다. 썼거나(null) 꺼져 있으면(키 없음) 빈 문자열입니다.
TEST(PerfusionSummary, ReadsSpeedupSkipReasons) {
  const auto skipped = ReadPerfusionSummary(R"({"pi_left": 0.1, "pi_right": 0.2, "dfr": {
      "presegment": {"enabled": true, "threads": 2, "used": [], "missed": [{"reason": "model_not_loaded"}],
                     "reason": "model_not_loaded"},
      "prestream": {"used": false, "reason": "no_presegment", "catchup_s": 0.0}}})");
  ASSERT_TRUE(skipped.has_value());
  EXPECT_EQ(skipped->presegment_skipped, "model_not_loaded");
  EXPECT_EQ(skipped->prestream_skipped, "no_presegment");
  EXPECT_NE(DescribeSpeedupSkip(skipped->presegment_skipped).find("분할 모델"), std::string::npos);
  const auto used = ReadPerfusionSummary(R"({"pi_left": 0.1, "pi_right": 0.2, "dfr": {
      "presegment": {"enabled": true, "reason": null}, "prestream": {"used": true, "reason": null}}})");
  ASSERT_TRUE(used.has_value());
  EXPECT_EQ(used->presegment_skipped, "");
  EXPECT_EQ(used->prestream_skipped, "");
  const auto off = ReadPerfusionSummary(R"({"pi_left": 0.1, "pi_right": 0.2,
      "dfr": {"presegment": {"enabled": false}, "prestream": {"enabled": false}}})");
  ASSERT_TRUE(off.has_value());
  EXPECT_EQ(off->prestream_skipped, "");
  EXPECT_NE(DescribeSpeedupSkip("new_code").find("new_code"), std::string::npos);
  EXPECT_EQ(DescribeSpeedupSkip(""), "");
}

// pi_left나 pi_right 키가 없거나 JSON이 깨지면 읽지 않습니다.
TEST(PerfusionSummary, MissingPiKeyFails) {
  std::string error;
  EXPECT_FALSE(ReadPerfusionSummary(Replace(Data("102528_summary.json"), "\"pi_left\":", "\"pi_l\":"), &error));
  EXPECT_NE(error.find("pi_left"), std::string::npos);
  EXPECT_FALSE(ReadPerfusionSummary(R"({"pi_right": 0.1})"));
  EXPECT_FALSE(ReadPerfusionSummary(R"({"pi_left": 0.1, "pi_right": 0.2)"));
  EXPECT_FALSE(ReadPerfusionSummary(""));
}

// 판정 항목이 없으면 판정 없음, 나머지 키가 없으면 기본값입니다.
TEST(PerfusionSummary, MissingOptionalKeysUseDefaults) {
  const auto s = ReadPerfusionSummary(R"({"pi_left": 0.1, "pi_right": 0.1})");
  ASSERT_TRUE(s.has_value());
  EXPECT_FALSE(s->classified);
  EXPECT_EQ(PadVerdictOf(*s), PadVerdict::kNone);
  EXPECT_FALSE(s->lower_foot.has_value());  // 같은 값
  EXPECT_DOUBLE_EQ(s->pi_ratio, 1.0);       // 키가 없어 왼발 / 오른발로 구함
  EXPECT_EQ(s->window_count, 0);
  EXPECT_EQ(s->window_us, 0);
  EXPECT_TRUE(std::isnan(s->fps));
  // p_pad만 있고 threshold가 없으면 판정으로 보지 않습니다.
  const auto half = ReadPerfusionSummary(R"({"pi_left": 0.1, "pi_right": 0.2, "classification": {"p_pad": 0.7}})");
  ASSERT_TRUE(half.has_value());
  EXPECT_FALSE(half->classified);
}

// 파이썬이 쓴 NaN은 값이 없는 것으로 읽고, 더 낮은 발과 비를 지어내지 않습니다.
TEST(PerfusionSummary, NanValuesStayUnknown) {
  const auto s = ReadPerfusionSummary(
      R"({"pi_left": NaN, "pi_right": 0.09, "pi_ratio_left_right": NaN,
          "classification": {"p_pad": NaN, "threshold": 0.45, "decision": "normal", "window_probs": [0.1, NaN, 0.2]}})");
  ASSERT_TRUE(s.has_value());
  EXPECT_TRUE(std::isnan(s->pi_left));
  EXPECT_NEAR(s->pi_right, 0.09, 1e-12);
  EXPECT_TRUE(std::isnan(s->pi_ratio));
  EXPECT_FALSE(s->lower_foot.has_value());
  ASSERT_TRUE(s->classified);
  EXPECT_TRUE(std::isnan(s->p_pad));
  ASSERT_EQ(s->window_probs.size(), 3u);
  EXPECT_TRUE(std::isnan(s->window_probs[1]));
  // 비 키가 없고 PI 하나가 NaN이면 비를 구하지 않습니다.
  const auto no_ratio = ReadPerfusionSummary(R"({"pi_left": NaN, "pi_right": 0.09})");
  ASSERT_TRUE(no_ratio.has_value());
  EXPECT_TRUE(std::isnan(no_ratio->pi_ratio));
}

// 관류 분석 프로그램이 모르는 판정 낱말을 적으면 어느 쪽으로도 짐작하지 않습니다.
TEST(PerfusionSummary, UnknownDecisionIsNone) {
  const auto s = ReadPerfusionSummary(
      R"({"pi_left": 0.1, "pi_right": 0.2, "classification": {"p_pad": 0.9, "threshold": 0.4, "decision": "PAD"}})");
  ASSERT_TRUE(s.has_value());
  EXPECT_EQ(PadVerdictOf(*s), PadVerdict::kNone);
}

// 분석결과 폴더에는 요약, 보고서, 그림, GIF만 들어갑니다(npz, 다른 촬영 이름, 관류 분석 프로그램 기록 제외).
TEST(PerfusionSummary, SelectsCollectedFiles) {
  EXPECT_TRUE(IsCollectedResultFile("102528_summary.json", "102528"));
  EXPECT_TRUE(IsCollectedResultFile("102528_report.txt", "102528"));
  EXPECT_TRUE(IsCollectedResultFile("102528_frame_timing.csv", "102528"));
  EXPECT_TRUE(IsCollectedResultFile("102528_frame_windows.csv", "102528"));
  EXPECT_FALSE(IsCollectedResultFile("102528_other.csv", "102528"));
  EXPECT_TRUE(IsCollectedResultFile("102528_perfusion.gif", "102528"));
  EXPECT_TRUE(IsCollectedResultFile("102528_temporal_mean.png", "102528"));
  EXPECT_FALSE(IsCollectedResultFile("102528_perfusion.npz", "102528"));
  EXPECT_FALSE(IsCollectedResultFile("analyzer_log.txt", "102528"));
  EXPECT_FALSE(IsCollectedResultFile("102529_summary.json", "102528"));
  EXPECT_FALSE(IsCollectedResultFile("102528_.png", ""));
  EXPECT_EQ(PerfusionGifPath("a/perfusion", "102528"), "a/perfusion/102528_perfusion.gif");
}

// 결과 파일을 <저장 위치>/results/<촬영 폴더 경로 단계>_*로 복사합니다. npz는 복사하지 않습니다.
TEST(PerfusionSummary, CollectsResultsUnderShotPrefix) {
  namespace fs = std::filesystem;
  const fs::path base = fs::temp_directory_path() / "hwlab_test_collect";
  fs::remove_all(base);
  const fs::path out = base / "shot" / "perfusion";
  fs::create_directories(out);
  for (const char* name : {"run_summary.json", "run_perfusion.gif", "run_waveforms.png", "run_perfusion.npz"}) {
    std::ofstream(out / name) << name;
  }
  const ShotLayout layout = FolderLayout("test_19551103/0/20261020_102528");
  const std::string prefix = "test_19551103_0_20261020_102528";
  const auto copied = CollectPerfusionResults(out.string(), "run", (base / "root").string(), layout);
  ASSERT_TRUE(copied.has_value());
  EXPECT_EQ(copied->files.size(), 3u);
  EXPECT_EQ(copied->copied, 3);
  EXPECT_EQ(copied->reanalysis, 1);
  // 저장 위치 아래 폴더 이름은 한글이라 UTF-8로 만든 경로로 확인합니다.
  const fs::path collect = base / "root" / fs::path("results");
  EXPECT_EQ(ReadText(collect / (prefix + "_summary.json")), "run_summary.json");
  EXPECT_TRUE(fs::exists(collect / (prefix + "_perfusion.gif")));
  EXPECT_TRUE(fs::exists(collect / (prefix + "_waveforms.png")));
  EXPECT_FALSE(fs::exists(collect / (prefix + "_perfusion.npz")));
  std::string error;
  EXPECT_FALSE(
      CollectPerfusionResults((base / "none").string(), "run", (base / "root").string(), FolderLayout("x"), {}, &error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(CollectPerfusionResults(out.string(), "run", (base / "root").string(), ShotLayout{}));
}

// 판 이름(tag)을 주면 앞부분 뒤에 붙여, 판마다 이름이 같은 결과 파일이 분석결과 폴더에서 서로 덮지 않습니다.
TEST(PerfusionSummary, CollectsVariantsUnderTaggedPrefix) {
  namespace fs = std::filesystem;
  const fs::path base = fs::temp_directory_path() / "hwlab_test_collect_tag";
  fs::remove_all(base);
  const std::string root = (base / "root").string();
  const ShotLayout layout = FolderLayout("p");
  for (const char* variant : {"v6", "v7"}) {
    const fs::path out = base / "perfusion" / variant;
    fs::create_directories(out);
    std::ofstream(out / "run_summary.json") << variant;
    const auto collected = CollectPerfusionResults(out.string(), "run", root, layout, variant);
    ASSERT_TRUE(collected.has_value());
    EXPECT_EQ(collected->copied, 1);
    EXPECT_EQ(collected->reanalysis, 1);
  }
  const fs::path collect = base / "root" / fs::path("results");
  EXPECT_EQ(ReadText(collect / "p_v6_summary.json"), "v6");
  EXPECT_EQ(ReadText(collect / "p_v7_summary.json"), "v7");
  EXPECT_FALSE(fs::exists(collect / "p_summary.json"));
}

// 같은 내용으로 다시 모으면 통과하고, 내용이 다른 결과(다시 분석)는 덮어쓰지 않고 _reanalysis2, _reanalysis3으로 새로 모읍니다.
TEST(PerfusionSummary, CollectNeverOverwritesAndNumbersReanalysis) {
  namespace fs = std::filesystem;
  const fs::path base = fs::temp_directory_path() / "hwlab_test_collect_again";
  fs::remove_all(base);
  const fs::path out = base / "perfusion";
  fs::create_directories(out);
  std::ofstream(out / "run_summary.json") << "first";
  std::ofstream(out / "run_waveforms.png") << "png";
  const std::string root = (base / "root").string();
  const ShotLayout layout = FolderLayout("p");
  ASSERT_TRUE(CollectPerfusionResults(out.string(), "run", root, layout).has_value());
  const auto again = CollectPerfusionResults(out.string(), "run", root, layout);
  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(again->files.size(), 2u);
  EXPECT_EQ(again->copied, 0);
  EXPECT_EQ(again->reanalysis, 1);

  // 다시 분석해 요약이 바뀌면 처음 모은 파일은 그대로 두고, 이번 결과 전체를 2번으로 모읍니다.
  std::ofstream(out / "run_summary.json") << "changed";
  std::ofstream(out / "run_perfusion.gif") << "gif";
  const auto second = CollectPerfusionResults(out.string(), "run", root, layout);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->files.size(), 3u);
  EXPECT_EQ(second->copied, 3);
  EXPECT_EQ(second->reanalysis, 2);
  EXPECT_EQ(std::filesystem::path(std::u8string(second->folder.begin(), second->folder.end())),
            (base / "root" / fs::path("results")).make_preferred());
  const fs::path collect = base / "root" / fs::path("results");
  const std::string second_prefix = hwlab::record::CollectedResultPrefix(layout, 2);
  EXPECT_EQ(ReadText(collect / "p_summary.json"), "first");
  EXPECT_FALSE(fs::exists(collect / "p_perfusion.gif"));
  EXPECT_EQ(ReadText(collect / fs::path(std::u8string(u8"p_reanalysis2_summary.json"))), "changed");
  EXPECT_TRUE(fs::exists(collect / fs::path(std::u8string(u8"p_reanalysis2_perfusion.gif"))));
  EXPECT_TRUE(fs::exists(collect / fs::path(std::u8string(u8"p_reanalysis2_waveforms.png"))));
  EXPECT_EQ(second_prefix, "p_reanalysis2");

  // 2번과 같은 결과를 다시 모으면 새 번호를 만들지 않고, 또 바뀌면 3번으로 모읍니다.
  ASSERT_TRUE(CollectPerfusionResults(out.string(), "run", root, layout).has_value());
  EXPECT_FALSE(fs::exists(collect / fs::path(std::u8string(u8"p_reanalysis3_summary.json"))));
  std::ofstream(out / "run_summary.json") << "third";
  const auto third = CollectPerfusionResults(out.string(), "run", root, layout);
  ASSERT_TRUE(third.has_value());
  EXPECT_EQ(third->reanalysis, 3);
  EXPECT_EQ(ReadText(collect / fs::path(std::u8string(u8"p_reanalysis3_summary.json"))), "third");
}

// 실제 촬영 102528의 매니페스트 files 항목에서 영상, 사이드카, 원시값 파일 이름을 읽습니다.
TEST(ShotReview, ReadsManifestFiles) {
  const auto files = ReadShotManifest(R"({"schema_version": 3, "pixel_left_foot": "right", "files": {
    "left_video": "102528_left.mkv", "left_sidecar": "102528_left.json",
    "right_video": "102528_right.mkv", "right_sidecar": "102528_right.json",
    "thermal_raw": "102528_thermal.raw16", "thermal_sidecar": "102528_thermal.json",
    "thermal_preview": "102528_thermal.avi", "manifest": "102528_shot.json"},
    "analysis_inputs": ["left_video", "right_video"]})");
  ASSERT_TRUE(files.has_value());
  EXPECT_EQ(files->left_video, "102528_left.mkv");
  EXPECT_EQ(files->right_sidecar, "102528_right.json");
  EXPECT_EQ(files->thermal_raw, "102528_thermal.raw16");
  EXPECT_EQ(files->thermal_sidecar, "102528_thermal.json");
  EXPECT_EQ(files->pixel_left_foot, FootSide::kRight);
  // 열화상 없이 찍은 촬영은 열화상 이름이 비고, 영상 이름이 없으면 읽지 않습니다.
  const auto rgb_only = ReadShotManifest(R"({"files": {"left_video": "a.mkv", "right_video": "b.mkv"}})");
  ASSERT_TRUE(rgb_only.has_value());
  EXPECT_EQ(rgb_only->thermal_raw, "");
  EXPECT_FALSE(rgb_only->pixel_left_foot.has_value());
  EXPECT_FALSE(ReadShotManifest(R"({"files": {"left_video": "a.mkv"}})"));
}

// 사이드카에서 크기, 프레임별 공통 시각, 표시 범위를 읽습니다(실제 촬영 102528 열화상 사이드카의 앞부분 값).
TEST(ShotReview, ReadsSidecarFrames) {
  const auto thermal = ReadSidecarFrames(R"({"format": {"width": 640, "height": 480},
    "display_range": {"low": 9418, "high": 9666},
    "per_frame": {"index": [2065, 2069], "common_time_us": [26441960429443, 26441960562713]}})");
  ASSERT_TRUE(thermal.has_value());
  EXPECT_EQ(thermal->width, 640);
  EXPECT_EQ(thermal->height, 480);
  ASSERT_EQ(thermal->common_time.size(), 2u);
  EXPECT_EQ(thermal->common_time[1].us, 26441960562713);
  ASSERT_TRUE(thermal->display_range.has_value());
  EXPECT_EQ(thermal->display_range->low, 9418);
  EXPECT_EQ(thermal->display_range->high, 9666);
  const auto rgb = ReadSidecarFrames(
      R"({"format": {"width": 640, "height": 1080}, "display_range": null, "per_frame": {"common_time_us": []}})");
  ASSERT_TRUE(rgb.has_value());
  EXPECT_FALSE(rgb->display_range.has_value());
  EXPECT_FALSE(ReadSidecarFrames(R"({"format": {"width": 640, "height": 480}})"));
  EXPECT_FALSE(ReadSidecarFrames(R"({"format": {"width": 640, "height": 480}, "per_frame": {"common_time_us": [1.5]}})"));
  // 열화상 사이드카에는 카메라 설정 기록이 없습니다.
  EXPECT_TRUE(thermal->camera.mode.empty());
  EXPECT_FALSE(thermal->camera.exposure_step.has_value());
  EXPECT_FALSE(thermal->frame_rate.has_value());
}

// 촬영 때 입력한 기준과 조명을 매니페스트에서 읽습니다(실제 촬영 20261028_160307의 값).
TEST(ShotReview, ReadsManifestCriterionAndLight) {
  const auto files = ReadShotManifest(R"({"criterion": 40,
    "lighting": {"source": "external", "product": "4000K COB LED, 12V", "brightness_percent": null},
    "files": {"left_video": "a.mkv", "right_video": "b.mkv"}})");
  ASSERT_TRUE(files.has_value());
  EXPECT_EQ(files->criterion, 40);
  EXPECT_EQ(files->light_source, "external");
  EXPECT_EQ(files->light_product, "4000K COB LED, 12V");
  // 기준을 고르지 않은 촬영(-1)과 기준이 없는 이전 매니페스트는 nullopt입니다.
  const auto unset = ReadShotManifest(R"({"criterion": -1, "files": {"left_video": "a.mkv", "right_video": "b.mkv"}})");
  ASSERT_TRUE(unset.has_value());
  EXPECT_FALSE(unset->criterion.has_value());
  EXPECT_TRUE(unset->light_product.empty());
  EXPECT_TRUE(unset->ankle_level.empty());
}

// 발목 혈압 단계를 매니페스트 schema 6의 blood_pressure에서 읽습니다(실기 촬영 20261030_100959의 값).
TEST(ShotReview, ReadsManifestAnkleLevel) {
  const auto files = ReadShotManifest(R"({"schema_version": 6, "criterion": 40,
    "blood_pressure": {"arm": {"systolic_mmhg": 120, "diastolic_mmhg": 80}, "ankle": {"systolic_mmhg": 135, "diastolic_mmhg": 84}, "ankle_level": {"name": "D", "pressure_mmhg": 84}},
    "files": {"left_video": "a.avi", "right_video": "b.avi"}})");
  ASSERT_TRUE(files.has_value());
  EXPECT_EQ(files->ankle_level, "D");
  EXPECT_EQ(files->criterion, 40);
}

// RGB 사이드카의 카메라 설정을 읽습니다(실제 촬영 20261028_160307 왼발 사이드카의 값).
TEST(ShotReview, ReadsSidecarCameraSettings) {
  const auto rgb = ReadSidecarFrames(R"({"format": {"pixel_format": "UYVY", "width": 774, "height": 1080,
    "frame_rate": 30, "codec": "ffv1"},
    "settings": {"mode": "manual", "exposure_auto": false, "exposure_step": -9, "exposure_ms": 1.953, "gain": 1,
      "white_balance_auto": false, "white_balance_k": 4000, "sdk_frame_rate": 30, "locked": true},
    "per_frame": {"common_time_us": [1, 2]}})");
  ASSERT_TRUE(rgb.has_value());
  EXPECT_EQ(rgb->frame_rate, 30);
  EXPECT_EQ(rgb->camera.mode, "manual");
  EXPECT_EQ(rgb->camera.exposure_step, -9);
  ASSERT_TRUE(rgb->camera.exposure_ms.has_value());
  EXPECT_DOUBLE_EQ(*rgb->camera.exposure_ms, 1.953);
  EXPECT_EQ(rgb->camera.gain, 1);
  EXPECT_EQ(rgb->camera.white_balance_auto, false);
  EXPECT_EQ(rgb->camera.white_balance_k, 4000);
  EXPECT_EQ(rgb->camera.locked, true);
}

// 지금 앱이 쓰는 사이드카(schema 7, 조명 기록과 깜빡임 요약 포함)를 결과 창(ReadSidecarFrames)과
// 분석 요청(record::ReadSidecarSummary)이 모두 읽습니다.
TEST(ShotReview, ReadsCurrentSidecarSchemaWithLight) {
  hwlab::record::Sidecar rgb;
  rgb.format = {hwlab::PixelFormat::kUyvy, 640, 1080, 30};
  rgb.foot = FootSide::kLeft;
  rgb.crop = {0, 0, 640, 1080};
  rgb.written_frames = 3;
  rgb.written_in_rgb_window = 3;
  for (int64_t i = 0; i < 3; ++i) {
    const int64_t t_us = i * 33'333;
    rgb.records.push_back({static_cast<hwlab::FrameIndex>(i), {1'000'000 + t_us}, {2'000'000 + t_us}, 100, 0});
  }
  hwlab::record::LightRecord light;
  light.source = hwlab::record::LightSource::kApp;
  light.percent = 70;
  light.usb_vendor_id = 0x0403;
  light.usb_product_id = 0x6001;
  light.serial = "A50285BIA";
  light.confirmed = true;
  light.flicker = hwlab::pixel::Flicker{.frame_count = 3, .mean = 1.0, .dominant_period_frames = 2.5};
  rgb.light = light;
  const std::string rgb_json = hwlab::record::BuildSidecarJson(rgb);
  ASSERT_NE(rgb_json.find("\"schema_version\": 8"), std::string::npos);

  std::string error;
  const auto frames = ReadSidecarFrames(rgb_json, &error);
  ASSERT_TRUE(frames.has_value()) << error;
  EXPECT_EQ(frames->width, 640);
  ASSERT_EQ(frames->common_time.size(), 3u);
  EXPECT_EQ(frames->common_time[2].us, 1'066'666);
  EXPECT_FALSE(frames->display_range.has_value());
  const auto summary = hwlab::record::ReadSidecarSummary(rgb_json, &error);
  ASSERT_TRUE(summary.has_value()) << error;
  EXPECT_EQ(summary->written_frames, 3);
  EXPECT_EQ(summary->foot, FootSide::kLeft);

  // 열화상 사이드카는 조명이 null이고 표시 범위가 있습니다.
  hwlab::record::Sidecar thermal;
  thermal.stream = hwlab::StreamKind::kThermal;
  thermal.format = {hwlab::PixelFormat::kY16, 640, 480, 9};
  thermal.crop = {0, 0, 640, 480};
  thermal.display_range = hwlab::pixel::Y16Range{9418, 9666};
  thermal.records.push_back({0, {5}, {5}, 0, 0});
  const std::string thermal_json = hwlab::record::BuildSidecarJson(thermal);
  const auto thermal_frames = ReadSidecarFrames(thermal_json, &error);
  ASSERT_TRUE(thermal_frames.has_value()) << error;
  ASSERT_TRUE(thermal_frames->display_range.has_value());
  EXPECT_EQ(thermal_frames->display_range->high, 9666);
  EXPECT_TRUE(hwlab::record::ReadSidecarSummary(thermal_json, &error).has_value()) << error;
}

// 원시값 파일에서 번호 i의 프레임을 리틀 엔디언으로 읽고, 범위 밖 번호는 읽지 않습니다.
TEST(ShotReview, ReadsRaw16FrameByNumber) {
  const auto path = (std::filesystem::temp_directory_path() / "hwlab_test_review.raw16").string();
  {
    std::ofstream file(path, std::ios::binary);
    for (int frame = 0; frame < 3; ++frame) {
      for (int i = 0; i < 6; ++i) {
        const uint16_t v = static_cast<uint16_t>(frame * 1000 + i + 0x100);
        file.put(static_cast<char>(v & 0xFF));
        file.put(static_cast<char>(v >> 8));
      }
    }
  }
  std::vector<uint16_t> out;
  ASSERT_TRUE(ReadRaw16Frame(path, 3, 2, 2, &out));
  ASSERT_EQ(out.size(), 6u);
  EXPECT_EQ(out[0], 2000 + 0x100);
  EXPECT_EQ(out[5], 2005 + 0x100);
  EXPECT_FALSE(ReadRaw16Frame(path, 3, 2, 3, &out));
  EXPECT_FALSE(ReadRaw16Frame(path, 3, 2, -1, &out));
}

}  // namespace
