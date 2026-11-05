#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "hwlab/flow/capture_spec.h"

namespace {

using hwlab::SettingMode;
using hwlab::flow::CaptureSpec;
using hwlab::flow::ParseCaptureSpec;
using hwlab::flow::SpecProblem;
using hwlab::flow::ValidateCaptureSpec;

std::string ReadSpecFile(const char* name) {
  std::ifstream file(std::string(HWLAB_SPEC_DIR) + "/" + name);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

// 레포의 직접 지정 선언 파일을 읽으면 실측값이 그대로 들어오고 검증을 통과합니다.
TEST(CaptureSpec, ParsesManualSpecFile) {
  std::string error;
  const auto spec = ParseCaptureSpec(ReadSpecFile("see3cam_manual_30fps.json"), &error);

  ASSERT_TRUE(spec) << error;
  EXPECT_EQ(spec->device, "See3CAM_24CUG");
  EXPECT_EQ(spec->settings.mode, SettingMode::kManual);
  EXPECT_EQ(spec->settings.exposure_step, -5);
  EXPECT_EQ(spec->settle.black_level_settled_frame, 75);
  EXPECT_EQ(spec->target_frames, 900);
  EXPECT_EQ(spec->codec, hwlab::flow::VideoCodec::kFfv1);
  EXPECT_STREQ(hwlab::flow::ExtensionOf(spec->codec), "mkv");
  EXPECT_EQ(spec->thermal_record_period_us, 111111);
  EXPECT_TRUE(ValidateCaptureSpec(*spec).empty());
}

// 모르는 코덱 이름은 거부합니다.
TEST(CaptureSpec, RejectsUnknownCodec) {
  std::string text = ReadSpecFile("see3cam_manual_30fps.json");
  text.replace(text.find("\"ffv1\""), 6, "\"h264\"");
  std::string error;

  EXPECT_FALSE(ParseCaptureSpec(text, &error));
  EXPECT_NE(error.find("codec"), std::string::npos);
}

// 자동 맞춤 선언에는 노출 단계가 없어도 됩니다.
TEST(CaptureSpec, ParsesAutoSpecWithoutExposure) {
  std::string error;
  const auto spec = ParseCaptureSpec(ReadSpecFile("see3cam_auto_30fps.json"), &error);

  ASSERT_TRUE(spec) << error;
  EXPECT_EQ(spec->settings.mode, SettingMode::kAuto);
  EXPECT_TRUE(ValidateCaptureSpec(*spec).empty());
}

// 기본 선언(1280x720 60 fps 고정 노출)은 30초를 1800장으로 찍고 검증을 통과합니다.
TEST(CaptureSpec, ParsesDefault720p60SpecFile) {
  std::string error;
  const auto spec = ParseCaptureSpec(ReadSpecFile("see3cam_manual_720p60.json"), &error);

  ASSERT_TRUE(spec) << error;
  EXPECT_EQ(spec->width, 1280);
  EXPECT_EQ(spec->height, 720);
  EXPECT_EQ(spec->target_frames, 1800);
  EXPECT_EQ(spec->settings.mode, SettingMode::kManual);
  EXPECT_EQ(spec->settings.sdk_frame_rate, 60);
  EXPECT_TRUE(ValidateCaptureSpec(*spec).empty());
}

// 패키지에 들어가는 선언 파일은 모두 읽히고 검증을 통과해야 앱이 시작합니다(없앤 키가 남으면 앱이 바로 끝납니다).
TEST(CaptureSpec, EverySpecFileParses) {
  int count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(HWLAB_SPEC_DIR)) {
    if (entry.path().extension() != ".json") continue;
    const std::string name = entry.path().filename().string();
    std::string error;
    const auto spec = ParseCaptureSpec(ReadSpecFile(name.c_str()), &error);
    ASSERT_TRUE(spec) << name << ": " << error;
    EXPECT_TRUE(ValidateCaptureSpec(*spec).empty()) << name;
    ++count;
  }
  EXPECT_GE(count, 5);
}

// 직접 지정인데 노출 단계가 빠지면 거부합니다.
TEST(CaptureSpec, ManualRequiresExposure) {
  std::string text = ReadSpecFile("see3cam_manual_30fps.json");
  text.replace(text.find("\"exposure_step\": -5,"), 20, "");
  std::string error;

  EXPECT_FALSE(ParseCaptureSpec(text, &error));
  EXPECT_NE(error.find("settings.exposure_step"), std::string::npos);
}

// 오타로 생긴 모르는 키는 조용히 무시하지 않고 거부합니다.
TEST(CaptureSpec, RejectsUnknownKey) {
  std::string text = ReadSpecFile("see3cam_manual_30fps.json");
  text.replace(text.find("\"target_frames\""), 15, "\"target_frame\"");
  std::string error;

  EXPECT_FALSE(ParseCaptureSpec(text, &error));
  EXPECT_FALSE(error.empty());
}

// 정수가 아닌 숫자와 깨진 문서는 위치와 함께 거부합니다.
TEST(CaptureSpec, RejectsFractionAndBrokenJson) {
  std::string error;
  EXPECT_FALSE(ParseCaptureSpec(R"({"width": 1920.5})", &error));
  EXPECT_NE(error.find("정수"), std::string::npos);
  EXPECT_FALSE(ParseCaptureSpec(R"({"width": 1920)", &error));
}

// 검증은 값의 범위를 봅니다. 홀수 폭은 UYVY 화소 쌍을 깨뜨립니다.
TEST(CaptureSpec, ValidationFindsBadValues) {
  CaptureSpec spec;
  spec.device = "cam";
  spec.width = 1921;
  spec.height = 1080;
  spec.settings.sdk_frame_rate = 30;
  spec.settings.gain = 4;
  spec.settings.white_balance_k = 4600;
  spec.target_frames = 900;
  spec.settle = {4, 3, 3, 0};
  spec.clip_level = 250;
  spec.buffer_frames = 60;

  const auto problems = ValidateCaptureSpec(spec);

  ASSERT_EQ(problems.size(), 2u);
  EXPECT_EQ(problems[0], SpecProblem::kBadSize);
  EXPECT_EQ(problems[1], SpecProblem::kBadSettleRule);
}

// 검사 기준 목록(criteria)은 2026-10-30부터 받지 않습니다. 그 키가 남은 선언 파일은 모르는 키로 거부합니다.
TEST(CaptureSpec, RejectsCriteriaKey) {
  std::string text = ReadSpecFile("see3cam_auto_30fps.json");
  text.replace(text.find("\"codec\": \"ffv1\","), 16, "\"codec\": \"ffv1\", \"criteria\": [0, 40, 80, 120],");
  std::string error;
  EXPECT_FALSE(ParseCaptureSpec(text, &error));
  EXPECT_NE(error.find("criteria"), std::string::npos) << error;
}

// 열화상 기록 주기는 필수 키이고 0(모두 기록)을 명시할 수 있으며, 음수는 검증에서 거부합니다.
TEST(CaptureSpec, ThermalRecordPeriodIsRequiredAndNonNegative) {
  std::string text = ReadSpecFile("see3cam_auto_30fps.json");
  std::string missing = text;
  missing.replace(missing.find("\"thermal_record_period_us\": 111111,"), 35, "");
  std::string error;
  EXPECT_FALSE(ParseCaptureSpec(missing, &error));
  EXPECT_NE(error.find("thermal_record_period_us"), std::string::npos);

  std::string zero = text;
  zero.replace(zero.find("111111"), 6, "0");
  auto spec = ParseCaptureSpec(zero, &error);
  ASSERT_TRUE(spec) << error;
  EXPECT_EQ(spec->thermal_record_period_us, 0);
  EXPECT_TRUE(ValidateCaptureSpec(*spec).empty());

  spec->thermal_record_period_us = -1;
  const auto problems = ValidateCaptureSpec(*spec);
  ASSERT_EQ(problems.size(), 1u);
  EXPECT_EQ(problems[0], SpecProblem::kBadThermalRecordPeriod);
}

// int32 칸에 들지 않는 정수는 잘라 넣지 않고 거부합니다. 4294969216을 자르면 1920이 되어 검증을 통과합니다.
TEST(CaptureSpec, RejectsIntegerOutsideInt32) {
  const std::string text = ReadSpecFile("see3cam_manual_30fps.json");
  std::string error;

  std::string wide = text;
  wide.replace(wide.find("\"width\": 1920"), 13, "\"width\": 4294969216");
  EXPECT_FALSE(ParseCaptureSpec(wide, &error));
  EXPECT_NE(error.find("width"), std::string::npos);

  std::string gain = text;
  gain.replace(gain.find("\"gain\": 4"), 9, "\"gain\": -2147483649");
  EXPECT_FALSE(ParseCaptureSpec(gain, &error));
  EXPECT_NE(error.find("settings.gain"), std::string::npos);

  std::string edge = text;
  edge.replace(edge.find("\"gain\": 4"), 9, "\"gain\": 2147483647");
  const auto spec = ParseCaptureSpec(edge, &error);
  ASSERT_TRUE(spec) << error;
  EXPECT_EQ(spec->settings.gain, 2147483647);
}

std::vector<SpecProblem> ProblemsWith(void (*change)(CaptureSpec*)) {
  std::string error;
  auto spec = ParseCaptureSpec(ReadSpecFile("see3cam_manual_30fps.json"), &error);
  EXPECT_TRUE(spec) << error;
  if (!spec) return {};
  change(&*spec);
  return ValidateCaptureSpec(*spec);
}

// 폭과 높이는 음수, 0, kMaxFrameSide 초과를 거부합니다.
TEST(CaptureSpec, ValidationRejectsImpossibleSize) {
  using P = std::vector<SpecProblem>;
  const P bad{SpecProblem::kBadSize};
  EXPECT_EQ(ProblemsWith([](CaptureSpec* s) { s->width = -1920; }), bad);
  EXPECT_EQ(ProblemsWith([](CaptureSpec* s) { s->height = 0; }), bad);
  EXPECT_EQ(ProblemsWith([](CaptureSpec* s) { s->width = hwlab::flow::kMaxFrameSide + 2; }), bad);
  EXPECT_EQ(ProblemsWith([](CaptureSpec* s) { s->height = hwlab::flow::kMaxFrameSide + 1; }), bad);
  EXPECT_TRUE(ProblemsWith([](CaptureSpec* s) {
                s->width = hwlab::flow::kMaxFrameSide;
                s->height = hwlab::flow::kMaxFrameSide;
              }).empty());
}

// 직접 지정의 노출 단계는 -13~0만 받습니다. 자동 맞춤은 노출 단계를 쓰지 않아 보지 않습니다.
TEST(CaptureSpec, ValidationChecksExposureOnlyInManual) {
  using P = std::vector<SpecProblem>;
  const P bad{SpecProblem::kBadExposure};
  EXPECT_EQ(ProblemsWith([](CaptureSpec* s) { s->settings.exposure_step = -14; }), bad);
  EXPECT_EQ(ProblemsWith([](CaptureSpec* s) { s->settings.exposure_step = 1; }), bad);
  EXPECT_TRUE(ProblemsWith([](CaptureSpec* s) { s->settings.exposure_step = -13; }).empty());
  EXPECT_TRUE(ProblemsWith([](CaptureSpec* s) { s->settings.exposure_step = 0; }).empty());
  EXPECT_TRUE(ProblemsWith([](CaptureSpec* s) {
                s->settings.mode = SettingMode::kAuto;
                s->settings.exposure_step = 99;
              }).empty());
}

// 게인과 색온도는 UVC 제어 값(부호 없는 2바이트)에 들지 않는 값을 거부하고, 색온도 0도 거부합니다.
TEST(CaptureSpec, ValidationRejectsImpossibleGainAndWhiteBalance) {
  using P = std::vector<SpecProblem>;
  EXPECT_EQ(ProblemsWith([](CaptureSpec* s) { s->settings.gain = -1; }), P{SpecProblem::kBadGain});
  EXPECT_EQ(ProblemsWith([](CaptureSpec* s) { s->settings.gain = 65536; }), P{SpecProblem::kBadGain});
  EXPECT_EQ(ProblemsWith([](CaptureSpec* s) { s->settings.white_balance_k = 0; }), P{SpecProblem::kBadWhiteBalance});
  EXPECT_EQ(ProblemsWith([](CaptureSpec* s) { s->settings.white_balance_k = 70000; }),
            P{SpecProblem::kBadWhiteBalance});
  EXPECT_TRUE(ProblemsWith([](CaptureSpec* s) {
                s->settings.gain = 0;
                s->settings.white_balance_k = 65535;
              }).empty());
}

}  // namespace
