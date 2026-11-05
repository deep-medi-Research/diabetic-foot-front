#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "hwlab/platform/shot_folder.h"
#include "hwlab/record/analysis_inputs.h"
#include "hwlab/record/shot.h"
#include "hwlab/record/sidecar.h"

namespace {

using hwlab::StreamKind;
using hwlab::platform::CheckShotLimits;
using hwlab::platform::CreateFreshShotFolder;
using hwlab::platform::CreateShotFolder;
using hwlab::platform::DiscardUnrecordedShot;
using hwlab::platform::DescribeFreeSpace;
using hwlab::platform::DescribeSaveFailure;
using hwlab::platform::FreeBytesAt;
using hwlab::platform::FreeSpaceNotice;
using hwlab::platform::kFreeSpaceReserveBytes;
using hwlab::platform::OpenFileUtf8;
using hwlab::platform::PathFromUtf8;
using hwlab::platform::PathOfFile;
using hwlab::platform::PlanShotLayout;
using hwlab::platform::PrepareShot;
using hwlab::platform::PreparedShot;
using hwlab::platform::ReadAnalysisFiles;
using hwlab::platform::SavedVideos;
using hwlab::platform::WriteThermalArrays;
using hwlab::platform::WriteManifest;
using hwlab::record::BuildManifestJson;
using hwlab::record::CheckAnalysisInputs;
using hwlab::record::FileRole;
using hwlab::record::FootSide;
using hwlab::record::InputProblem;
using hwlab::record::kMinThermalFramesInRgbWindow;
using hwlab::record::LayoutOf;
using hwlab::flow::VideoCodec;
using hwlab::record::ShotDevice;
using hwlab::record::ShotInfo;
using hwlab::record::ShotLayout;


ShotInfo MakeShot() { return {"홍길동", "19551103", {120, 80}, {130, 85}, "M", 2026, 9, 15, 14, 22, 33}; }

ShotLayout MakeLayout(const ShotInfo& shot) {
  return *LayoutOf(shot, "mkv", {FileRole::kFullVideo, FileRole::kFullSidecar});
}

// 테스트마다 비어 있는 저장 위치를 씁니다. 저장 위치 이름에도 한글을 넣어 경로 변환을 함께 봅니다.
std::string FreshRoot(const char* name) {
  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_shot_folder" / name / PathFromUtf8("저장");
  std::filesystem::remove_all(dir);
  const std::u8string text = dir.u8string();
  return std::string(text.begin(), text.end());
}

std::string ReadBytes(const std::string& utf8_path) {
  std::ifstream file(PathFromUtf8(utf8_path), std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

// 한글 이름이 든 촬영 폴더를 단계마다 만들고, 그 안에 파일을 열 수 있습니다.
TEST(ShotFolder, CreatesKoreanFolder) {
  const std::string root = FreshRoot("korean");
  const ShotLayout layout = MakeLayout(MakeShot());
  std::string error;

  ASSERT_TRUE(CreateShotFolder(root, layout, &error)) << error;

  const auto folder = PathFromUtf8(root) / PathFromUtf8("홍길동_19551103") / "M" / "20260915_142233";
  EXPECT_TRUE(std::filesystem::is_directory(folder));
  const std::string video = PathOfFile(root, layout, FileRole::kFullVideo);
  std::FILE* file = OpenFileUtf8(video, "wb");
  ASSERT_NE(file, nullptr);
  std::fclose(file);
  EXPECT_TRUE(std::filesystem::exists(folder / "20260915_142233_full.mkv"));
}

// 같은 환자와 기준으로 다시 찍어도 환자 폴더의 수정 시각이 새 촬영 시각으로 바뀝니다(최근 환자 목록이 이 값을 씁니다).
TEST(ShotFolder, TouchesPatientFolder) {
  const std::string root = FreshRoot("touch_patient");
  std::string error;
  ASSERT_TRUE(CreateShotFolder(root, MakeLayout(MakeShot()), &error)) << error;
  const auto patient = PathFromUtf8(root) / PathFromUtf8("홍길동_19551103");
  const auto day_ago = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
  std::filesystem::last_write_time(patient, day_ago);

  ShotInfo later = MakeShot();
  later.second = 34;
  ASSERT_TRUE(CreateShotFolder(root, MakeLayout(later), &error)) << error;
  EXPECT_GT(std::filesystem::last_write_time(patient), day_ago + std::chrono::hours(23));
}

// 같은 촬영 폴더가 이미 있으면 만들지 않고 이유를 남깁니다. 안에 있던 파일은 그대로입니다.
TEST(ShotFolder, RejectsExistingFolder) {
  const std::string root = FreshRoot("existing");
  const ShotLayout layout = MakeLayout(MakeShot());
  std::string error;
  ASSERT_TRUE(CreateShotFolder(root, layout, &error)) << error;
  const std::string video = PathOfFile(root, layout, FileRole::kFullVideo);
  std::FILE* file = OpenFileUtf8(video, "wb");
  ASSERT_NE(file, nullptr);
  std::fputs("first", file);
  std::fclose(file);

  EXPECT_FALSE(CreateShotFolder(root, layout, &error));
  EXPECT_NE(error.find("이미 있음"), std::string::npos);
  EXPECT_EQ(ReadBytes(video), "first");
}

// 같은 초에 다시 찍으면 번호 2, 3을 붙인 폴더를 새로 만듭니다.
TEST(ShotFolder, NumbersFolderWhenTaken) {
  const std::string root = FreshRoot("numbered");
  const ShotLayout layout = MakeLayout(MakeShot());
  std::string error;

  const auto first = CreateFreshShotFolder(root, layout, 99, &error);
  const auto second = CreateFreshShotFolder(root, layout, 99, &error);
  const auto third = CreateFreshShotFolder(root, layout, 99, &error);

  ASSERT_TRUE(first && second && third) << error;
  EXPECT_EQ(first->stem, "20260915_142233");
  EXPECT_EQ(second->stem, "20260915_142233_2");
  EXPECT_EQ(third->stem, "20260915_142233_3");
  EXPECT_EQ(third->NameOf(FileRole::kFullVideo), "20260915_142233_3_full.mkv");
  EXPECT_FALSE(CreateFreshShotFolder(root, layout, 3, &error));
}

// 매니페스트는 BuildManifestJson의 바이트를 그대로 쓰고, 이미 있으면 덮어쓰지 않습니다.
TEST(ShotFolder, WritesManifestBytes) {
  const std::string root = FreshRoot("manifest");
  const ShotInfo shot = MakeShot();
  const ShotLayout layout = MakeLayout(shot);
  const std::string json = BuildManifestJson(shot, layout, {{StreamKind::kRgb, "FakeCamera"}});
  std::string error;
  ASSERT_TRUE(CreateShotFolder(root, layout, &error)) << error;

  ASSERT_TRUE(WriteManifest(root, layout, json, &error)) << error;

  const std::string path = PathOfFile(root, layout, FileRole::kManifest);
  EXPECT_EQ(ReadBytes(path), json);
  EXPECT_FALSE(WriteManifest(root, layout, "{}\n", &error));
  EXPECT_EQ(ReadBytes(path), json);
}

const std::vector<ShotDevice> kRgbOnly{{StreamKind::kRgb, "FakeCamera"}};
const std::vector<ShotDevice> kRgbAndThermal{{StreamKind::kRgb, "FakeCamera"}, {StreamKind::kThermal, "FakeThermal"}};

void WriteBytes(const std::string& utf8_path, const char* text) {
  std::FILE* file = OpenFileUtf8(utf8_path, "wb");
  ASSERT_NE(file, nullptr);
  std::fputs(text, file);
  std::fclose(file);
}

// RGB만 쓰면 영상, 사이드카, 매니페스트만 정하고 열화상 경로는 비웁니다. 매니페스트는 BuildManifestJson의 바이트입니다.
TEST(PrepareShot, PreparesRgbOnlyShot) {
  const std::string root = FreshRoot("prepare_rgb");
  const ShotInfo shot = MakeShot();
  std::string error;

  const auto prepared = PrepareShot(root, shot, VideoCodec::kFfv1, SavedVideos::kFull, kRgbOnly, 99, &error);

  ASSERT_TRUE(prepared) << error;
  EXPECT_TRUE(std::filesystem::is_directory(PathFromUtf8(prepared->folder)));
  EXPECT_EQ(prepared->video, PathOfFile(root, prepared->layout, FileRole::kFullVideo));
  EXPECT_EQ(PathFromUtf8(prepared->video).filename(), "20260915_142233_full.mkv");
  EXPECT_EQ(PathFromUtf8(prepared->sidecar).filename(), "20260915_142233_full.json");
  EXPECT_TRUE(prepared->thermal_raw.empty());
  EXPECT_TRUE(prepared->thermal_sidecar.empty());
  EXPECT_TRUE(prepared->thermal_preview.empty());
  EXPECT_EQ(ReadBytes(prepared->manifest), BuildManifestJson(shot, prepared->layout, kRgbOnly));
}

// 열화상 장치가 있으면 열화상 역할 3개가 생기고, 매니페스트의 장치 목록과 파일 목록에 열화상이 들어갑니다.
TEST(PrepareShot, AddsThermalRolesWithThermalDevice) {
  const std::string root = FreshRoot("prepare_thermal");
  std::string error;

  const auto prepared = PrepareShot(root, MakeShot(), VideoCodec::kUncompressed, SavedVideos::kFull, kRgbAndThermal, 99, &error);

  ASSERT_TRUE(prepared) << error;
  EXPECT_EQ(PathFromUtf8(prepared->video).filename(), "20260915_142233_full.avi");
  EXPECT_EQ(PathFromUtf8(prepared->thermal_raw).filename(), "20260915_142233_thermal.raw16");
  EXPECT_EQ(PathFromUtf8(prepared->thermal_sidecar).filename(), "20260915_142233_thermal.json");
  EXPECT_EQ(PathFromUtf8(prepared->thermal_preview).filename(), "20260915_142233_thermal.avi");
  EXPECT_EQ(PathFromUtf8(prepared->thermal_raw).parent_path(), PathFromUtf8(prepared->folder));
  const std::string manifest = ReadBytes(prepared->manifest);
  EXPECT_NE(manifest.find("{\"stream\": \"thermal\", \"name\": \"FakeThermal\"}"), std::string::npos);
  EXPECT_NE(manifest.find("\"thermal_raw\": \"20260915_142233_thermal.raw16\""), std::string::npos);
  EXPECT_NE(manifest.find("\"thermal_sidecar\": \"20260915_142233_thermal.json\""), std::string::npos);
  EXPECT_NE(manifest.find("\"thermal_preview\": \"20260915_142233_thermal.avi\""), std::string::npos);
}

// 같은 초의 촬영 폴더가 이미 있으면 번호를 넣은 폴더에 준비하고, 앞 촬영의 파일은 그대로 둡니다.
TEST(PrepareShot, NumbersFolderWhenTaken) {
  const std::string root = FreshRoot("prepare_numbered");
  std::string error;
  const auto first = PrepareShot(root, MakeShot(), VideoCodec::kFfv1, SavedVideos::kFull, kRgbAndThermal, 99, &error);
  ASSERT_TRUE(first) << error;
  const std::string first_manifest = ReadBytes(first->manifest);

  const auto second = PrepareShot(root, MakeShot(), VideoCodec::kFfv1, SavedVideos::kFull, kRgbAndThermal, 99, &error);

  ASSERT_TRUE(second) << error;
  EXPECT_EQ(second->layout.stem, "20260915_142233_2");
  EXPECT_EQ(PathFromUtf8(second->folder).filename(), "20260915_142233_2");
  EXPECT_EQ(PathFromUtf8(second->video).filename(), "20260915_142233_2_full.mkv");
  EXPECT_EQ(PathFromUtf8(second->thermal_raw).filename(), "20260915_142233_2_thermal.raw16");
  EXPECT_NE(ReadBytes(second->manifest).find("\"folder\": \"홍길동_19551103/M/20260915_142233_2\""),
            std::string::npos);
  EXPECT_EQ(ReadBytes(first->manifest), first_manifest);
  EXPECT_FALSE(PrepareShot(root, MakeShot(), VideoCodec::kFfv1, SavedVideos::kFull, kRgbOnly, 2, &error));
  EXPECT_FALSE(error.empty());
}

// 촬영 정보가 검증을 통과하지 않으면 폴더를 만들지 않고 이유를 남깁니다.
TEST(PrepareShot, RejectsInvalidShotWithoutFolder) {
  const std::string root = FreshRoot("prepare_invalid");
  ShotInfo shot = MakeShot();
  shot.ankle_level = "x";
  std::string error;

  EXPECT_FALSE(PrepareShot(root, shot, VideoCodec::kFfv1, SavedVideos::kFull, kRgbOnly, 99, &error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(std::filesystem::exists(PathFromUtf8(root)));
}

// 한글 저장 위치와 한글 이름 폴더에 준비한 경로로 파일을 열 수 있습니다.
TEST(PrepareShot, PreparesKoreanFolder) {
  const std::string root = FreshRoot("prepare_korean");
  ShotInfo shot = MakeShot();
  shot.patient_name = "김철수 환자";
  std::string error;

  const auto prepared = PrepareShot(root, shot, VideoCodec::kFfv1, SavedVideos::kFull, kRgbAndThermal, 99, &error);

  ASSERT_TRUE(prepared) << error;
  const auto folder = PathFromUtf8(root) / PathFromUtf8("김철수 환자_19551103") / "M" / "20260915_142233";
  EXPECT_TRUE(std::filesystem::equivalent(PathFromUtf8(prepared->folder), folder));
  WriteBytes(prepared->thermal_raw, "raw");
  EXPECT_TRUE(std::filesystem::exists(folder / "20260915_142233_thermal.raw16"));
  EXPECT_NE(ReadBytes(prepared->manifest).find("\"patient_name\": \"김철수 환자\""), std::string::npos);
}

// 영상 없이 끝난 촬영은 매니페스트와 빈 촬영 폴더를 지우고, 상위 폴더는 남깁니다.
TEST(DiscardUnrecordedShot, RemovesManifestAndEmptyFolder) {
  const std::string root = FreshRoot("discard_empty");
  std::string error;
  const auto prepared = PrepareShot(root, MakeShot(), VideoCodec::kFfv1, SavedVideos::kFull, kRgbAndThermal, 99, &error);
  ASSERT_TRUE(prepared) << error;

  EXPECT_TRUE(DiscardUnrecordedShot(*prepared));

  EXPECT_FALSE(std::filesystem::exists(PathFromUtf8(prepared->manifest)));
  EXPECT_FALSE(std::filesystem::exists(PathFromUtf8(prepared->folder)));
  EXPECT_TRUE(std::filesystem::is_directory(PathFromUtf8(prepared->folder).parent_path()));
}

// 영상 파일이 있는 촬영은 지우지 않습니다.
TEST(DiscardUnrecordedShot, KeepsRecordedShot) {
  const std::string root = FreshRoot("discard_recorded");
  std::string error;
  const auto prepared = PrepareShot(root, MakeShot(), VideoCodec::kFfv1, SavedVideos::kFull, kRgbOnly, 99, &error);
  ASSERT_TRUE(prepared) << error;
  WriteBytes(prepared->video, "video");

  EXPECT_FALSE(DiscardUnrecordedShot(*prepared));

  EXPECT_TRUE(std::filesystem::exists(PathFromUtf8(prepared->manifest)));
  EXPECT_EQ(ReadBytes(prepared->video), "video");
}

// 영상이 없어도 매니페스트 말고 다른 파일(열화상 원시값)이 있으면 매니페스트와 폴더를 모두 남깁니다.
TEST(DiscardUnrecordedShot, KeepsFolderWithOtherFiles) {
  const std::string root = FreshRoot("discard_other");
  std::string error;
  const auto prepared = PrepareShot(root, MakeShot(), VideoCodec::kFfv1, SavedVideos::kFull, kRgbAndThermal, 99, &error);
  ASSERT_TRUE(prepared) << error;
  const std::string manifest = ReadBytes(prepared->manifest);
  WriteBytes(prepared->thermal_raw, "raw");

  EXPECT_FALSE(DiscardUnrecordedShot(*prepared));

  EXPECT_EQ(ReadBytes(prepared->manifest), manifest);
  EXPECT_EQ(ReadBytes(prepared->thermal_raw), "raw");
}

// 좌우 분할과 열화상 장치가 있으면 좌우 영상, 좌우 .npy 경로가 생기고 전체 화면 영상은 없습니다.
TEST(PrepareShot, AddsFeetAndArrayRoles) {
  const std::string root = FreshRoot("prepare_feet");
  ShotInfo shot = MakeShot();
  shot.pixel_left_foot = FootSide::kLeft;
  std::string error;

  const auto prepared = PrepareShot(root, shot, VideoCodec::kFfv1, SavedVideos::kFeet, kRgbAndThermal, 99,
                                    &error);

  ASSERT_TRUE(prepared) << error;
  EXPECT_TRUE(prepared->video.empty());
  EXPECT_TRUE(prepared->sidecar.empty());
  EXPECT_EQ(PathFromUtf8(prepared->left_video).filename(), "20260915_142233_left.mkv");
  EXPECT_EQ(PathFromUtf8(prepared->right_sidecar).filename(), "20260915_142233_right.json");
  EXPECT_EQ(PathFromUtf8(prepared->thermal_left_array).filename(), "thermal_left.npy");
  EXPECT_EQ(PathFromUtf8(prepared->thermal_right_array).filename(), "thermal_right.npy");
  EXPECT_EQ(prepared->pixel_left_foot, FootSide::kLeft);
  const std::string manifest = ReadBytes(prepared->manifest);
  EXPECT_NE(manifest.find("\"pixel_left_foot\": \"left\""), std::string::npos);
  EXPECT_NE(manifest.find("\"analysis_inputs\": [\"left_video\", \"right_video\", \"thermal_left_array\", "
                          "\"thermal_right_array\"]"),
            std::string::npos);

  const auto both = PrepareShot(root, shot, VideoCodec::kFfv1, SavedVideos::kFullAndFeet, kRgbOnly, 99,
                                &error);
  ASSERT_TRUE(both) << error;
  EXPECT_FALSE(both->video.empty());
  EXPECT_FALSE(both->left_video.empty());
  EXPECT_TRUE(both->thermal_left_array.empty());
}

// 번호 폴더에서도 .npy는 접두 없는 이름 그대로 번호 폴더 안에 둡니다.
TEST(PrepareShot, NumberedFolderKeepsArrayNames) {
  const std::string root = FreshRoot("prepare_numbered_npy");
  std::string error;
  ASSERT_TRUE(PrepareShot(root, MakeShot(), VideoCodec::kFfv1, SavedVideos::kFeet, kRgbAndThermal, 99,
                          &error));

  const auto second = PrepareShot(root, MakeShot(), VideoCodec::kFfv1, SavedVideos::kFeet, kRgbAndThermal,
                                  99, &error);

  ASSERT_TRUE(second) << error;
  EXPECT_EQ(PathFromUtf8(second->thermal_left_array).filename(), "thermal_left.npy");
  EXPECT_EQ(PathFromUtf8(second->thermal_left_array).parent_path().filename(), "20260915_142233_2");
  EXPECT_EQ(PathFromUtf8(second->left_video).filename(), "20260915_142233_2_left.mkv");
  EXPECT_NE(ReadBytes(second->manifest).find("\"thermal_right_array\": \"thermal_right.npy\""), std::string::npos);
}

// 좌우 분할 영상만 있는 촬영(전체 화면 영상 없음)도 영상이 있는 촬영으로 보고 지우지 않습니다.
TEST(DiscardUnrecordedShot, KeepsShotWithOnlyFootVideo) {
  const std::string root = FreshRoot("discard_feet");
  std::string error;
  const auto prepared = PrepareShot(root, MakeShot(), VideoCodec::kFfv1, SavedVideos::kFeet, kRgbOnly, 99,
                                    &error);
  ASSERT_TRUE(prepared) << error;
  WriteBytes(prepared->right_video, "video");

  EXPECT_FALSE(DiscardUnrecordedShot(*prepared));

  EXPECT_TRUE(std::filesystem::exists(PathFromUtf8(prepared->manifest)));
  EXPECT_EQ(ReadBytes(prepared->right_video), "video");
}

// 열화상 변환 시험용: 화소 값이 장 번호 * 1000 + 행 * 10 + 열인 원시값 파일과 열화상 사이드카를 씁니다.
class ThermalArrays : public ::testing::Test {
 protected:
  static constexpr int32_t kWidth = 6;
  static constexpr int32_t kHeight = 4;
  static constexpr int32_t kFrames = 3;

  void Prepare(const char* name, int32_t width, int32_t height, int64_t sidecar_frames) {
    root_ = FreshRoot(name);
    std::string error;
    const auto prepared = PrepareShot(root_, MakeShot(), VideoCodec::kFfv1, SavedVideos::kFeet,
                                      kRgbAndThermal, 99, &error);
    ASSERT_TRUE(prepared) << error;
    shot_ = *prepared;
    std::string raw;
    for (int32_t f = 0; f < kFrames; ++f) {
      for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
          const auto v = static_cast<uint16_t>(f * 1000 + y * 10 + x);
          raw.push_back(static_cast<char>(v & 0xFF));
          raw.push_back(static_cast<char>(v >> 8));
        }
      }
    }
    WriteBytes(shot_.thermal_raw, raw);
    hwlab::record::Sidecar sidecar;
    sidecar.stream = StreamKind::kThermal;
    sidecar.format = {hwlab::PixelFormat::kY16, width, height, 30};
    sidecar.crop = {0, 0, width, height};
    sidecar.written_frames = sidecar_frames;
    sidecar.written_in_rgb_window = 270;
    WriteBytes(shot_.thermal_sidecar, hwlab::record::BuildSidecarJson(sidecar));
  }

  static void WriteBytes(const std::string& path, const std::string& bytes) {
    std::FILE* file = OpenFileUtf8(path, "wb");
    ASSERT_NE(file, nullptr);
    std::fwrite(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
  }

  // .npy 머리글 뒤 자료가 원시값 파일에서 영역(x0부터 폭 width)을 자른 것과 같은지 봅니다.
  static std::string ExpectedData(int32_t x0, int32_t width, int32_t height) {
    std::string data;
    for (int32_t f = 0; f < kFrames; ++f) {
      for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = x0; x < x0 + width; ++x) {
          const auto v = static_cast<uint16_t>(f * 1000 + y * 10 + x);
          data.push_back(static_cast<char>(v & 0xFF));
          data.push_back(static_cast<char>(v >> 8));
        }
      }
    }
    return data;
  }

  static std::string DataOf(const std::string& npy) {
    const size_t header = static_cast<uint8_t>(npy[8]) | (static_cast<size_t>(static_cast<uint8_t>(npy[9])) << 8);
    return npy.substr(10 + header);
  }

  std::string root_;
  PreparedShot shot_;
};

// 기본(가운데 세로선)으로 나눠 왼발과 오른발 .npy를 씁니다. 화소 왼쪽이 오른발이면 화소 오른쪽 절반이 thermal_left.npy입니다.
TEST_F(ThermalArrays, WritesFootArraysFromCenterSplit) {
  Prepare("arrays_center", kWidth, kHeight, kFrames);
  std::string error;

  ASSERT_TRUE(WriteThermalArrays(shot_, std::nullopt, &error)) << error;

  const std::string left = ReadBytes(shot_.thermal_left_array);
  const std::string right = ReadBytes(shot_.thermal_right_array);
  ASSERT_GT(left.size(), 10u);
  EXPECT_EQ(left.substr(0, 6), "\x93NUMPY");
  EXPECT_NE(left.find("'shape': (3, 4, 3)"), std::string::npos);
  EXPECT_NE(left.find("'descr': '<u2'"), std::string::npos);
  EXPECT_EQ((left.size() - DataOf(left).size()) % 64, 0u);
  EXPECT_EQ(DataOf(left), ExpectedData(3, 3, kHeight));
  EXPECT_EQ(DataOf(right), ExpectedData(0, 3, kHeight));
  EXPECT_TRUE(std::filesystem::exists(PathFromUtf8(shot_.thermal_raw)));
}

// 영역을 직접 주면 그 영역으로 자르고, 화소 왼쪽 발이 왼발이면 화소 왼쪽 영역이 thermal_left.npy입니다.
TEST_F(ThermalArrays, UsesGivenRegions) {
  Prepare("arrays_given", kWidth, kHeight, kFrames);
  shot_.pixel_left_foot = FootSide::kLeft;
  std::string error;

  ASSERT_TRUE(WriteThermalArrays(shot_, std::make_pair(hwlab::pixel::Rect{0, 0, 2, 4}, hwlab::pixel::Rect{4, 0, 2, 4}),
                                 &error))
      << error;

  EXPECT_NE(ReadBytes(shot_.thermal_left_array).find("'shape': (3, 4, 2)"), std::string::npos);
  EXPECT_EQ(DataOf(ReadBytes(shot_.thermal_left_array)), ExpectedData(0, 2, kHeight));
  EXPECT_EQ(DataOf(ReadBytes(shot_.thermal_right_array)), ExpectedData(4, 2, kHeight));
}

// 발 영역이 세로로 길지 않으면 .npy를 쓰지 않고, 원시값 파일은 남깁니다.
TEST_F(ThermalArrays, RejectsLandscapeRegion) {
  Prepare("arrays_landscape", 8, 3, kFrames);
  std::string error;

  EXPECT_FALSE(WriteThermalArrays(shot_, std::nullopt, &error));

  EXPECT_NE(error.find("세로"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(PathFromUtf8(shot_.thermal_left_array)));
  EXPECT_FALSE(std::filesystem::exists(PathFromUtf8(shot_.thermal_right_array)));
  EXPECT_TRUE(std::filesystem::exists(PathFromUtf8(shot_.thermal_raw)));
}

// 원시값 장 수가 사이드카와 다르면 이번에 쓴 .npy를 지우고, 원시값 파일은 남깁니다.
TEST_F(ThermalArrays, RemovesArraysWhenFrameCountDiffers) {
  Prepare("arrays_mismatch", kWidth, kHeight, kFrames + 2);
  std::string error;

  EXPECT_FALSE(WriteThermalArrays(shot_, std::nullopt, &error));

  EXPECT_NE(error.find("장 수"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(PathFromUtf8(shot_.thermal_left_array)));
  EXPECT_FALSE(std::filesystem::exists(PathFromUtf8(shot_.thermal_right_array)));
  EXPECT_EQ(std::filesystem::file_size(PathFromUtf8(shot_.thermal_raw)),
            static_cast<uintmax_t>(kWidth * kHeight * 2 * kFrames));
}

// 한글 폴더의 촬영에서 좌우 영상 사이드카와 .npy를 읽어 분석 전 확인을 통과하고, 영상이 빠지면 그 역할을 알립니다.
TEST_F(ThermalArrays, ReadsAnalysisFilesForCheck) {
  Prepare("arrays_check", kWidth, kHeight, kFrames);
  std::string error;
  ASSERT_TRUE(WriteThermalArrays(shot_, std::nullopt, &error)) << error;
  for (const auto& [video, sidecar_path, foot] :
       {std::tuple{shot_.left_video, shot_.left_sidecar, FootSide::kLeft},
        std::tuple{shot_.right_video, shot_.right_sidecar, FootSide::kRight}}) {
    WriteBytes(video, "video");
    hwlab::record::Sidecar sidecar;
    sidecar.format = {hwlab::PixelFormat::kUyvy, 1920, 1080, 30};
    sidecar.foot = foot;
    sidecar.crop = {foot == FootSide::kLeft ? 960 : 480, 0, 480, 1080};
    WriteBytes(sidecar_path, hwlab::record::BuildSidecarJson(sidecar));
  }

  EXPECT_TRUE(CheckAnalysisInputs(ReadAnalysisFiles(shot_), kMinThermalFramesInRgbWindow).empty());

  std::filesystem::remove(PathFromUtf8(shot_.right_video));
  const auto findings = CheckAnalysisInputs(ReadAnalysisFiles(shot_), kMinThermalFramesInRgbWindow);
  ASSERT_EQ(findings.size(), 1u);
  EXPECT_EQ(findings[0].problem, InputProblem::kMissingFile);
  EXPECT_EQ(findings[0].role, FileRole::kRightVideo);
}

// 폴더를 만들기 전에 구한 배치는 PrepareShot이 만든 배치와 같고, 폴더를 만들지 않습니다.
TEST(ShotLimits, PlannedLayoutMatchesPreparedShot) {
  const std::string root = FreshRoot("limits_plan");
  std::string error;

  const auto planned = PlanShotLayout(MakeShot(), VideoCodec::kFfv1, SavedVideos::kFeet, kRgbAndThermal);
  ASSERT_TRUE(planned);
  EXPECT_FALSE(std::filesystem::exists(PathFromUtf8(root)));
  const auto prepared =
      PrepareShot(root, MakeShot(), VideoCodec::kFfv1, SavedVideos::kFeet, kRgbAndThermal, 99, &error);
  ASSERT_TRUE(prepared) << error;
  EXPECT_EQ(planned->folder, prepared->layout.folder);
  ASSERT_EQ(planned->files.size(), prepared->layout.files.size());
  for (size_t i = 0; i < planned->files.size(); ++i) EXPECT_EQ(planned->files[i].name, prepared->layout.files[i].name);
}

// 아직 없는 저장 위치는 있는 상위 폴더의 디스크 여유를 읽습니다.
TEST(ShotLimits, FreeBytesOfMissingRootUsesParent) {
  const std::string root = FreshRoot("limits_free");
  const auto free_bytes = FreeBytesAt(root + "/없는/폴더");

  ASSERT_TRUE(free_bytes);
  EXPECT_GT(*free_bytes, 0);
}

// 경로가 상한을 넘거나 디스크 여유가 모자라면 이유를 돌려주고, 상한과 여유를 모르면 검사하지 않습니다.
TEST(ShotLimits, ChecksPathLengthAndFreeSpace) {
  const ShotLayout layout = MakeLayout(MakeShot());
  const std::string root = "C:/data";
  const int32_t longest = hwlab::record::LongestShotPathLength(root, layout, layout.stem);

  EXPECT_EQ(CheckShotLimits(root, layout, 1000, std::nullopt, longest), "");
  EXPECT_NE(CheckShotLimits(root, layout, 1000, std::nullopt, longest - 1).find("저장 경로가 너무 김"), std::string::npos);
  EXPECT_EQ(CheckShotLimits(root, layout, 1000, 1000 + kFreeSpaceReserveBytes, std::nullopt), "");
  const std::string short_space = CheckShotLimits(root, layout, 1000, 999 + kFreeSpaceReserveBytes, std::nullopt);
  EXPECT_NE(short_space.find("디스크 여유 부족"), std::string::npos) << short_space;
}

// 10 GB 미만이면 낮음 문장을, 필요한 양 미만이면 부족 문장을 만들고, 낮음은 촬영을 막지 않습니다.
TEST(ShotLimits, DescribesLowAndShortFreeSpace) {
  const FreeSpaceNotice enough = DescribeFreeSpace(10'000'000'000, 1'000'000'000);
  EXPECT_EQ(enough.verdict, hwlab::record::FreeSpace::kEnough);
  EXPECT_EQ(enough.text, "");

  const FreeSpaceNotice low = DescribeFreeSpace(9'200'000'000, 1'000'000'000);
  EXPECT_EQ(low.verdict, hwlab::record::FreeSpace::kLow);
  EXPECT_EQ(low.text,
            "저장 공간이 9.2 GB 남았습니다. 10 GB 미만이면 촬영 중 공간이 부족할 수 있습니다. 다른 위치로 옮기거나 공간을 비워 주세요.");

  const FreeSpaceNotice scarce = DescribeFreeSpace(1'500'000'000, 1'000'000'000);
  EXPECT_EQ(scarce.verdict, hwlab::record::FreeSpace::kShort);
  EXPECT_NE(scarce.text.find("남은 1.5 GB, 필요 2 GB"), std::string::npos) << scarce.text;

  const ShotLayout layout = MakeLayout(MakeShot());
  EXPECT_EQ(CheckShotLimits("C:/data", layout, 1000, 9'000'000'000, std::nullopt), "");
}

// 저장 실패 문장은 남은 공간이 예비량 미만이면 공간 부족을 원인으로 적고, 저장된 폴더를 알립니다.
TEST(ShotLimits, DescribesSaveFailureWithCause) {
  EXPECT_EQ(DescribeSaveFailure(120'000'000, "D:/촬영/폴더", "x"),
            "저장 실패: 저장 공간이 부족해 이 촬영을 끝까지 저장하지 못했습니다(남은 120 MB). 저장된 부분은 D:/촬영/폴더에 그대로 있습니다.");

  const std::string other = DescribeSaveFailure(5'000'000'000, "D:/촬영/폴더", "영상 파일을 끝까지 쓰지 못함: a.mkv");
  EXPECT_EQ(other.find("저장 공간이 부족"), std::string::npos) << other;
  EXPECT_NE(other.find("영상 파일을 끝까지 쓰지 못함: a.mkv"), std::string::npos) << other;
  EXPECT_NE(DescribeSaveFailure(std::nullopt, "D:/f", "").find("저장 실패"), std::string::npos);
}

}  // namespace
