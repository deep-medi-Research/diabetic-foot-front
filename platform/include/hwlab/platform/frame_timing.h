#ifndef HWLAB_PLATFORM_FRAME_TIMING_H_
#define HWLAB_PLATFORM_FRAME_TIMING_H_

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "hwlab/platform/perfusion_summary.h"
#include "hwlab/timestamp.h"

namespace hwlab::platform {

/**
 * 관류 분석 프로그램의 분석 구간 하나입니다.
 *
 * 관류 분석 프로그램은 프레임을 정확히 1/fps 간격으로 보고 계산하므로 구간 시각은 "프레임 번호 / fps"입니다. 녹화 중 fps가
 * 떨어지면 이 시각과 실제 시각이 어긋나므로, 구간을 프레임 번호로 바꿔 그 안의 실제 프레임 간격을 봅니다.
 */
struct AnalysisWindow {
  std::string kind;   // "pi"(10초 관류 구간, 요약 n_windows), "cuff_level"(커프 단계 구간, cuff_level.window_centres_s)
  double centre_s = 0;  // 구간 가운데(프레임 번호 / fps)
  double length_s = 0;  // 구간 길이
};

/** 분석 구간 하나의 프레임 간격 요약입니다. */
struct FrameTimingWindow {
  AnalysisWindow window;
  int64_t first_frame = 0;     // 구간의 첫 프레임 번호(파일 안 순서, 0부터)
  int64_t frames = 0;          // 구간 안 프레임 수
  int64_t slow_intervals = 0;  // 느린 간격 수(주기의 1.2배 초과 1.5배 이하, timing::CheckFrameRate)
  int64_t missing_frames = 0;  // 카메라 단 누락 장 수(주기의 1.5배 초과, timing::FindFrameGaps)
  double min_fps = 0;          // 구간 안 1초 창의 가장 낮은 fps. 프레임이 둘 미만이면 0
  double elapsed_s = 0;        // 구간 첫 프레임부터 마지막 프레임까지 실제 시각(공통 시각). 고른 30fps면 length_s와 거의 같음
};

/**
 * 요약에서 분석 구간을 꺼냅니다.
 *
 * @param summary ReadPerfusionSummary의 결과
 * @return 관류 구간(n_windows개, window_s 길이, window_stride_s 간격, 첫 구간은 0초부터)과 커프 단계 구간(window_centres_s,
 *         10초). 값이 없는 쪽은 넣지 않습니다
 */
std::vector<AnalysisWindow> AnalysisWindowsOf(const PerfusionSummary& summary);

/**
 * 분석 구간마다 그 안의 프레임 간격을 요약합니다.
 *
 * @param times 파일에 쓴 순서의 프레임 공통 시각(사이드카 per_frame.common_time_us)
 * @param fps 관류 분석 프로그램이 쓴 fps(요약 fps). 구간을 프레임 번호로 바꾸고 주기(1/fps)를 정합니다
 * @param windows AnalysisWindowsOf의 결과
 * @return windows와 같은 순서. fps가 0 이하이면 빈 목록
 */
std::vector<FrameTimingWindow> SummarizeFrameWindows(std::span<const Timestamp> times, double fps,
                                                     const std::vector<AnalysisWindow>& windows);

/**
 * 프레임별 간격 CSV를 만듭니다. 열은 frame, analysis_s, elapsed_s, common_time_us, interval_ms, kind입니다.
 *
 * analysis_s는 관류 분석 프로그램이 보는 시각(frame / fps), elapsed_s는 첫 프레임부터의 실제 시각, interval_ms는 앞 프레임과의
 * 실제 간격(첫 프레임은 비움), kind는 느린 간격이면 slow, 누락이면 gap, 아니면 비웁니다.
 */
std::string BuildFrameTimingCsv(std::span<const Timestamp> times, double fps);

/**
 * 분석 구간별 CSV를 만듭니다. 열은 kind, centre_s, start_s, end_s, first_frame, frames, elapsed_s, slow_intervals,
 * missing_frames, min_fps입니다.
 */
std::string BuildFrameWindowsCsv(const std::vector<FrameTimingWindow>& windows);

/** 프레임별 간격 CSV의 이름입니다(`<name>_frame_timing.csv`). 분석결과 폴더로 복사합니다(IsCollectedResultFile). */
std::string FrameTimingCsvPath(const std::string& output_dir, const std::string& name);
/** 분석 구간별 CSV의 이름입니다(`<name>_frame_windows.csv`). 분석결과 폴더로 복사합니다(IsCollectedResultFile). */
std::string FrameWindowsCsvPath(const std::string& output_dir, const std::string& name);

/**
 * 촬영 폴더의 왼쪽 RGB 사이드카와 분석 요약으로 두 CSV를 분석 결과 폴더에 씁니다.
 *
 * @param shot_dir 촬영 폴더(UTF-8). `<name>_shot.json`이 있어야 합니다
 * @param output_dir 분석 결과 폴더(UTF-8). 요약 `<name>_summary.json`이 있어야 합니다
 * @param name 촬영 이름(분석 요청의 name)
 * @param error 실패하면 이유를 씁니다. nullptr이면 쓰지 않습니다
 * @return 두 파일을 모두 썼으면 true
 * @note 좌우 RGB는 같은 카메라의 같은 프레임을 잘라 쓰므로 프레임 시각이 같아 왼쪽만 씁니다.
 */
bool WriteFrameTiming(const std::string& shot_dir, const std::string& output_dir, const std::string& name,
                      std::string* error = nullptr);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_FRAME_TIMING_H_
