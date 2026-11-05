#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "hwlab/record/analysis_inputs.h"
#include "hwlab/record/sidecar.h"

namespace {

using hwlab::PixelFormat;
using hwlab::StreamKind;
using hwlab::pixel::Rect;
using hwlab::record::AnalysisFiles;
using hwlab::record::BuildSidecarJson;
using hwlab::record::CheckAnalysisInputs;
using hwlab::record::FileRole;
using hwlab::record::FootSide;
using hwlab::record::HasBlockingFinding;
using hwlab::record::InputFinding;
using hwlab::record::InputProblem;
using hwlab::record::kMinThermalFramesInRgbWindow;
using hwlab::record::ReadSidecarSummary;
using hwlab::record::Sidecar;
using hwlab::record::SidecarSummary;
using hwlab::record::ThermalFootRegions;

const std::vector<FileRole> kAllPresent{FileRole::kLeftVideo,         FileRole::kLeftSidecar,
                                        FileRole::kRightVideo,        FileRole::kRightSidecar,
                                        FileRole::kThermalSidecar,    FileRole::kThermalLeftArray,
                                        FileRole::kThermalRightArray, FileRole::kManifest};

SidecarSummary FootVideo(FootSide foot) {
  SidecarSummary s;
  s.foot = foot;
  s.frame_width = 1920;
  s.frame_height = 1080;
  s.crop = {foot == FootSide::kLeft ? 960 : 400, 0, 480, 1080};
  s.written_frames = 900;
  s.written_in_rgb_window = 900;
  return s;
}

SidecarSummary Thermal(int64_t in_window) {
  SidecarSummary s;
  s.stream = StreamKind::kThermal;
  s.frame_width = 384;
  s.frame_height = 288;
  s.crop = {0, 0, 384, 288};
  s.written_frames = in_window + 2;
  s.written_in_rgb_window = in_window;
  return s;
}

AnalysisFiles GoodFiles() { return {kAllPresent, FootVideo(FootSide::kLeft), FootVideo(FootSide::kRight), Thermal(270)}; }

// BuildSidecarJson이 쓴 열화상 사이드카에서 크기, 영역, 장 수, 누락 수를 읽습니다.
TEST(SidecarSummary, ReadsWhatBuildSidecarWrote) {
  Sidecar sidecar;
  sidecar.video_file = "142233_thermal.raw16";
  sidecar.device = "기기\"1\"\n";
  sidecar.stream = StreamKind::kThermal;
  sidecar.format = {PixelFormat::kY16, 384, 288, 30};
  sidecar.crop = {0, 0, 384, 288};
  sidecar.clock.rate = 1.000123456;
  sidecar.written_frames = 275;
  sidecar.written_in_rgb_window = 270;
  sidecar.gaps = {{10, 99990, 2}};
  sidecar.dropped_frames = 1;
  std::string error;

  const auto summary = ReadSidecarSummary(BuildSidecarJson(sidecar), &error);

  ASSERT_TRUE(summary) << error;
  EXPECT_EQ(summary->stream, StreamKind::kThermal);
  EXPECT_FALSE(summary->foot);
  EXPECT_EQ(summary->frame_width, 384);
  EXPECT_EQ(summary->frame_height, 288);
  EXPECT_EQ(summary->crop, (Rect{0, 0, 384, 288}));
  EXPECT_EQ(summary->written_frames, 275);
  EXPECT_EQ(summary->written_in_rgb_window, 270);
  EXPECT_EQ(summary->missing_frames, 2);
  EXPECT_EQ(summary->dropped_frames, 1);
}

// 좌우 영상 사이드카의 발과 저장 영역을 읽습니다.
TEST(SidecarSummary, ReadsFootAndCrop) {
  Sidecar sidecar;
  sidecar.format = {PixelFormat::kUyvy, 1920, 1080, 30};
  sidecar.foot = FootSide::kRight;
  sidecar.crop = {400, 0, 480, 1080};
  std::string error;

  const auto summary = ReadSidecarSummary(BuildSidecarJson(sidecar), &error);

  ASSERT_TRUE(summary) << error;
  EXPECT_EQ(summary->stream, StreamKind::kRgb);
  EXPECT_EQ(summary->foot, FootSide::kRight);
  EXPECT_EQ(summary->crop, (Rect{400, 0, 480, 1080}));
}

// 문법이 틀렸거나, 판이 낮거나, 필요한 키가 없으면 읽지 않고 이유를 남깁니다.
TEST(SidecarSummary, RejectsBrokenOrOldSidecar) {
  const std::string good = BuildSidecarJson(Sidecar{});
  std::string error;

  EXPECT_TRUE(ReadSidecarSummary(good, &error)) << error;
  EXPECT_FALSE(ReadSidecarSummary(good.substr(0, good.size() / 2), &error));
  EXPECT_FALSE(error.empty());
  std::string old = good;
  old.replace(old.find("\"schema_version\": 8"), 19, "\"schema_version\": 2");
  EXPECT_FALSE(ReadSidecarSummary(old, &error));
  std::string no_window = good;
  no_window.replace(no_window.find("written_in_rgb_window"), 21, "other_key_in_window__");
  EXPECT_FALSE(ReadSidecarSummary(no_window, &error));
  EXPECT_FALSE(ReadSidecarSummary("[]", &error));
}

// 영역을 주지 않으면 가운데 세로선에서 나누고, 화소 왼쪽 발에 따라 왼발과 오른발 영역을 바꿉니다.
TEST(ThermalFootRegions, SplitsAtCenterByPixelLeftFoot) {
  const auto right_on_left = ThermalFootRegions(384, 288, std::nullopt, FootSide::kRight);
  EXPECT_EQ(right_on_left.first, (Rect{192, 0, 192, 288}));
  EXPECT_EQ(right_on_left.second, (Rect{0, 0, 192, 288}));

  const auto left_on_left = ThermalFootRegions(384, 288, std::nullopt, FootSide::kLeft);
  EXPECT_EQ(left_on_left.first, (Rect{0, 0, 192, 288}));
  EXPECT_EQ(left_on_left.second, (Rect{192, 0, 192, 288}));

  const auto odd = ThermalFootRegions(385, 288, std::nullopt, FootSide::kLeft);
  EXPECT_EQ(odd.first, (Rect{0, 0, 192, 288}));
  EXPECT_EQ(odd.second, (Rect{193, 0, 192, 288}));
}

// 영역을 직접 주면 그 값을 쓰고, 화소 왼쪽 발에 따라 순서만 바꿉니다.
TEST(ThermalFootRegions, UsesGivenRegions) {
  const std::pair<Rect, Rect> given{{10, 20, 100, 200}, {200, 20, 100, 200}};

  const auto regions = ThermalFootRegions(384, 288, given, FootSide::kRight);

  EXPECT_EQ(regions.first, given.second);
  EXPECT_EQ(regions.second, given.first);
}

// 필요한 파일이 모두 있고 값이 기준을 넘으면 문제가 없습니다.
TEST(CheckAnalysisInputs, PassesCompleteShot) {
  EXPECT_TRUE(CheckAnalysisInputs(GoodFiles(), kMinThermalFramesInRgbWindow).empty());
}

// 없는 역할마다 문제를 하나씩 냅니다. 좌우 영상이 없으면 막고, 열화상 파일이 일부 없으면 경고입니다.
TEST(CheckAnalysisInputs, ReportsMissingRoles) {
  AnalysisFiles files = GoodFiles();
  files.present = {FileRole::kLeftVideo, FileRole::kLeftSidecar, FileRole::kRightSidecar, FileRole::kThermalSidecar,
                   FileRole::kThermalLeftArray};

  const auto findings = CheckAnalysisInputs(files, kMinThermalFramesInRgbWindow);

  const std::vector<InputFinding> expected{{InputProblem::kMissingFile, FileRole::kRightVideo, true, 0, 0},
                                           {InputProblem::kMissingFile, FileRole::kThermalRightArray, false, 0, 0}};
  EXPECT_EQ(findings, expected);
  EXPECT_TRUE(HasBlockingFinding(findings));
}

// 열화상 파일이 하나도 없어도 분석할 수 있으므로 막지 않는 경고 하나만 냅니다.
TEST(CheckAnalysisInputs, WarnsThermalAbsent) {
  AnalysisFiles files = GoodFiles();
  files.present = {FileRole::kLeftVideo, FileRole::kLeftSidecar, FileRole::kRightVideo, FileRole::kRightSidecar};
  files.thermal.reset();

  const auto findings = CheckAnalysisInputs(files, kMinThermalFramesInRgbWindow);

  const std::vector<InputFinding> expected{{InputProblem::kThermalAbsent, FileRole::kThermalSidecar, false, 0, 0}};
  EXPECT_EQ(findings, expected);
  EXPECT_FALSE(HasBlockingFinding(findings));
}

// 열화상 사이드카 파일이 있는데 읽지 못하면 열화상만 빼는 경고입니다.
TEST(CheckAnalysisInputs, ReportsUnreadableSidecar) {
  AnalysisFiles files = GoodFiles();
  files.thermal.reset();

  const auto findings = CheckAnalysisInputs(files, kMinThermalFramesInRgbWindow);

  const std::vector<InputFinding> expected{{InputProblem::kUnreadableSidecar, FileRole::kThermalSidecar, false, 0, 0}};
  EXPECT_EQ(findings, expected);
}

// RGB 녹화 구간 안 열화상이 225장보다 적으면 열화상만 빼는 경고입니다. 225장은 통과입니다.
TEST(CheckAnalysisInputs, ReportsFewThermalFrames) {
  AnalysisFiles files = GoodFiles();
  files.thermal = Thermal(224);

  const auto findings = CheckAnalysisInputs(files, kMinThermalFramesInRgbWindow);

  const std::vector<InputFinding> expected{{InputProblem::kFewThermalFrames, FileRole::kThermalSidecar, false, 224, 225}};
  EXPECT_EQ(findings, expected);
  EXPECT_FALSE(HasBlockingFinding(findings));
  files.thermal = Thermal(225);
  EXPECT_TRUE(CheckAnalysisInputs(files, kMinThermalFramesInRgbWindow).empty());
}

// 열화상 누락은 분석을 막지 않는 경고입니다. 카메라 단 누락과 저장 누락을 더해 남깁니다.
TEST(CheckAnalysisInputs, WarnsThermalGap) {
  AnalysisFiles files = GoodFiles();
  files.thermal->missing_frames = 3;
  files.thermal->dropped_frames = 1;

  const auto findings = CheckAnalysisInputs(files, kMinThermalFramesInRgbWindow);

  const std::vector<InputFinding> expected{{InputProblem::kThermalGap, FileRole::kThermalSidecar, false, 4, 0}};
  EXPECT_EQ(findings, expected);
  EXPECT_FALSE(HasBlockingFinding(findings));
}

// 좌우 영상의 저장 영역이 세로로 길지 않으면(높이 <= 폭) 분석을 막습니다.
TEST(CheckAnalysisInputs, ReportsLandscapeVideo) {
  AnalysisFiles files = GoodFiles();
  files.right->crop = {0, 0, 1080, 1080};

  const auto findings = CheckAnalysisInputs(files, kMinThermalFramesInRgbWindow);

  const std::vector<InputFinding> expected{{InputProblem::kNotPortrait, FileRole::kRightVideo, true, 1080, 1080}};
  EXPECT_EQ(findings, expected);
}

}  // namespace
