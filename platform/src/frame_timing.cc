#include "hwlab/platform/frame_timing.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

#include "hwlab/platform/perfusion_analysis.h"
#include "hwlab/platform/shot_folder.h"
#include "hwlab/platform/shot_review.h"
#include "hwlab/timing/frame_gap.h"

namespace hwlab::platform {

namespace {

// 커프 단계 모델의 구간 길이입니다(관류 분석 프로그램 cuff_level, 10초 구간 5초 간격).
constexpr double kCuffWindowS = 10.0;

std::optional<std::string> ReadUtf8File(const std::string& path) {
  std::FILE* f = OpenFileUtf8(path, "rb");
  if (f == nullptr) return std::nullopt;
  std::string text;
  char buffer[65536];
  size_t n = 0;
  while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) text.append(buffer, n);
  std::fclose(f);
  return text;
}

bool WriteUtf8File(const std::string& path, const std::string& text) {
  std::FILE* f = OpenFileUtf8(path, "wb");
  if (f == nullptr) return false;
  bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
  ok = std::fclose(f) == 0 && ok;
  return ok;
}

std::string Fixed(double value, int digits) {
  char text[64];
  std::snprintf(text, sizeof(text), "%.*f", digits, value);
  return text;
}

int64_t PeriodUs(double fps) { return static_cast<int64_t>(std::llround(1e6 / fps)); }

std::string JoinPath(const std::string& dir, const std::string& name) {
  if (dir.empty() || dir.back() == '/' || dir.back() == '\\') return dir + name;
  return dir + "/" + name;
}

}  // namespace

std::vector<AnalysisWindow> AnalysisWindowsOf(const PerfusionSummary& summary) {
  std::vector<AnalysisWindow> out;
  if (summary.window_count > 0 && summary.window_us > 0) {
    const double length = static_cast<double>(summary.window_us) / 1e6;
    const double stride = static_cast<double>(summary.window_stride_us) / 1e6;
    for (int32_t k = 0; k < summary.window_count; ++k) out.push_back({"pi", k * stride + length / 2, length});
  }
  if (summary.cuff_level) {
    for (const double centre : summary.cuff_level->window_centres_s) {
      if (std::isfinite(centre)) out.push_back({"cuff_level", centre, kCuffWindowS});
    }
  }
  return out;
}

std::vector<FrameTimingWindow> SummarizeFrameWindows(std::span<const Timestamp> times, double fps,
                                                     const std::vector<AnalysisWindow>& windows) {
  std::vector<FrameTimingWindow> out;
  if (!(fps > 0)) return out;
  const int64_t period = PeriodUs(fps);
  const auto count = static_cast<int64_t>(times.size());
  for (const AnalysisWindow& w : windows) {
    FrameTimingWindow r;
    r.window = w;
    const int64_t begin = std::clamp<int64_t>(std::llround((w.centre_s - w.length_s / 2) * fps), 0, count);
    const int64_t end = std::clamp<int64_t>(std::llround((w.centre_s + w.length_s / 2) * fps), begin, count);
    r.first_frame = begin;
    r.frames = end - begin;
    const auto part = times.subspan(static_cast<size_t>(begin), static_cast<size_t>(end - begin));
    const timing::FrameRateCheck check = timing::CheckFrameRate(part, period);
    r.slow_intervals = check.slow_intervals;
    r.min_fps = check.min_fps;
    r.missing_frames = timing::CountMissingFrames(timing::FindFrameGaps(part, period));
    if (part.size() >= 2) r.elapsed_s = static_cast<double>(part.back().us - part.front().us) / 1e6;
    out.push_back(r);
  }
  return out;
}

std::string BuildFrameTimingCsv(std::span<const Timestamp> times, double fps) {
  std::string out = "frame,analysis_s,elapsed_s,common_time_us,interval_ms,kind\n";
  if (times.empty() || !(fps > 0)) return out;
  const int64_t period = PeriodUs(fps);
  for (size_t i = 0; i < times.size(); ++i) {
    out += std::to_string(i) + "," + Fixed(static_cast<double>(i) / fps, 4) + "," +
           Fixed(static_cast<double>(times[i].us - times.front().us) / 1e6, 6) + "," + std::to_string(times[i].us) + ",";
    if (i > 0) {
      const int64_t interval = times[i].us - times[i - 1].us;
      out += Fixed(static_cast<double>(interval) / 1000.0, 3) + ",";
      // timing::CheckFrameRate, timing::FindFrameGaps와 같은 기준입니다.
      if (interval * 2 > period * 3) {
        out += "gap";
      } else if (interval * 5 > period * 6) {
        out += "slow";
      }
    } else {
      out += ",";
    }
    out += "\n";
  }
  return out;
}

