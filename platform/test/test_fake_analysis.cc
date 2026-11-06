// 분석 API 저장소의 가짜 분석 프로그램(fake/fake_analyzer.py)을 앱과 같은 명령줄로 띄워 Analyzer와 결과 읽기가 모든 결과 파일을
// 읽는지 확인합니다. 가짜 분석 프로그램은 받은 줄을 API의 요청 스키마로 확인하고 어긴 줄에는 종료 코드 4로 끝나므로, 이 시험이
// 통과하면 앱이 보내는 요청 줄이 API를 지킨다는 뜻입니다. 파이썬 3.9 이상이 없으면 시험을 건너뜁니다.
// 구성할 때 -DPython3_EXECUTABLE=<python>으로 지정합니다.
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "hwlab/platform/analyzer.h"
#include "hwlab/platform/npy_archive.h"
#include "hwlab/platform/perfusion_analysis.h"
#include "hwlab/platform/perfusion_detail.h"
#include "hwlab/platform/perfusion_summary.h"
#include "hwlab/platform/template_map.h"
#include "hwlab/record/shot.h"

namespace {

namespace fs = std::filesystem;
using namespace hwlab::platform;
using Clock = std::chrono::steady_clock;

constexpr char kPython[] = HWLAB_FAKE_ANALYSIS_PYTHON;
constexpr char kScript[] = HWLAB_FAKE_ANALYSIS_SCRIPT;
constexpr int kWindows = 11;
constexpr int kFrames = 900;

fs::path FromUtf8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

std::string ToUtf8(const fs::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

std::string ReadFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

fs::path TempDir(const std::string& name) {
  const fs::path dir = fs::temp_directory_path() / "hwlab_test_fake_analysis" / name;
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir;
}

// 분석 요청이 가리키는 영상 파일입니다. 가짜 분석 프로그램은 영상 내용을 읽지 않습니다.
struct Shot {
  fs::path dir;
  fs::path left;
  fs::path right;
};

Shot MakeShot(const std::string& name) {
  Shot shot;
  shot.dir = TempDir(name);
  shot.left = shot.dir / "left.mkv";
  shot.right = shot.dir / "right.mkv";
  std::ofstream(shot.left, std::ios::binary) << "x";
  std::ofstream(shot.right, std::ios::binary) << "x";
  return shot;
}

PerfusionRequest RequestFor(const Shot& shot, const std::string& name) {
  PerfusionRequest request;
  request.recording_dir = ToUtf8(shot.dir);
  request.left_video = ToUtf8(shot.left);
  request.right_video = ToUtf8(shot.right);
  request.left_frames = kFrames;
  request.right_frames = kFrames;
  request.output_dir = PerfusionOutputDir(request.recording_dir, "fake");
  request.name = name;
  return request;
}

// main.cc가 판 하나를 띄우는 명령줄과 같습니다.
ProcessLaunch AppLaunch() {
  ProcessLaunch launch = AnalyzerLaunch("", "", kPython, kScript);
  launch.arguments.insert(launch.arguments.end(), {"--presegment-threads", "2", "--prestream"});
  return AnalyzerVariantLaunch(std::move(launch), "fake");
}

AnalyzerStatus WaitFor(const Analyzer& analyzer, const std::function<bool(const AnalyzerStatus&)>& done,
                       int64_t timeout_ms = 30'000) {
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  AnalyzerStatus status = analyzer.Status();
  while (!done(status)) {
    if (Clock::now() > deadline) {
      ADD_FAILURE() << "시간 안에 기다린 상태가 되지 않았습니다. state=" << static_cast<int>(status.state)
                    << " message=" << status.message << " finished=" << status.finished_count;
      return status;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    status = analyzer.Status();
  }
  return status;
}

AnalyzerOutcome WaitFinished(const Analyzer& analyzer, int64_t count) {
  return WaitFor(analyzer, [&](const AnalyzerStatus& s) { return s.finished_count >= count; }).last_done;
}

struct GifLayout {
  int width = 0;
  int height = 0;
  int frames = 0;
  bool well_formed = false;
};

// 머리, 색표, 확장 블록, 장 블록을 끝 표시(0x3B)까지 따라가며 장 수를 셉니다.
GifLayout ReadGifLayout(const std::string& bytes) {
  GifLayout gif;
  const auto u8 = [&](size_t at) { return at < bytes.size() ? static_cast<uint8_t>(bytes[at]) : uint8_t{0}; };
  const auto u16 = [&](size_t at) { return u8(at) | (u8(at + 1) << 8); };
  if (bytes.compare(0, 6, "GIF89a") != 0 || bytes.size() < 14) return gif;
  gif.width = u16(6);
  gif.height = u16(8);
  size_t at = 13;
  if (u8(10) & 0x80) at += 3u * (1u << ((u8(10) & 7) + 1));
  const auto skip_blocks = [&] {
    while (at < bytes.size() && u8(at) != 0) at += 1 + u8(at);
    ++at;
  };
  while (at < bytes.size()) {
    const uint8_t tag = u8(at++);
    if (tag == 0x3B) {
      gif.well_formed = at == bytes.size();
      return gif;
    }
    if (tag == 0x21) {
      ++at;
      skip_blocks();
    } else if (tag == 0x2C) {
      at += 8;
      const uint8_t flags = u8(at++);
      if (flags & 0x80) at += 3u * (1u << ((flags & 7) + 1));
      ++at;
      skip_blocks();
      ++gif.frames;
    } else {
      return gif;
    }
  }
  return gif;
}

struct PngLayout {
  uint32_t width = 0;
  uint32_t height = 0;
  bool well_formed = false;
};

PngLayout ReadPngLayout(const std::string& bytes) {
  PngLayout png;
  const auto u32 = [&](size_t at) {
    uint32_t value = 0;
    for (size_t i = 0; i < 4 && at + i < bytes.size(); ++i) value = (value << 8) | static_cast<uint8_t>(bytes[at + i]);
    return value;
  };
  if (bytes.compare(0, 8, "\x89PNG\r\n\x1a\n") != 0) return png;
  size_t at = 8;
  bool first = true;
  while (at + 12 <= bytes.size()) {
    const uint32_t length = u32(at);
    const std::string kind = bytes.substr(at + 4, 4);
    if (first) {
      if (kind != "IHDR" || length != 13) return png;
      png.width = u32(at + 8);
      png.height = u32(at + 12);
      first = false;
    }
    at += 12 + static_cast<size_t>(length);
    if (kind == "IEND") {
      png.well_formed = at == bytes.size();
      return png;
    }
  }
  return png;
}

class FakeAnalysis : public ::testing::Test {
 protected:
  void SetUp() override {
    if (std::string(kPython).empty()) {
      GTEST_SKIP() << "파이썬 3.9 이상을 찾지 못해 가짜 분석 프로그램 시험을 건너뜁니다. "
                      "cmake 구성에 -DPython3_EXECUTABLE=<python 실행 파일>을 주면 돕니다";
    }
  }

  std::unique_ptr<Analyzer> StartIdle() {
    std::string error;
    auto analyzer = Analyzer::Start(AppLaunch(), &error);
    EXPECT_NE(analyzer, nullptr) << error;
    if (analyzer) WaitFor(*analyzer, [](const AnalyzerStatus& s) { return s.state == AnalyzerState::kIdle; });
    return analyzer;
  }
};

// 앱은 판 폴더에서 판을 찾고, 준비 줄과 preload에 답한 프로그램을 요청을 받을 수 있는 상태로 둡니다.
TEST_F(FakeAnalysis, IsFoundAsOneVariantAndGetsReady) {
  const std::string script_dir = ToUtf8(FromUtf8(kScript).parent_path());
  std::string error;
  const auto variants = FindAnalyzerVariants(script_dir, &error);
  ASSERT_EQ(variants.size(), 1u) << error;
  EXPECT_EQ(variants[0].id, "fake");
  EXPECT_FALSE(variants[0].label.empty());
  EXPECT_FALSE(variants[0].summary.empty());
  EXPECT_EQ(variants[0].upstream_commit, "fake");
  EXPECT_EQ(AnalyzerFilesProblem(AppLaunch()), std::nullopt);

  auto analyzer = StartIdle();
  ASSERT_NE(analyzer, nullptr);
  const AnalyzerStatus status = analyzer->Status();
  EXPECT_EQ(status.state, AnalyzerState::kIdle);
  EXPECT_EQ(status.api_version, hwlab::platform::AnalysisApiVersion());
  EXPECT_EQ(status.worker_version, "fake-1");
  EXPECT_EQ(status.upstream_commit, "fake");
}

// 분석 한 건이 끝나면 앱이 읽는 결과 일곱 개를 모두 읽을 수 있고, 시간 창 수가 요약, npz, GIF에서 같습니다.
TEST_F(FakeAnalysis, WritesResultsTheAppCanRead) {
  const Shot shot = MakeShot("results");
  auto analyzer = StartIdle();
  ASSERT_NE(analyzer, nullptr);
  const PerfusionRequest request = RequestFor(shot, "shot_a");
  ASSERT_TRUE(analyzer->Submit(request));
  const AnalyzerOutcome done = WaitFinished(*analyzer, 1);
  ASSERT_TRUE(done.ok) << done.error;
  EXPECT_EQ(done.summary_path, request.output_dir + "/shot_a_summary.json");
  WaitFor(*analyzer, [](const AnalyzerStatus& s) { return s.figures_count >= 1; });

  std::string error;
  const auto summary = ReadPerfusionSummary(ReadFile(FromUtf8(done.summary_path)), &error);
  ASSERT_TRUE(summary) << error;
  EXPECT_NEAR(summary->pi_left, 0.12, 1e-6);
  EXPECT_NEAR(summary->pi_right, 0.15, 1e-6);
  EXPECT_NEAR(summary->pi_ratio, 0.8, 1e-6);
  EXPECT_EQ(summary->lower_foot, hwlab::record::FootSide::kLeft);
  EXPECT_EQ(summary->window_count, kWindows);
  EXPECT_EQ(summary->window_us, 10'000'000);
  EXPECT_EQ(summary->window_stride_us, 2'000'000);
  EXPECT_EQ(summary->fps, 30.0);
  EXPECT_EQ(summary->frames, kFrames);
  EXPECT_EQ(summary->variant, "fake");
  EXPECT_EQ(summary->upstream_commit, "fake");
  EXPECT_EQ(summary->api_version, hwlab::platform::AnalysisApiVersion());
  EXPECT_EQ(summary->worker_version, "fake-1");
  EXPECT_FALSE(std::isnan(summary->left.pi_toes));
  EXPECT_FALSE(std::isnan(summary->right.pi_heel));
  EXPECT_FALSE(std::isnan(summary->left.detection_score));
  EXPECT_GT(summary->cells_used, 0.0);
  EXPECT_GT(summary->template_corr, 0.5);
  ASSERT_TRUE(summary->cuff_level);
  EXPECT_TRUE(summary->cuff_level->applicable);
  EXPECT_TRUE(summary->cuff_level->states);
  EXPECT_EQ(summary->cuff_level->level_code, "0");
  ASSERT_TRUE(summary->quality);
  EXPECT_EQ(summary->quality->status, "check");
  ASSERT_EQ(summary->quality->items.size(), 3u);
  EXPECT_EQ(summary->quality->items[2].severity, "warn");
  EXPECT_FALSE(std::isnan(summary->quality->thermal_contrast_left));

  const auto detail = ReadPerfusionDetail(PerfusionDetailPath(request.output_dir, "shot_a"), &error);
  ASSERT_TRUE(detail) << error;
  EXPECT_EQ(detail->window_centers_s, (std::vector<double>{5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25}));
  EXPECT_EQ(detail->template_left.size(), static_cast<size_t>(kFrames));
  EXPECT_EQ(detail->template_right.size(), static_cast<size_t>(kFrames));
  EXPECT_EQ(detail->fps, 30.0);
  EXPECT_EQ(detail->template_corr.size(), static_cast<size_t>(kWindows));
  EXPECT_EQ(detail->light_ref_count, std::vector<int32_t>(kWindows, 6));
  for (const auto* series : {&detail->left_series, &detail->right_series}) {
    for (const char* key : {"pi", "pi_toes", "pi_fore", "pi_mid", "pi_heel"}) {
      ASSERT_EQ(series->count(key), 1u) << key;
      EXPECT_EQ(series->at(key).size(), static_cast<size_t>(kWindows)) << key;
    }
  }
  ASSERT_TRUE(detail->HasTemplateMap());
  EXPECT_EQ(detail->map_rows, 40);
  EXPECT_EQ(detail->map_cols, 15);
  EXPECT_EQ(detail->maps.size(), static_cast<size_t>(2 * kWindows * 40 * 15));
  EXPECT_EQ(detail->outline_u.size(), 41u);
  const auto top = TemplateMapTop(*detail);
  ASSERT_TRUE(top);
  EXPECT_GT(*top, 0.0);
  for (int window = 0; window < kWindows; ++window) {
    const auto image = DrawTemplateMap(*detail, window, 120, *top);
    ASSERT_TRUE(image) << window;
    EXPECT_GT(image->width, 0);
    EXPECT_EQ(image->height, 120);
  }

  const auto perfusion_file = ReadNpz(PerfusionDetailPath(request.output_dir, "shot_a"), &error);
  ASSERT_TRUE(perfusion_file) << error;
  EXPECT_EQ(perfusion_file->skipped.count("sides"), 1u);
  EXPECT_EQ(perfusion_file->skipped.count("cuff_side"), 1u);

  const auto waveforms = ReadNpz(request.output_dir + "/shot_a_waveforms.npz", &error);
  ASSERT_TRUE(waveforms) << error;
  for (const char* key : {"ac_left", "ac_right", "pulse_left", "pulse_right"}) {
    ASSERT_EQ(waveforms->arrays.count(key), 1u) << key;
    EXPECT_EQ(waveforms->arrays.at(key).values.size(), static_cast<size_t>(kFrames)) << key;
  }
  for (const char* key : {"fps", "pi_left", "pi_right"}) {
    ASSERT_EQ(waveforms->arrays.count(key), 1u) << key;
    EXPECT_TRUE(waveforms->arrays.at(key).shape.empty()) << key;
  }

  const GifLayout gif = ReadGifLayout(ReadFile(FromUtf8(PerfusionGifPath(request.output_dir, "shot_a"))));
  EXPECT_TRUE(gif.well_formed);
  EXPECT_GT(gif.width, 0);
  EXPECT_GT(gif.height, 0);
  EXPECT_EQ(gif.frames, summary->window_count);
  EXPECT_EQ(gif.frames, static_cast<int>(detail->window_centers_s.size()));

  for (const char* suffix : {"segmentation", "template", "pi_map"}) {
    const PngLayout png = ReadPngLayout(ReadFile(FromUtf8(request.output_dir + "/shot_a_" + suffix + ".png")));
    EXPECT_TRUE(png.well_formed) << suffix;
    EXPECT_GT(png.width, 0u) << suffix;
    EXPECT_GT(png.height, 0u) << suffix;
  }

  // 앱이 저장 위치로 복사하는 파일은 요약, GIF, PNG 세 장이고 npz는 복사하지 않습니다.
  hwlab::record::ShotLayout layout;
  layout.folder = "fake_19551103/0/20261105_100000";
  const auto collected = CollectPerfusionResults(request.output_dir, "shot_a", ToUtf8(shot.dir / "root"), layout, "fake", &error);
  ASSERT_TRUE(collected) << error;
  EXPECT_EQ(collected->files.size(), 5u);
  EXPECT_EQ(collected->copied, 5);
}

// 녹화 중 미리 분할과 미리 읽기를 받은 영상의 요약에는 쓰지 못한 이유가 없습니다. 미리 읽기는 녹화 한 번에 한 번만 쓰므로
// 같은 영상의 두 번째 요청에는 not_started가 남습니다.
TEST_F(FakeAnalysis, RecordsWhetherPresegmentWasUsed) {
  const Shot shot = MakeShot("presegment");
  auto analyzer = StartIdle();
  ASSERT_NE(analyzer, nullptr);
  std::string error;
  ASSERT_TRUE(analyzer->Presegment(ToUtf8(shot.left), ToUtf8(shot.right), &error)) << error;
  WaitFor(*analyzer, [](const AnalyzerStatus& s) { return s.presegmented >= 2; });
  ASSERT_TRUE(analyzer->Submit(RequestFor(shot, "first")));
  ASSERT_TRUE(analyzer->Submit(RequestFor(shot, "again")));
  ASSERT_TRUE(WaitFinished(*analyzer, 2).ok);
  const fs::path out = FromUtf8(PerfusionOutputDir(ToUtf8(shot.dir), "fake"));
  const auto first = ReadPerfusionSummary(ReadFile(out / "first_summary.json"));
  const auto again = ReadPerfusionSummary(ReadFile(out / "again_summary.json"));
  ASSERT_TRUE(first && again);
  EXPECT_EQ(first->presegment_skipped, "");
  EXPECT_EQ(first->prestream_skipped, "");
  EXPECT_EQ(again->presegment_skipped, "");
  EXPECT_EQ(again->prestream_skipped, "not_started");
}

// 요청 키에 따라 GIF와 커프 단계를 빼고, defer_figures가 false이면 done 전에 그림을 다 쓰고 figures 줄을 찍지 않습니다.
TEST_F(FakeAnalysis, HonorsRequestFlags) {
  const Shot shot = MakeShot("flags");
  auto analyzer = StartIdle();
  ASSERT_NE(analyzer, nullptr);
  PerfusionRequest request = RequestFor(shot, "plain");
  request.gif = false;
  request.classify = false;
  request.defer_figures = false;
  ASSERT_TRUE(analyzer->Submit(request));
  ASSERT_TRUE(WaitFinished(*analyzer, 1).ok);
  const fs::path out = FromUtf8(request.output_dir);
  EXPECT_TRUE(fs::exists(out / "plain_pi_map.png"));
  EXPECT_TRUE(fs::exists(out / "plain_perfusion.npz"));
  EXPECT_FALSE(fs::exists(out / "plain_perfusion.gif"));
  const auto summary = ReadPerfusionSummary(ReadFile(out / "plain_summary.json"));
  ASSERT_TRUE(summary);
  EXPECT_FALSE(summary->cuff_level);
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  EXPECT_EQ(analyzer->Status().figures_count, 0);
}

// 촬영 폴더 이름이 fail, nosummary이거나 입력이 틀리면 앱이 실패로 보이는 문장과 함께 끝납니다. 비정상 종료 뒤에는 다시 띄웁니다.
TEST_F(FakeAnalysis, ReportsFailures) {
  const Shot shot = MakeShot("failures");
  auto analyzer = StartIdle();
  ASSERT_NE(analyzer, nullptr);

  ASSERT_TRUE(analyzer->Submit(RequestFor(shot, "fail")));
  AnalyzerOutcome done = WaitFinished(*analyzer, 1);
  EXPECT_FALSE(done.ok);
  EXPECT_NE(done.error.find("left.avi 가 없습니다"), std::string::npos) << done.error;
  EXPECT_NE(done.error.find('\n'), std::string::npos) << done.error;

  ASSERT_TRUE(analyzer->Submit(RequestFor(shot, "nosummary")));
  done = WaitFinished(*analyzer, 2);
  EXPECT_FALSE(done.ok);
  EXPECT_NE(done.error.find("요약 파일이 없습니다"), std::string::npos) << done.error;

  PerfusionRequest missing = RequestFor(shot, "missing");
  missing.left_video = ToUtf8(shot.dir / "none.mkv");
  ASSERT_TRUE(analyzer->Submit(missing));
  done = WaitFinished(*analyzer, 3);
  EXPECT_FALSE(done.ok);
  EXPECT_NE(done.error.find("left 영상이 없습니다"), std::string::npos) << done.error;

  PerfusionRequest pad = RequestFor(shot, "pad");
  pad.pad_classifier = true;
  ASSERT_TRUE(analyzer->Submit(pad));
  done = WaitFinished(*analyzer, 4);
  EXPECT_FALSE(done.ok);
  EXPECT_NE(done.error.find("이진 PAD 판정 모델이 없습니다"), std::string::npos) << done.error;

  ASSERT_TRUE(analyzer->Submit(RequestFor(shot, "crash")));
  done = WaitFinished(*analyzer, 5);
  EXPECT_FALSE(done.ok);
  EXPECT_NE(done.error.find("종료 코드 3"), std::string::npos) << done.error;
  ASSERT_TRUE(analyzer->Submit(RequestFor(shot, "after_crash")));
  EXPECT_TRUE(WaitFinished(*analyzer, 6).ok);
}

}  // namespace
