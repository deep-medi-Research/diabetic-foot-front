#ifndef HWLAB_RECORD_ANALYSIS_INPUTS_H_
#define HWLAB_RECORD_ANALYSIS_INPUTS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "hwlab/pixel/rect.h"
#include "hwlab/record/shot.h"
#include "hwlab/record/sidecar.h"
#include "hwlab/stream.h"

namespace hwlab::record {

/** 관류 분석이 요구하는 RGB 녹화 구간 안 열화상 장 수의 최솟값입니다(30초에 225장). */
inline constexpr int64_t kMinThermalFramesInRgbWindow = 225;

/** 분석 전 확인과 열화상 변환에 쓰는 사이드카 값입니다. BuildSidecarJson이 쓴 JSON에서 읽습니다. */
struct SidecarSummary {
  StreamKind stream = StreamKind::kRgb;
  std::optional<FootSide> foot;  // 전체 화면이면 비웁니다
  int32_t frame_width = 0;       // format.width
  int32_t frame_height = 0;      // format.height
  pixel::Rect crop;              // 영상에 저장한 영역
  int64_t written_frames = 0;
  int64_t written_in_rgb_window = 0;
  int64_t missing_frames = 0;  // 카메라 단 누락
  int64_t dropped_frames = 0;  // 저장 누락
};

/**
 * 사이드카 JSON에서 분석 전 확인에 쓰는 값을 읽습니다.
 *
 * @param json 사이드카 파일 내용(UTF-8)
 * @param error 실패하면 이유를 씁니다
 * @return 읽은 값. JSON 문법이 틀리거나, schema_version이 3보다 작거나, 필요한 키가 없으면 nullopt
 * @note 필요한 키는 stream, foot, format.width, format.height, crop, frames.written, frames.written_in_rgb_window,
 *       frames.missing, frames.dropped입니다. 다른 키는 읽지 않습니다.
 */
std::optional<SidecarSummary> ReadSidecarSummary(const std::string& json, std::string* error);

/**
 * 열화상 프레임에서 환자 왼발과 오른발 영역을 정합니다.
 *
 * 열화상은 RGB와 맞춘 보정값이 없어 RGB 영역을 옮겨 쓰지 않고 따로 정합니다.
 *
 * @param frame_width 열화상 프레임 폭
 * @param frame_height 열화상 프레임 높이
 * @param pixel_regions 직접 준 영역(화소 왼쪽, 화소 오른쪽). nullopt이면 프레임 전체를 가운데 세로선에서 나눕니다
 * @param pixel_left_foot 화소 왼쪽에 찍히는 발(ShotInfo::pixel_left_foot)
 * @return 왼발 영역과 오른발 영역. 프레임 폭이 2보다 작으면 둘 다 빈 영역
 * @note 가운데 세로선 분할에서 폭이 홀수이면 가운데 한 열을 버리고 두 영역의 폭을 같게 합니다.
 */
std::pair<pixel::Rect, pixel::Rect> ThermalFootRegions(int32_t frame_width, int32_t frame_height,
                                                       const std::optional<std::pair<pixel::Rect, pixel::Rect>>& pixel_regions,
                                                       FootSide pixel_left_foot);

/** 분석 전 확인에서 찾은 문제의 종류입니다. */
enum class InputProblem : uint8_t {
  kMissingFile,       // 필요한 역할의 파일이 촬영 폴더에 없음
  kUnreadableSidecar, // 사이드카 파일은 있으나 읽지 못함
  kFewThermalFrames,  // RGB 녹화 구간 안 열화상 장 수가 최솟값보다 적음
  kThermalGap,        // 열화상에 카메라 단 누락이나 저장 누락이 있음. 분석은 할 수 있는 경고
  kThermalAbsent,     // 열화상 파일이 하나도 없음. 분석은 할 수 있는 경고
  kNotPortrait,       // 좌우 영상이 세로로 긴 영상이 아님
};

/** 분석 전 확인에서 찾은 문제 하나입니다. */
struct InputFinding {
  InputProblem problem = InputProblem::kMissingFile;
  FileRole role = FileRole::kManifest;  // 문제가 있는 파일의 역할
  bool blocking = true;                 // true면 분석을 시작하지 않습니다. 경고는 false
  int64_t value = 0;                    // 잰 값(장 수, 누락 수, 폭). 없으면 0
  int64_t limit = 0;                    // 기준값(최소 장 수, 높이). 없으면 0
  friend bool operator==(const InputFinding&, const InputFinding&) = default;
};

/** 분석 전 확인의 입력입니다. 파일 입출력은 platform이 하고 결과만 넘깁니다. */
struct AnalysisFiles {
  std::vector<FileRole> present;          // 촬영 폴더에 실제로 있는 파일의 역할
  std::optional<SidecarSummary> left;     // 왼발 영상 사이드카. 없거나 읽지 못하면 비웁니다
  std::optional<SidecarSummary> right;    // 오른발 영상 사이드카
  std::optional<SidecarSummary> thermal;  // 열화상 사이드카
};

/**
 * 관류 분석을 시작해도 되는지 확인합니다.
 *
 * 확인 순서: 필요한 역할(좌우 영상, 좌우 영상 사이드카, 열화상 사이드카, 좌우 열화상 .npy)의 파일이 있는지,
 * 있는 사이드카를 읽었는지, 좌우 영상의 저장 영역이 세로로 긴지(높이 > 폭), RGB 녹화 구간 안 열화상 장 수가
 * min_thermal_frames 이상인지, 열화상에 누락이 있는지입니다.
 *
 * 좌우 영상 문제는 분석을 막습니다(blocking). 열화상 문제는 막지 않는 경고입니다. 커프 단계 추정은 RGB 영상만 쓰기 때문입니다.
 *
 * @param files 촬영 폴더에서 읽은 결과
 * @param min_thermal_frames RGB 녹화 구간 안 열화상 장 수의 최솟값. 보통 kMinThermalFramesInRgbWindow
 * @return 찾은 문제 목록(위 순서). 비어 있거나 blocking이 모두 false이면 분석을 시작해도 됩니다
 */
std::vector<InputFinding> CheckAnalysisInputs(const AnalysisFiles& files, int64_t min_thermal_frames);

/**
 * 문제 목록에 분석을 막는 문제가 있는지 봅니다.
 *
 * @param findings CheckAnalysisInputs의 결과
 * @return blocking인 문제가 하나라도 있으면 true
 */
bool HasBlockingFinding(const std::vector<InputFinding>& findings);

}  // namespace hwlab::record

#endif  // HWLAB_RECORD_ANALYSIS_INPUTS_H_
