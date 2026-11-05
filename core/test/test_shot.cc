#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "hwlab/record/shot.h"

namespace {

using hwlab::StreamKind;
using hwlab::record::AnkleLevelMmhg;
using hwlab::record::BloodPressure;
using hwlab::record::BuildManifestJson;
using hwlab::record::CollectedResultPrefix;
using hwlab::record::FileRole;
using hwlab::record::FootSide;
using hwlab::record::IsAnalysisInput;
using hwlab::record::LayoutOf;
using hwlab::record::LightSource;
using hwlab::record::NumberLayout;
using hwlab::record::ShotDevice;
using hwlab::record::ShotInfo;
using hwlab::record::ShotProblem;
using hwlab::record::SplitShotFolderPath;
using hwlab::record::ThermalAbsence;
using hwlab::record::UsesStemPrefix;
using hwlab::record::ValidateShot;

const std::vector<FileRole> kFullFrame{FileRole::kFullVideo, FileRole::kFullSidecar};
const std::vector<FileRole> kAllRoles{FileRole::kLeftVideo,      FileRole::kLeftSidecar,     FileRole::kRightVideo,
                                      FileRole::kRightSidecar,   FileRole::kThermalRaw,      FileRole::kThermalSidecar,
                                      FileRole::kThermalPreview, FileRole::kManifest};

ShotInfo MakeShot() { return {"홍길동", "19551103", {120, 80}, {130, 85}, "M", 2026, 9, 15, 14, 22, 33}; }

// NAS 서버와 같은 이름_생년월일/발목 혈압 단계/날짜_시각 폴더를 만듭니다.
TEST(Shot, BuildsFolderLikeNas) {
  const auto layout = LayoutOf(MakeShot(), "avi", kAllRoles);

  ASSERT_TRUE(layout);
  EXPECT_EQ(layout->folder, "홍길동_19551103/M/20260915_142233");
  EXPECT_EQ(layout->stem, "20260915_142233");
  EXPECT_EQ(layout->NameOf(FileRole::kLeftVideo), "20260915_142233_left.avi");
  EXPECT_EQ(layout->NameOf(FileRole::kThermalRaw), "20260915_142233_thermal.raw16");
  EXPECT_EQ(layout->NameOf(FileRole::kManifest), "20260915_142233_shot.json");
}

// 영상 확장자는 코덱에서 받습니다. 기본 코덱 FFV1은 mkv이고, 열화상 확인용 영상은 avi 그대로입니다.
TEST(Shot, VideoExtensionFollowsCodec) {
  const auto layout = LayoutOf(MakeShot(), "mkv", kAllRoles);

  ASSERT_TRUE(layout);
  EXPECT_EQ(layout->NameOf(FileRole::kLeftVideo), "20260915_142233_left.mkv");
  EXPECT_EQ(layout->NameOf(FileRole::kRightVideo), "20260915_142233_right.mkv");
  EXPECT_EQ(layout->NameOf(FileRole::kThermalPreview), "20260915_142233_thermal.avi");
  EXPECT_FALSE(LayoutOf(MakeShot(), "", kAllRoles));
}

// 전체 화면 녹화는 영상, 사이드카, 매니페스트 세 파일이고, 매니페스트는 요청하지 않아도 들어갑니다.
TEST(Shot, FullFrameShotHasThreeFiles) {
  const auto layout = LayoutOf(MakeShot(), "mkv", kFullFrame);

  ASSERT_TRUE(layout);
  ASSERT_EQ(layout->files.size(), 3u);
  EXPECT_EQ(layout->NameOf(FileRole::kFullVideo), "20260915_142233_full.mkv");
  EXPECT_EQ(layout->NameOf(FileRole::kFullSidecar), "20260915_142233_full.json");
  EXPECT_EQ(layout->NameOf(FileRole::kManifest), "20260915_142233_shot.json");
  EXPECT_EQ(layout->NameOf(FileRole::kLeftVideo), "");
}

// 같은 폴더가 있으면 폴더와 모든 파일 이름의 앞부분에 같은 번호를 붙입니다.
TEST(Shot, NumberedLayoutRenamesFolderAndFiles) {
  const auto layout = LayoutOf(MakeShot(), "mkv", kFullFrame);
  ASSERT_TRUE(layout);

  const auto second = NumberLayout(*layout, 2);

  EXPECT_EQ(second.folder, "홍길동_19551103/M/20260915_142233_2");
  EXPECT_EQ(second.stem, "20260915_142233_2");
  EXPECT_EQ(second.NameOf(FileRole::kFullVideo), "20260915_142233_2_full.mkv");
  EXPECT_EQ(second.NameOf(FileRole::kFullSidecar), "20260915_142233_2_full.json");
  EXPECT_EQ(second.NameOf(FileRole::kManifest), "20260915_142233_2_shot.json");
  EXPECT_EQ(NumberLayout(*layout, 1).folder, layout->folder);
  EXPECT_EQ(NumberLayout(*layout, 12).NameOf(FileRole::kFullVideo), "20260915_142233_12_full.mkv");
}

// 폴더 이름이 되는 이름에 Windows가 거부하는 문자가 있으면 만들지 않습니다.
TEST(Shot, RejectsUnsafeName) {
  for (const char* name : {"홍/길동", "a:b", "홍길동.", " 홍길동", "a\tb", "a*b"}) {
    ShotInfo shot = MakeShot();
    shot.patient_name = name;
    const auto problems = ValidateShot(shot);
    ASSERT_EQ(problems.size(), 1u) << name;
    EXPECT_EQ(problems[0], ShotProblem::kBadNameCharacter) << name;
    EXPECT_FALSE(LayoutOf(shot, "mkv", kFullFrame)) << name;
  }
}

// 없는 날짜는 거부합니다.
TEST(Shot, RejectsBadDates) {
  ShotInfo shot = MakeShot();
  shot.birth_date = "19550230";
  shot.month = 13;

  const auto problems = ValidateShot(shot);

  const std::vector<ShotProblem> expected{ShotProblem::kBadBirthDate, ShotProblem::kBadTime};
  EXPECT_EQ(problems, expected);
}

// 여덟 자리가 아닌 생년월일은 거부합니다.
TEST(Shot, RejectsShortBirthDate) {
  ShotInfo shot = MakeShot();
  shot.birth_date = "1955110";

  const std::vector<ShotProblem> expected{ShotProblem::kBadBirthDate};
  EXPECT_EQ(ValidateShot(shot), expected);
}

// 폴더의 가운데 단계는 고른 발목 혈압 단계입니다. 커프 없이 찍은 촬영은 2026-10-30 전 검사 기준 폴더와 같은 0입니다.
TEST(Shot, FolderMiddleIsAnkleLevel) {
  for (const char* level : {"0", "V", "D", "M", "S"}) {
    ShotInfo shot = MakeShot();
    shot.ankle_level = level;
    const auto layout = LayoutOf(shot, "mkv", kFullFrame);
    ASSERT_TRUE(layout) << level;
    EXPECT_EQ(layout->folder, std::string("홍길동_19551103/") + level + "/20260915_142233");
  }
}

// 혈압은 상완과 발목 모두 있어야 하고, 입력하지 않은 0은 거부합니다.
TEST(Shot, RejectsMissingPressure) {
  ShotInfo shot = MakeShot();
  shot.arm_pressure = {};
  shot.ankle_pressure = {130, 0};

  const std::vector<ShotProblem> expected{ShotProblem::kBadArmPressure, ShotProblem::kBadAnklePressure};
  EXPECT_EQ(ValidateShot(shot), expected);
  EXPECT_FALSE(LayoutOf(shot, "mkv", kFullFrame));
}

// 혈압은 20~300mmHg이고 수축기가 이완기보다 커야 합니다. 범위의 끝 값은 받습니다.
TEST(Shot, ChecksPressureRangeAndOrder) {
  ShotInfo shot = MakeShot();
  shot.arm_pressure = {300, 20};
  shot.ankle_pressure = {21, 20};
  EXPECT_TRUE(ValidateShot(shot).empty());

  const std::vector<ShotProblem> arm{ShotProblem::kBadArmPressure};
  for (const BloodPressure bad : {BloodPressure{301, 80}, {120, 19}, {80, 80}, {80, 120}}) {
    shot.arm_pressure = bad;
    EXPECT_EQ(ValidateShot(shot), arm) << bad.systolic_mmhg << "/" << bad.diastolic_mmhg;
  }
}

// 발목 혈압 단계의 압력은 발목 혈압으로 계산합니다. M은 0.5 단위가 될 수 있습니다.
TEST(Shot, ComputesAnkleLevelFromAnklePressure) {
  const BloodPressure ankle{135, 84};

  EXPECT_EQ(AnkleLevelMmhg("0", ankle).value_or(-1), 0.0);
  EXPECT_EQ(AnkleLevelMmhg("V", ankle).value_or(-1), 34.0);
  EXPECT_EQ(AnkleLevelMmhg("D", ankle).value_or(-1), 84.0);
  EXPECT_EQ(AnkleLevelMmhg("M", ankle).value_or(-1), 109.5);
  EXPECT_EQ(AnkleLevelMmhg("S", ankle).value_or(-1), 135.0);
  EXPECT_FALSE(AnkleLevelMmhg("", ankle));
  EXPECT_FALSE(AnkleLevelMmhg("d", ankle));
}

// 발목 혈압 단계는 골라야 합니다. 발목 이완기가 50mmHg보다 낮아 V가 음수이면 거부합니다.
TEST(Shot, RejectsMissingOrNegativeAnkleLevel) {
  ShotInfo shot = MakeShot();
  const std::vector<ShotProblem> unknown{ShotProblem::kUnknownAnkleLevel};
  for (const char* level : {"", "x"}) {
    shot.ankle_level = level;
    EXPECT_EQ(ValidateShot(shot), unknown) << level;
  }

  shot.ankle_level = "V";
  shot.ankle_pressure = {120, 50};
  EXPECT_TRUE(ValidateShot(shot).empty());

  shot.ankle_pressure = {120, 49};
  const std::vector<ShotProblem> negative{ShotProblem::kNegativeAnkleLevel};
  EXPECT_EQ(ValidateShot(shot), negative);
  EXPECT_FALSE(LayoutOf(shot, "mkv", kFullFrame));
}

// 윤년 2월 29일은 받습니다.
TEST(Shot, AcceptsLeapDay) {
  ShotInfo shot = MakeShot();
  shot.birth_date = "20000229";

  EXPECT_TRUE(ValidateShot(shot).empty());
}

// 매니페스트는 공통 정보와 파일 목록을 한 번만 담습니다.
TEST(Shot, ManifestListsFilesOnce) {
  const ShotInfo shot = MakeShot();
  const auto layout = LayoutOf(shot, "avi", kAllRoles);
  ASSERT_TRUE(layout);

  const std::string json = BuildManifestJson(shot, *layout, {{StreamKind::kRgb, "See3CAM_24CUG"}});

  EXPECT_NE(json.find("\"patient_name\": \"홍길동\""), std::string::npos);
  EXPECT_NE(json.find("\"shot_time\": \"2026-09-15T14:22:33\""), std::string::npos);
  EXPECT_NE(json.find("\"right_sidecar\": \"20260915_142233_right.json\""), std::string::npos);
  // 이름은 patient_name과 folder에만 나옵니다.
  int count = 0;
  for (size_t at = json.find("홍길동"); at != std::string::npos; at = json.find("홍길동", at + 1)) ++count;
  EXPECT_EQ(count, 2);
}

// 같은 기준의 몇 번째 촬영인지와 녹화 전 발 대비를 남깁니다. 발 대비는 열화상을 녹화한 촬영에만 씁니다.
TEST(Shot, ManifestKeepsAttemptAndFootWarmth) {
  ShotInfo shot = MakeShot();
  shot.attempt = 2;
  shot.foot_warmth = hwlab::record::FootWarmthRecord{38, 72, 60, true};
  const auto with_thermal = LayoutOf(shot, "mkv", kAllRoles);
  ASSERT_TRUE(with_thermal);
  const std::string json = BuildManifestJson(shot, *with_thermal, {{StreamKind::kRgb, "See3CAM_24CUG"}});
  EXPECT_NE(json.find("\"attempt\": 2,"), std::string::npos);
  EXPECT_NE(json.find("\"foot_warmth\": {\"left\": 38, \"right\": 72, \"min_warm\": 60, \"cold_confirmed\": true}"),
            std::string::npos);
  const auto rgb_only = LayoutOf(shot, "mkv", kFullFrame);
  ASSERT_TRUE(rgb_only);
  EXPECT_EQ(BuildManifestJson(shot, *rgb_only, {}).find("foot_warmth"), std::string::npos);
}

// 매니페스트의 장치는 스트림 종류와 이름의 목록이고, 전체 바이트가 정해진 형식과 같습니다.
TEST(Shot, ManifestBytesWithDeviceList) {
  const ShotInfo shot = MakeShot();
  const auto layout = LayoutOf(shot, "mkv", kFullFrame);
  ASSERT_TRUE(layout);
  const std::vector<ShotDevice> devices{{StreamKind::kRgb, "See3CAM_24CUG"}, {StreamKind::kThermal, "TE-EV1"}};

  const std::string json = BuildManifestJson(shot, NumberLayout(*layout, 2), devices);

  const std::string expected =
      "{\n"
      "  \"schema_version\": 7,\n"
      "  \"patient_name\": \"홍길동\",\n"
      "  \"birth_date\": \"19551103\",\n"
      "  \"blood_pressure\": {\"arm\": {\"systolic_mmhg\": 120, \"diastolic_mmhg\": 80}, "
      "\"ankle\": {\"systolic_mmhg\": 130, \"diastolic_mmhg\": 85}, "
      "\"ankle_level\": {\"name\": \"M\", \"pressure_mmhg\": 107.5}},\n"
      "  \"shot_time\": \"2026-09-15T14:22:33\",\n"
      "  \"attempt\": null,\n"
      "  \"app\": {\"version\": null, \"commit\": null},\n"
      "  \"spec\": {\"file\": null, \"sha256\": null},\n"
      "  \"pixel_left_foot\": \"right\",\n"
      "  \"lighting\": {\"source\": \"unknown\", \"product\": \"\", \"brightness_percent\": null},\n"
      "  \"thermal_recorded\": false,\n"
      "  \"thermal_absent_reason\": \"unknown\",\n"
      "  \"thermal_skip_confirmed\": false,\n"
      "  \"devices\": [\n"
      "    {\"stream\": \"rgb\", \"name\": \"See3CAM_24CUG\"},\n"
      "    {\"stream\": \"thermal\", \"name\": \"TE-EV1\"}\n"
      "  ],\n"
      "  \"folder\": \"홍길동_19551103/M/20260915_142233_2\",\n"
      "  \"files\": {\n"
      "    \"full_video\": \"20260915_142233_2_full.mkv\",\n"
      "    \"full_sidecar\": \"20260915_142233_2_full.json\",\n"
      "    \"manifest\": \"20260915_142233_2_shot.json\"\n"
      "  },\n"
      "  \"analysis_inputs\": []\n"
      "}\n";
  EXPECT_EQ(json, expected);
  EXPECT_EQ(json, BuildManifestJson(shot, NumberLayout(*layout, 2), devices));
}

// 장치 이름의 따옴표와 제어 문자는 JSON 이스케이프로 씁니다.
TEST(Shot, ManifestEscapesDeviceName) {
  const ShotInfo shot = MakeShot();
  const auto layout = LayoutOf(shot, "mkv", kFullFrame);
  ASSERT_TRUE(layout);

  const std::string json = BuildManifestJson(shot, *layout, {{StreamKind::kRgb, "cam\"1\"\n"}});

  EXPECT_NE(json.find("\"name\": \"cam\\\"1\\\"\\u000a\""), std::string::npos);
}

const std::vector<FileRole> kAnalysisRoles{FileRole::kLeftVideo,         FileRole::kLeftSidecar,
                                           FileRole::kRightVideo,        FileRole::kRightSidecar,
                                           FileRole::kThermalRaw,        FileRole::kThermalSidecar,
                                           FileRole::kThermalLeftArray,  FileRole::kThermalRightArray};

// 열화상 .npy는 시각 접두 없이 관류 분석이 읽는 고정 이름이고, 나머지는 시각으로 시작합니다.
TEST(Shot, ThermalArraysHaveFixedNames) {
  const auto layout = LayoutOf(MakeShot(), "mkv", kAnalysisRoles);
  ASSERT_TRUE(layout);

  EXPECT_EQ(layout->NameOf(FileRole::kThermalLeftArray), "thermal_left.npy");
  EXPECT_EQ(layout->NameOf(FileRole::kThermalRightArray), "thermal_right.npy");
  EXPECT_EQ(layout->NameOf(FileRole::kLeftVideo), "20260915_142233_left.mkv");
  EXPECT_FALSE(UsesStemPrefix(FileRole::kThermalLeftArray));
  EXPECT_FALSE(UsesStemPrefix(FileRole::kThermalRightArray));
  EXPECT_TRUE(UsesStemPrefix(FileRole::kThermalRaw));
  EXPECT_TRUE(UsesStemPrefix(FileRole::kManifest));
}

// 번호 폴더에서 시각으로 시작하는 이름만 번호를 넣고 .npy 이름은 그대로 둡니다.
TEST(Shot, NumberedLayoutKeepsArrayNames) {
  const auto layout = LayoutOf(MakeShot(), "mkv", kAnalysisRoles);
  ASSERT_TRUE(layout);

  const auto third = NumberLayout(*layout, 3);

  EXPECT_EQ(third.folder, "홍길동_19551103/M/20260915_142233_3");
  EXPECT_EQ(third.NameOf(FileRole::kThermalLeftArray), "thermal_left.npy");
  EXPECT_EQ(third.NameOf(FileRole::kThermalRightArray), "thermal_right.npy");
  EXPECT_EQ(third.NameOf(FileRole::kRightVideo), "20260915_142233_3_right.mkv");
  EXPECT_EQ(third.NameOf(FileRole::kThermalRaw), "20260915_142233_3_thermal.raw16");
  EXPECT_EQ(third.NameOf(FileRole::kManifest), "20260915_142233_3_shot.json");
}

// 분석 입력은 좌우 영상과 좌우 열화상 .npy 네 역할입니다.
TEST(Shot, AnalysisInputRoles) {
  EXPECT_TRUE(IsAnalysisInput(FileRole::kLeftVideo));
  EXPECT_TRUE(IsAnalysisInput(FileRole::kRightVideo));
  EXPECT_TRUE(IsAnalysisInput(FileRole::kThermalLeftArray));
  EXPECT_TRUE(IsAnalysisInput(FileRole::kThermalRightArray));
  EXPECT_FALSE(IsAnalysisInput(FileRole::kFullVideo));
  EXPECT_FALSE(IsAnalysisInput(FileRole::kLeftSidecar));
  EXPECT_FALSE(IsAnalysisInput(FileRole::kThermalRaw));
}

// 매니페스트는 화소 왼쪽 발과 분석 입력 역할 목록(파일 목록 순서)을 남깁니다.
TEST(Shot, ManifestListsAnalysisInputsAndFoot) {
  ShotInfo shot = MakeShot();
  shot.pixel_left_foot = FootSide::kLeft;
  const auto layout = LayoutOf(shot, "mkv", kAnalysisRoles);
  ASSERT_TRUE(layout);

  const std::string json = BuildManifestJson(shot, *layout, {{StreamKind::kRgb, "See3CAM_24CUG"}});

  EXPECT_NE(json.find("\"schema_version\": 7,"), std::string::npos);
  EXPECT_NE(json.find("\"pixel_left_foot\": \"left\","), std::string::npos);
  EXPECT_NE(json.find("\"thermal_left_array\": \"thermal_left.npy\""), std::string::npos);
  EXPECT_NE(json.find("\"analysis_inputs\": [\"left_video\", \"right_video\", \"thermal_left_array\", "
                      "\"thermal_right_array\"]\n}\n"),
            std::string::npos);
}

// 앱 밖에서 켠 조명은 출처와 제품을 남기고 밝기는 null입니다. 열화상 역할이 있으면 녹화 이유를 쓰지 않습니다.
TEST(Shot, ManifestWritesExternalLightingAndThermalRecorded) {
  ShotInfo shot = MakeShot();
  shot.lighting = {LightSource::kExternal, "4000K COB LED, 12V", std::nullopt};
  shot.thermal_absence = ThermalAbsence::kNotReceiving;
  const auto layout = LayoutOf(shot, "mkv", kAnalysisRoles);
  ASSERT_TRUE(layout);

  const std::string json = BuildManifestJson(shot, *layout, {{StreamKind::kRgb, "See3CAM_24CUG"}});

  EXPECT_NE(json.find("\"lighting\": {\"source\": \"external\", \"product\": \"4000K COB LED, 12V\", "
                      "\"brightness_percent\": null},\n  \"thermal_recorded\": true,\n  \"devices\""),
            std::string::npos);
  EXPECT_EQ(json.find("thermal_absent_reason"), std::string::npos);
}

// 열화상 역할이 없는 촬영은 thermal_recorded가 false이고 이유와 사용자 확인 여부를 남깁니다. 앱이 보낸 밝기는 숫자입니다.
TEST(Shot, ManifestWritesThermalAbsentReason) {
  ShotInfo shot = MakeShot();
  shot.lighting = {LightSource::kApp, "", 80};
  shot.thermal_absence = ThermalAbsence::kPreparing;
  shot.thermal_skip_confirmed = true;
  const auto layout = LayoutOf(shot, "mkv", kFullFrame);
  ASSERT_TRUE(layout);

  const std::string json = BuildManifestJson(shot, *layout, {{StreamKind::kRgb, "See3CAM_24CUG"}});

  EXPECT_NE(json.find("\"source\": \"app\", \"product\": \"\", \"brightness_percent\": 80}"), std::string::npos);
  EXPECT_NE(json.find("\"thermal_recorded\": false,\n  \"thermal_absent_reason\": \"preparing\",\n"
                      "  \"thermal_skip_confirmed\": true,\n"),
            std::string::npos);
}

// 앱 버전, 커밋, 촬영 선언 파일 이름과 SHA-256을 남깁니다. 개발 빌드처럼 버전이 비어 있으면 null입니다.
TEST(Shot, ManifestWritesAppAndSpec) {
  ShotInfo shot = MakeShot();
  shot.app_commit = "2c80764";
  shot.spec_file = "see3cam_auto_30fps.json";
  shot.spec_sha256 = "ab12";
  const auto layout = LayoutOf(shot, "mkv", kFullFrame);
  ASSERT_TRUE(layout);

  std::string json = BuildManifestJson(shot, *layout, {{StreamKind::kRgb, "See3CAM_24CUG"}});
  EXPECT_NE(json.find("\"app\": {\"version\": null, \"commit\": \"2c80764\"},\n"
                      "  \"spec\": {\"file\": \"see3cam_auto_30fps.json\", \"sha256\": \"ab12\"},\n"),
            std::string::npos);

  shot.app_version = "v0.1.2";
  json = BuildManifestJson(shot, *layout, {{StreamKind::kRgb, "See3CAM_24CUG"}});
  EXPECT_NE(json.find("\"app\": {\"version\": \"v0.1.2\", \"commit\": \"2c80764\"}"), std::string::npos);
}

// 분석결과 파일 이름 앞부분은 폴더 경로 단계 전체라, 같은 시각이어도 날짜, 환자, 기준, 재촬영 번호가 다르면 다릅니다.
TEST(Shot, CollectedResultPrefixIsUniquePerShotFolder) {
  const auto layout = LayoutOf(MakeShot(), "mkv", kAllRoles);
  ASSERT_TRUE(layout);
  EXPECT_EQ(CollectedResultPrefix(*layout), "홍길동_19551103_M_20260915_142233");
  EXPECT_EQ(CollectedResultPrefix(NumberLayout(*layout, 2)), "홍길동_19551103_M_20260915_142233_2");
  ShotInfo other_day = MakeShot();
  other_day.day = 16;
  const auto day_layout = LayoutOf(other_day, "mkv", kAllRoles);
  ASSERT_TRUE(day_layout);
  EXPECT_NE(CollectedResultPrefix(*day_layout), CollectedResultPrefix(*layout));
  ShotInfo other_patient = MakeShot();
  other_patient.patient_name = "김철수";
  const auto patient_layout = LayoutOf(other_patient, "mkv", kAllRoles);
  ASSERT_TRUE(patient_layout);
  EXPECT_NE(CollectedResultPrefix(*patient_layout), CollectedResultPrefix(*layout));
  EXPECT_EQ(CollectedResultPrefix(hwlab::record::ShotLayout{}), "");
}

// 촬영 폴더 경로의 마지막 세 단계가 폴더 배치이고 그 앞이 저장 위치입니다. 두 구분자를 모두 받습니다.
TEST(Shot, SplitShotFolderPathTakesLastThreeLevels) {
  const auto layout = LayoutOf(MakeShot(), "mkv", kAllRoles);
  ASSERT_TRUE(layout);
  const auto split = SplitShotFolderPath("C:\\data\\촬영/" + layout->folder + "/");
  ASSERT_TRUE(split);
  EXPECT_EQ(split->root, "C:\\data\\촬영");
  EXPECT_EQ(split->layout.folder, layout->folder);
  EXPECT_EQ(split->layout.stem, layout->stem);
  EXPECT_EQ(CollectedResultPrefix(split->layout), CollectedResultPrefix(*layout));

  const auto bare = SplitShotFolderPath("a_19551103/0/20260915_142233_2");
  ASSERT_TRUE(bare);
  EXPECT_EQ(bare->root, "");
  EXPECT_EQ(bare->layout.folder, "a_19551103/0/20260915_142233_2");
  EXPECT_EQ(bare->layout.stem, "20260915_142233_2");
  const auto unc = SplitShotFolderPath("\\\\nas\\share\\a_19551103\\0\\20260915_142233_12");
  ASSERT_TRUE(unc);
  EXPECT_EQ(unc->root, "\\\\nas\\share");
  EXPECT_EQ(unc->layout.folder, "a_19551103/0/20260915_142233_12");
  EXPECT_EQ(unc->layout.stem, "20260915_142233_12");
  EXPECT_FALSE(SplitShotFolderPath("0/20260915_142233"));
  EXPECT_FALSE(SplitShotFolderPath("a/b/c"));
  EXPECT_FALSE(SplitShotFolderPath("x/a//c/d"));
  EXPECT_FALSE(SplitShotFolderPath(""));
}

// 이전 배치(날짜/이름_생년월일/기준/시각)로 저장한 촬영은 마지막 네 단계를 배치로 보고, 결과 이름 앞부분도 전과 같습니다.
TEST(Shot, SplitShotFolderPathReadsPreviousLayout) {
  const auto split = SplitShotFolderPath("C:\\data\\촬영\\20260915\\홍길동_19551103\\40\\142233\\");
  ASSERT_TRUE(split);
  EXPECT_EQ(split->root, "C:\\data\\촬영");
  EXPECT_EQ(split->layout.folder, "20260915/홍길동_19551103/40/142233");
  EXPECT_EQ(split->layout.stem, "142233");
  EXPECT_EQ(CollectedResultPrefix(split->layout), "20260915_홍길동_19551103_40_142233");

  const auto bare = SplitShotFolderPath("20260915/a_19551103/0/142233_2");
  ASSERT_TRUE(bare);
  EXPECT_EQ(bare->root, "");
  EXPECT_EQ(bare->layout.stem, "142233_2");
  EXPECT_FALSE(SplitShotFolderPath("a_19551103/0/142233"));
}

// 다시 분석한 결과의 앞부분은 끝에 _reanalysis<번호>를 더하고, 번호 1 이하는 처음 모음과 같습니다.
TEST(Shot, CollectedResultPrefixNumbersReanalysis) {
  const auto layout = LayoutOf(MakeShot(), "mkv", kAllRoles);
  ASSERT_TRUE(layout);
  EXPECT_EQ(CollectedResultPrefix(*layout, 1), CollectedResultPrefix(*layout));
  EXPECT_EQ(CollectedResultPrefix(*layout, 0), CollectedResultPrefix(*layout));
  EXPECT_EQ(CollectedResultPrefix(*layout, 2), "홍길동_19551103_M_20260915_142233_reanalysis2");
  EXPECT_EQ(CollectedResultPrefix(NumberLayout(*layout, 2), 3), "홍길동_19551103_M_20260915_142233_2_reanalysis3");
  EXPECT_EQ(CollectedResultPrefix(hwlab::record::ShotLayout{}, 2), "");
}

}  // namespace
