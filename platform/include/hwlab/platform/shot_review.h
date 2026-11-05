#ifndef HWLAB_PLATFORM_SHOT_REVIEW_H_
#define HWLAB_PLATFORM_SHOT_REVIEW_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/pixel/convert.h"
#include "hwlab/record/sidecar.h"
#include "hwlab/timestamp.h"

namespace hwlab::platform {

/** 매니페스트에서 결과 창이 쓰는 값입니다. 파일 이름은 `files` 항목의 촬영 폴더 기준 이름이고, 없으면 빈 문자열입니다. */
struct ShotManifest {
  std::string left_video;
  std::string left_sidecar;
  std::string right_video;
  std::string right_sidecar;
  std::string thermal_raw;
  std::string thermal_sidecar;
  /** `pixel_left_foot`: RGB와 열화상 프레임의 화소 왼쪽에 찍힌 발. 없으면 nullopt입니다. */
  std::optional<record::FootSide> pixel_left_foot;
  /** `criterion`: 촬영 때 입력한 검사 기준(커프 압력, 수축기 혈압 대비 %). 없거나 음수이면 nullopt입니다. */
  std::optional<int32_t> criterion;
  /** `blood_pressure.ankle_level.name`: 촬영 때 고른 발목 혈압 단계(0, V, D, M, S). schema 6 전 매니페스트이면 비었습니다. */
  std::string ankle_level;
  /** `lighting.source`: "external"(외부 조명), "app"(앱이 켜는 조명), "unknown". 없으면 비었습니다. */
  std::string light_source;
  /** `lighting.product`: 조명 제품 설명. 없으면 비었습니다. */
  std::string light_product;
};

/**
 * 매니페스트 내용에서 영상, 사이드카, 열화상 원시값 파일 이름과 화소 왼쪽 발을 읽습니다.
 *
 * @param manifest_json `<폴더 이름>_shot.json`의 내용(UTF-8)
 * @param error 읽지 못하면 이유를 씁니다. nullptr이면 쓰지 않습니다
 * @return 파일 이름. JSON 문법이 틀리거나 `files.left_video`, `files.right_video`가 없으면 nullopt
 */
std::optional<ShotManifest> ReadShotManifest(const std::string& manifest_json, std::string* error = nullptr);

/** RGB 사이드카의 `settings`: 녹화 때 카메라에 적용한 설정입니다. 기록에 없는 값은 nullopt입니다. */
struct RecordedCameraSettings {
  std::string mode;                        // "manual" 또는 "auto". 없으면 비었습니다
  std::optional<int32_t> exposure_step;    // 노출 단계(See3CAM, 작을수록 짧음)
  std::optional<double> exposure_ms;       // 노출 시간(밀리초)
  std::optional<int32_t> gain;             // 게인
  std::optional<bool> white_balance_auto;  // 자동 화이트밸런스
  std::optional<int32_t> white_balance_k;  // 고정 화이트밸런스 색온도(K)
  std::optional<bool> locked;              // 녹화 동안 설정을 잠갔는지
};

/** 사이드카에서 결과 창이 쓰는 값입니다. */
struct SidecarFrames {
  int32_t width = 0;   // format.width
  int32_t height = 0;  // format.height
  /** format.frame_rate: 녹화 fps. 없으면 nullopt입니다. */
  std::optional<int32_t> frame_rate;
  /** settings: RGB 카메라 설정. 열화상 사이드카는 기록이 없어 모두 nullopt입니다. */
  RecordedCameraSettings camera;
  /** per_frame.common_time_us: 파일에 쓴 프레임마다의 공통 시각입니다. 파일 안 순서와 같습니다. */
  std::vector<Timestamp> common_time;
  /** display_range: 열화상 확인용 영상의 표시 범위. RGB 사이드카와 값이 null인 열화상 사이드카는 nullopt입니다. */
  std::optional<pixel::Y16Range> display_range;
};

/**
 * 사이드카 내용에서 프레임 크기, 프레임별 공통 시각, 표시 범위를 읽습니다.
 *
 * @param sidecar_json 사이드카 파일 내용(UTF-8)
 * @param error 읽지 못하면 이유를 씁니다. nullptr이면 쓰지 않습니다
 * @return 읽은 값. JSON 문법이 틀리거나, format.width, format.height, per_frame.common_time_us가 없거나,
 *         공통 시각에 정수가 아닌 값이 있으면 nullopt
 */
std::optional<SidecarFrames> ReadSidecarFrames(const std::string& sidecar_json, std::string* error = nullptr);

/**
 * 원시값 파일(`.raw16`)에서 번호로 프레임 하나를 읽습니다.
 *
 * @param path 원시값 파일 경로(UTF-8)
 * @param width 프레임 폭(사이드카 format.width)
 * @param height 프레임 높이(사이드카 format.height)
 * @param index 프레임 번호(0부터, 파일 안 순서)
 * @param out 결과 Y16 값. width * height개로 맞춥니다
 * @return 읽었으면 true. 파일을 열지 못하거나 번호가 파일 범위 밖이면 false
 * @note 원시값 파일은 머리글 없이 리틀 엔디언 uint16을 이어 쓴 파일이라 번호 i의 위치는 i * width * height * 2입니다.
 */
bool ReadRaw16Frame(const std::string& path, int32_t width, int32_t height, int64_t index, std::vector<uint16_t>* out);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_SHOT_REVIEW_H_