std::string BuildFrameWindowsCsv(const std::vector<FrameTimingWindow>& windows) {
  std::string out = "kind,centre_s,start_s,end_s,first_frame,frames,elapsed_s,slow_intervals,missing_frames,min_fps\n";
  for (const FrameTimingWindow& r : windows) {
    out += r.window.kind + "," + Fixed(r.window.centre_s, 3) + "," + Fixed(r.window.centre_s - r.window.length_s / 2, 3) +
           "," + Fixed(r.window.centre_s + r.window.length_s / 2, 3) + "," + std::to_string(r.first_frame) + "," +
           std::to_string(r.frames) + "," + Fixed(r.elapsed_s, 3) + "," + std::to_string(r.slow_intervals) + "," +
           std::to_string(r.missing_frames) + "," + Fixed(r.min_fps, 1) + "\n";
  }
  return out;
}

std::string FrameTimingCsvPath(const std::string& output_dir, const std::string& name) {
  return JoinPath(output_dir, name + "_frame_timing.csv");
}

std::string FrameWindowsCsvPath(const std::string& output_dir, const std::string& name) {
  return JoinPath(output_dir, name + "_frame_windows.csv");
}

bool WriteFrameTiming(const std::string& shot_dir, const std::string& output_dir, const std::string& name,
                      std::string* error) {
  const auto fail = [&](const std::string& why) {
    if (error != nullptr) *error = why;
    return false;
  };
  const auto manifest_text = ReadUtf8File(JoinPath(shot_dir, name + "_shot.json"));
  if (!manifest_text) return fail("매니페스트를 열지 못했습니다: " + JoinPath(shot_dir, name + "_shot.json"));
  std::string why;
  const auto manifest = ReadShotManifest(*manifest_text, &why);
  if (!manifest || manifest->left_sidecar.empty()) return fail("매니페스트에 왼쪽 사이드카가 없습니다 " + why);
  const auto sidecar_text = ReadUtf8File(JoinPath(shot_dir, manifest->left_sidecar));
  if (!sidecar_text) return fail("사이드카를 열지 못했습니다: " + manifest->left_sidecar);
  const auto frames = ReadSidecarFrames(*sidecar_text, &why);
  if (!frames) return fail("사이드카를 읽지 못했습니다: " + why);
  const auto summary_text = ReadUtf8File(PerfusionSummaryPath(output_dir, name));
  if (!summary_text) return fail("요약을 열지 못했습니다: " + PerfusionSummaryPath(output_dir, name));
  const auto summary = ReadPerfusionSummary(*summary_text, &why);
  if (!summary) return fail("요약을 읽지 못했습니다: " + why);
  const double fps = std::isfinite(summary->fps) && summary->fps > 0 ? summary->fps : 30.0;
  // 관류 분석 프로그램은 앞에서부터 n_frames장만 씁니다. 그 뒤 장은 분석과 관계없어 넣지 않습니다.
  std::span<const Timestamp> times(frames->common_time);
  if (summary->frames > 0 && static_cast<size_t>(summary->frames) < times.size()) {
    times = times.first(static_cast<size_t>(summary->frames));
  }
  const auto windows = SummarizeFrameWindows(times, fps, AnalysisWindowsOf(*summary));
  if (!WriteUtf8File(FrameTimingCsvPath(output_dir, name), BuildFrameTimingCsv(times, fps))) {
    return fail("파일을 쓰지 못했습니다: " + FrameTimingCsvPath(output_dir, name));
  }
  if (!WriteUtf8File(FrameWindowsCsvPath(output_dir, name), BuildFrameWindowsCsv(windows))) {
    return fail("파일을 쓰지 못했습니다: " + FrameWindowsCsvPath(output_dir, name));
  }
  return true;
}

}  // namespace hwlab::platform
