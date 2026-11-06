#include "hwlab/record/shot_limits.h"

#include <algorithm>

namespace hwlab::record {

namespace {

// 관류 분석 결과는 촬영 폴더 안 이 폴더의 판 폴더(perfusion/<판>/)에 분석이름_*로 쓰고, results 폴더로 모을 때는 이름 앞부분
// 뒤에 _<판>을 붙입니다. 결과 파일 중 가장 긴 뒷부분은 보고서 그림의 23자입니다(_report_propagation.png).
constexpr char kAnalysisFolder[] = "perfusion";
constexpr char kLongestVariantId[] = "v11";
constexpr char kLongestResultSuffix[] = "_report_propagation.png";

int64_t FrameBytes(VideoSize size) {
  if (size.width <= 0 || size.height <= 0) return 0;
  return static_cast<int64_t>(size.width) * size.height * 2;
}

// 녹화 시간(RGB 장 수 / fps) 동안 기록할 열화상 장 수의 상한입니다. 첫 장을 위해 1을 더합니다.
int64_t ThermalFrames(const ShotSizePlan& plan) {
  if (plan.rgb_frames <= 0 || plan.rgb_frame_rate <= 0) return 0;
  const int64_t duration_us = plan.rgb_frames * 1'000'000 / plan.rgb_frame_rate;
  if (plan.thermal_record_period_us > 0) return duration_us / plan.thermal_record_period_us + 1;
  if (plan.thermal_max_rate_hz <= 0) return 0;
  return duration_us * plan.thermal_max_rate_hz / 1'000'000 + 1;
}

// UTF-8 바이트를 UTF-16 코드 단위로 셉니다. 4바이트 문자(U+10000 이상)는 대리 쌍이라 2입니다.
int32_t Utf16Length(const std::string& utf8) {
  int32_t units = 0;
  for (const char c : utf8) {
    const auto byte = static_cast<unsigned char>(c);
    if ((byte & 0xC0) == 0x80) continue;
    units += byte >= 0xF0 ? 2 : 1;
  }
  return units;
}

bool EndsWithSeparator(const std::string& path) {
  return !path.empty() && (path.back() == '/' || path.back() == '\\');
}

}  // namespace

int64_t EstimateShotBytes(const ShotSizePlan& plan) {
  int64_t bytes = 0;
  const int64_t rgb_frames = std::max<int64_t>(plan.rgb_frames, 0);
  for (const VideoSize& video : plan.rgb_videos) {
    bytes += rgb_frames * (FrameBytes(video) + kSidecarBytesPerFrame);
  }
  if (plan.thermal) {
    // 원시값, 확인용 AVI, 좌우 .npy가 각각 화소당 2바이트 이하이고 사이드카는 원시값에만 있습니다.
    const int64_t frames = ThermalFrames(plan);
    bytes += frames * (FrameBytes(plan.thermal_size) * 3 + kSidecarBytesPerFrame);
  }
  return bytes;
}

FreeSpaceJudgement JudgeFreeSpace(std::optional<int64_t> free_bytes, int64_t estimated_bytes, int64_t reserve_bytes,
                                  int64_t low_bytes) {
  FreeSpaceJudgement judgement;
  judgement.required_bytes = std::max<int64_t>(estimated_bytes, 0) + std::max<int64_t>(reserve_bytes, 0);
  if (!free_bytes) return judgement;
  if (*free_bytes < judgement.required_bytes) {
    judgement.verdict = FreeSpace::kShort;
  } else {
    judgement.verdict = *free_bytes < low_bytes ? FreeSpace::kLow : FreeSpace::kEnough;
  }
  return judgement;
}

int32_t LongestShotPathLength(const std::string& root, const ShotLayout& layout, const std::string& analysis_name) {
  // 파일 이름 앞까지의 길이입니다(저장 위치, 구분자, 폴더, 구분자).
  const int32_t folder = Utf16Length(root) + (root.empty() || EndsWithSeparator(root) ? 0 : 1) +
                         Utf16Length(layout.folder) + 1;
  int32_t longest = 0;
  for (const ShotFile& file : layout.files) longest = std::max(longest, folder + Utf16Length(file.name));
  if (!analysis_name.empty()) {
    const int32_t result = Utf16Length(kAnalysisFolder) + 1 + Utf16Length(kLongestVariantId) + 1 +
                           Utf16Length(analysis_name) + Utf16Length(kLongestResultSuffix);
    longest = std::max(longest, folder + result);
    // 분석결과 폴더로 모은 결과는 가장 큰 재분석 번호까지 셉니다.
    const int32_t collected = Utf16Length(root) + (root.empty() || EndsWithSeparator(root) ? 0 : 1) +
                              Utf16Length(kCollectedResultFolder) + 1 +
                              Utf16Length(CollectedResultPrefix(layout, kMaxReanalysisNumber)) + 1 +
                              Utf16Length(kLongestVariantId) + Utf16Length(kLongestResultSuffix);
    longest = std::max(longest, collected);
  }
  return longest;
}

}  // namespace hwlab::record
