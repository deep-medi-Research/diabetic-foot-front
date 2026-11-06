#ifndef HWLAB_RECORD_SHOT_LIMITS_H_
#define HWLAB_RECORD_SHOT_LIMITS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/record/shot.h"

namespace hwlab::record {

/** 영상 한 장의 크기(화소)입니다. */
struct VideoSize {
  int32_t width = 0;
  int32_t height = 0;
};

/**
 * 촬영 한 번이 디스크에 쓸 양을 정하는 값입니다.
 *
 * @note 프레임 크기는 platform과 화면이 촬영 선언과 좌우 영역에서 정해 넘깁니다.
 */
struct ShotSizePlan {
  int64_t rgb_frames = 0;           // 녹화할 RGB 장 수(촬영 선언의 target_frames)
  int32_t rgb_frame_rate = 0;       // RGB fps. 열화상 장 수를 녹화 시간으로 셀 때 씁니다
  std::vector<VideoSize> rgb_videos;  // 저장하는 RGB 영상마다 하나(전체 화면, 왼발, 오른발)
  bool thermal = false;             // 열화상을 녹화하는지
  VideoSize thermal_size;           // 열화상 프레임 크기
  int64_t thermal_record_period_us = 0;  // 열화상 기록 주기. 0이면 받은 프레임을 모두 기록합니다
  int32_t thermal_max_rate_hz = 0;  // 열화상 장치가 내는 최대 Hz. 기록 주기가 0일 때 장 수의 상한으로 씁니다
};

/** 사이드카가 한 장마다 쓰는 바이트의 상한입니다. per_frame 배열 6개(정수 하나에 최대 20자와 구분자)입니다. */
inline constexpr int64_t kSidecarBytesPerFrame = 256;

/**
 * 촬영 한 번이 쓸 바이트의 상한을 셉니다.
 *
 * RGB 영상은 코덱과 관계없이 무압축 UYVY(화소당 2바이트)로 셉니다. 열화상은 원시값(화소당 2바이트), 확인용 AVI
 * (UYVY, 화소당 2바이트), 좌우 .npy(합해서 화소당 2바이트 이하)를 셉니다. 영상마다 사이드카(kSidecarBytesPerFrame)를 더합니다.
 *
 * @param plan 촬영 크기
 * @return 바이트 수. 값이 음수이거나 0인 항목은 0바이트로 셉니다
 * @note FFV1은 영상 내용에 따라 압축률이 달라 상한으로 무압축 크기를 씁니다. 관류 분석 결과는 reserve_bytes(JudgeFreeSpace)가 맡습니다.
 */
int64_t EstimateShotBytes(const ShotSizePlan& plan);

/** 디스크 여유 판정입니다. */
enum class FreeSpace : uint8_t {
  kEnough,   // 여유가 필요한 양 이상이고 경고 기준 이상
  kLow,      // 여유가 필요한 양 이상이지만 경고 기준 미만. 촬영은 시작할 수 있으나 촬영 중 모자랄 수 있음
  kShort,    // 여유가 모자람. 촬영을 시작하지 않습니다
  kUnknown,  // 여유를 읽지 못함
};

/** 디스크 여유 판정 결과입니다. */
struct FreeSpaceJudgement {
  FreeSpace verdict = FreeSpace::kUnknown;
  int64_t required_bytes = 0;  // 촬영 추정량과 예비량의 합
};

/**
 * 디스크 여유로 촬영을 시작할 수 있는지 판정합니다.
 *
 * @param free_bytes 저장 위치가 있는 디스크의 남은 바이트. 읽지 못했으면 nullopt
 * @param estimated_bytes EstimateShotBytes의 결과
 * @param reserve_bytes 촬영 뒤에도 남겨 둘 바이트(분석 결과, OS 임시 파일)
 * @param low_bytes 이 값 미만이면 미리 알리는 경고 기준. 0이면 경고하지 않습니다
 * @return free_bytes가 estimated_bytes + reserve_bytes보다 작으면 kShort, 필요한 양 이상이지만 low_bytes보다 작으면 kLow,
 *         그 밖에는 kEnough, 읽지 못했으면 kUnknown
 */
FreeSpaceJudgement JudgeFreeSpace(std::optional<int64_t> free_bytes, int64_t estimated_bytes, int64_t reserve_bytes,
                                  int64_t low_bytes);

/**
 * 촬영 폴더 안에서 가장 긴 파일 경로의 길이를 셉니다.
 *
 * 배치의 모든 파일과 분석 결과 중 가장 긴 파일(perfusion/<판>/분석이름_<뒷부분>)의 경로를 저장 위치/폴더/파일 이름으로
 * 이어 셉니다. 판 이름은 3자, 결과 파일 이름의 뒷부분은 23자까지로 봅니다. 분석 이름이 있으면 분석결과 폴더로 모은 결과
 * (저장 위치/분석결과/CollectedResultPrefix(layout, kMaxReanalysisNumber)_<판><뒷부분>)도 셉니다.
 * 길이는 UTF-16 코드 단위입니다(Windows 경로 상한의 단위). 한글 한 글자는 1, U+10000 이상 문자는 2입니다.
 *
 * @param root 저장 위치(UTF-8). 끝에 '/'나 '\\'가 있으면 구분자를 더하지 않습니다
 * @param layout 촬영 폴더 배치(LayoutOf 또는 NumberLayout의 결과)
 * @param analysis_name 분석 결과 파일 이름의 앞부분. 비면 분석 결과와 분석결과 폴더 경로는 세지 않습니다
 * @return 가장 긴 경로의 길이. 끝의 NUL은 세지 않습니다
 * @note 상한(Windows는 259)은 platform이 정합니다. 폴더 경로는 파일 경로보다 짧으므로 따로 세지 않습니다.
 */
int32_t LongestShotPathLength(const std::string& root, const ShotLayout& layout, const std::string& analysis_name);

}  // namespace hwlab::record

#endif  // HWLAB_RECORD_SHOT_LIMITS_H_
