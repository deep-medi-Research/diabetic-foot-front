#ifndef HWLAB_PLATFORM_SHOT_FOLDER_H_
#define HWLAB_PLATFORM_SHOT_FOLDER_H_

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "hwlab/flow/capture_spec.h"
#include "hwlab/pixel/rect.h"
#include "hwlab/record/analysis_inputs.h"
#include "hwlab/record/shot.h"
#include "hwlab/record/shot_limits.h"

namespace hwlab::platform {

/**
 * UTF-8 문자열 경로를 OS 경로로 바꿉니다.
 *
 * @note Windows에서 std::string을 그대로 경로로 쓰면 시스템 코드 페이지(CP949)로 읽어 한글 폴더 이름이 깨집니다.
 */
std::filesystem::path PathFromUtf8(const std::string& utf8);

/**
 * UTF-8 경로의 파일을 엽니다. std::fopen과 같지만 Windows에서도 한글 경로를 엽니다.
 *
 * @param utf8_path 파일 경로(UTF-8)
 * @param mode std::fopen의 mode
 * @return 열린 파일. 실패하면 nullptr
 */
std::FILE* OpenFileUtf8(const std::string& utf8_path, const char* mode);

/**
 * 촬영 폴더 안 파일 하나의 전체 경로(UTF-8)를 만듭니다.
 *
 * @param root 저장 위치(UTF-8)
 * @param layout 촬영 폴더 배치
 * @param role 파일 역할
 * @return 저장 위치/폴더/파일 이름. 배치에 그 역할이 없으면 빈 문자열
 */
std::string PathOfFile(const std::string& root, const record::ShotLayout& layout, record::FileRole role);

/**
 * 촬영 폴더를 만듭니다. 위 단계(이름_생년월일, 기준)는 없으면 만들고, 마지막 단계(날짜_시각)는 새로 만들어야 성공합니다.
 *
 * @param root 저장 위치(UTF-8). 없으면 만듭니다
 * @param layout 촬영 폴더 배치
 * @param error 실패하면 이유를 씁니다
 * @return 새 폴더를 만들었으면 true. 같은 폴더가 이미 있으면 false(앞 촬영을 덮어쓰지 않기 위함)
 * @note 만든 뒤 맨 위 폴더(이름_생년월일)의 수정 시각을 지금으로 맞춥니다. 최근 환자 목록이 저장 위치 목록 한 번으로
 *       환자마다 마지막 촬영 시각을 읽게 하기 위함입니다. 맞추지 못해도 true를 돌려줍니다.
 */
bool CreateShotFolder(const std::string& root, const record::ShotLayout& layout, std::string* error);

/**
 * 비어 있는 번호를 찾아 촬영 폴더를 만듭니다. 먼저 번호 없이, 있으면 2, 3, ... 순서로 시도합니다.
 *
 * @param root 저장 위치(UTF-8)
 * @param layout record::LayoutOf의 결과
 * @param max_number 시도할 가장 큰 번호
 * @param error 실패하면 이유를 씁니다
 * @return 실제로 만든 폴더의 배치(record::NumberLayout 결과일 수 있음). 모두 있거나 만들 수 없으면 nullopt
 */
std::optional<record::ShotLayout> CreateFreshShotFolder(const std::string& root, const record::ShotLayout& layout,
                                                        int32_t max_number, std::string* error);

/**
 * 촬영 폴더에 매니페스트를 씁니다. 바이트를 바꾸지 않고 그대로 씁니다.
 *
 * @param root 저장 위치(UTF-8)
 * @param layout 촬영 폴더 배치. 폴더가 이미 있어야 합니다
 * @param json record::BuildManifestJson의 결과
 * @param error 실패하면 이유를 씁니다
 * @return 썼으면 true. 같은 이름의 파일이 이미 있으면 덮어쓰지 않고 false
 */
bool WriteManifest(const std::string& root, const record::ShotLayout& layout, const std::string& json,
                   std::string* error);

/** 촬영에서 저장하는 RGB 영상입니다. */
enum class SavedVideos : uint8_t {
  kFull,         // 자르지 않은 전체 화면 영상 하나
  kFeet,         // 좌우 분할한 왼발 영상과 오른발 영상
  kFullAndFeet,  // 전체 화면 영상과 좌우 분할 영상
};

/**
 * 녹화를 시작할 수 있게 준비한 촬영 폴더입니다. 경로는 모두 UTF-8 전체 경로입니다.
 *
 * @note 이번 촬영에 없는 역할의 경로는 빈 문자열입니다(열화상 장치가 없으면 thermal_로 시작하는 다섯 경로,
 *       SavedVideos::kFull이면 left_와 right_로 시작하는 네 경로).
 */
struct PreparedShot {
  record::ShotLayout layout;     // 실제로 만든 폴더의 배치. 번호를 넣은 배치일 수 있습니다
  record::FootSide pixel_left_foot = record::FootSide::kRight;  // 매니페스트에 쓴 화소 왼쪽 발
  std::string folder;            // 촬영 폴더
  std::string manifest;          // 매니페스트
  std::string video;             // 전체 화면 영상
  std::string sidecar;           // 전체 화면 영상의 사이드카
  std::string left_video;        // 왼발 영상(환자 기준)
  std::string left_sidecar;      // 왼발 영상의 사이드카
  std::string right_video;       // 오른발 영상(환자 기준)
  std::string right_sidecar;     // 오른발 영상의 사이드카
  std::string thermal_raw;       // 열화상 원시값
  std::string thermal_sidecar;   // 열화상 사이드카
  std::string thermal_preview;   // 열화상을 볼 수 있게 옮긴 영상
  std::string thermal_left_array;   // 왼발 열화상 .npy(thermal_left.npy)
  std::string thermal_right_array;  // 오른발 열화상 .npy(thermal_right.npy)
};

/**
 * 촬영 폴더를 만들고 매니페스트를 써서 녹화를 시작할 수 있게 준비합니다.
 *
 * 영상 역할은 videos가 정합니다(전체 화면, 좌우 분할, 둘 다). 영상마다 사이드카가 하나씩 붙습니다.
 * devices에 열화상 장치가 있으면 열화상 원시값, 열화상 사이드카, 열화상 확인용 영상, 좌우 열화상 .npy를 더합니다.
 * 파일 이름은 record::LayoutOf가 정합니다.
 *
 * @param root 저장 위치(UTF-8). 없으면 만듭니다
 * @param info 촬영 정보. 시각은 호출하는 쪽이 한 번만 읽어 넘깁니다
 * @param codec 촬영 선언의 코덱. 영상 확장자를 정합니다
 * @param videos 저장하는 RGB 영상
 * @param devices 이번 촬영에 쓰는 장치. 매니페스트의 장치 목록이 됩니다
 * @param max_number 같은 폴더가 이미 있을 때 폴더 이름 뒤에 넣는 번호의 최대값
 * @param error 실패하면 이유를 씁니다
 * @return 준비한 촬영. 촬영 정보가 검증을 통과하지 않거나, 폴더를 만들지 못하거나, 매니페스트를 쓰지 못하면 nullopt
 * @note 같은 폴더가 이미 있으면 번호를 넣은 폴더(날짜_시각_2, 날짜_시각_3, ...)를 만들어 앞 촬영을 덮어쓰지 않습니다.
 *       매니페스트를 쓰지 못하면 이번에 만든 촬영 폴더를 지웁니다. 상위 폴더(이름_생년월일, 발목 혈압 단계)는 남깁니다.
 */
std::optional<PreparedShot> PrepareShot(const std::string& root, const record::ShotInfo& info, flow::VideoCodec codec,
                                        SavedVideos videos, const std::vector<record::ShotDevice>& devices,
                                        int32_t max_number, std::string* error);

/**
 * 영상 파일 없이 끝난 촬영의 매니페스트와 빈 촬영 폴더를 지웁니다.
 *
 * 녹화 전에 중지하면 매니페스트만 있는 폴더가 남고, 이 폴더는 촬영한 것처럼 보이므로 지웁니다.
 *
 * @param shot PrepareShot의 결과
 * @return 촬영 폴더를 지웠으면 true. 영상 파일(전체 화면, 왼발, 오른발 중 하나라도)이 있거나, 매니페스트 말고
 *         다른 파일이 있거나, 지우지 못하면 false
 * @note 매니페스트 말고 다른 파일(예: 열화상 원시값)이 있으면 매니페스트도 지우지 않습니다. 매니페스트가 그 파일의 환자 정보를 갖기 때문입니다.
 */
bool DiscardUnrecordedShot(const PreparedShot& shot);

/**
 * 녹화가 끝난 열화상 원시값을 좌우 발로 잘라 thermal_left.npy와 thermal_right.npy로 씁니다.
 *
 * 열화상 사이드카에서 프레임 크기와 기록한 장 수를 읽고, record::ThermalFootRegions로 왼발과 오른발 영역을 정해
 * WriteNpy로 씁니다. 화소 왼쪽 발은 shot.pixel_left_foot입니다.
 *
 * @param shot 열화상 장치로 준비한 PrepareShot의 결과
 * @param pixel_regions 직접 정한 열화상 영역(화소 왼쪽, 화소 오른쪽). nullopt이면 가운데 세로선에서 나눕니다
 * @param error 실패하면 이유를 씁니다
 * @return 두 파일을 모두 썼으면 true
 * @note 영역이 세로로 길지 않으면(높이 <= 폭) 쓰지 않고 실패합니다. 관류 분석은 한 발씩 세로로 긴 영상만 받습니다.
 *       실패하면 이 호출이 쓴 .npy를 지우고, 원시값 파일은 어떤 경우에도 지우지 않습니다(.npy를 다시 만드는 원본).
 */
bool WriteThermalArrays(const PreparedShot& shot,
                        const std::optional<std::pair<pixel::Rect, pixel::Rect>>& pixel_regions, std::string* error);

/**
 * 분석 전 확인(record::CheckAnalysisInputs)에 넘길 값을 촬영 폴더에서 읽습니다.
 *
 * @param shot PrepareShot의 결과
 * @return 있는 파일의 역할과 읽은 사이드카. 사이드카가 없거나 읽지 못하면 그 값은 비웁니다
 */
record::AnalysisFiles ReadAnalysisFiles(const PreparedShot& shot);

/** Windows 경로 길이 상한입니다. MAX_PATH(260)에서 끝의 NUL을 뺀 UTF-16 코드 단위 수입니다. */
inline constexpr int32_t kWindowsMaxPathLength = 259;

/**
 * 촬영 뒤에도 디스크에 남겨 둘 바이트입니다(record::JudgeFreeSpace의 reserve_bytes).
 *
 * @note 관류 분석 결과(요약, 보고서, 그림, GIF)와 OS 임시 파일 몫으로 정한 값이며 실측 근거는 없습니다.
 */
inline constexpr int64_t kFreeSpaceReserveBytes = 1'000'000'000;

/**
 * 저장 위치의 남은 공간이 이 값 미만이면 촬영 전에 미리 알립니다(record::FreeSpace::kLow).
 *
 * @note 한 번 촬영이 약 1.08 GB(1280x720 60fps, 열화상, PPG 포함)를 쓰므로 여러 번 촬영할 공간을 기준으로 한 값입니다.
 */
inline constexpr int64_t kFreeSpaceLowBytes = 10'000'000'000;

/**
 * 이 OS에서 파일 경로 길이의 상한입니다.
 *
 * @return Windows는 kWindowsMaxPathLength. 그 밖의 OS는 nullopt(검사하지 않음)
 * @note 앱이 긴 경로(\\?\ 접두나 longPathAware 매니페스트)를 쓰지 않으므로 Windows API가 이 상한에서 실패합니다.
 */
std::optional<int32_t> MaxPathLength();

/**
 * 저장 위치가 있는 디스크의 남은 바이트를 읽습니다(std::filesystem::space의 available).
 *
 * @param root 저장 위치(UTF-8). 아직 없으면 가장 가까운 상위 폴더로 읽습니다
 * @return 남은 바이트. 읽지 못하면 nullopt
 */
std::optional<int64_t> FreeBytesAt(const std::string& root);

/**
 * PrepareShot이 만들 촬영 폴더 배치를 폴더를 만들지 않고 구합니다. 번호는 넣지 않습니다.
 *
 * @return PrepareShot과 같은 역할로 만든 record::LayoutOf의 결과. 촬영 정보가 검증을 통과하지 않으면 nullopt
 */
std::optional<record::ShotLayout> PlanShotLayout(const record::ShotInfo& info, flow::VideoCodec codec, SavedVideos videos,
                                                 const std::vector<record::ShotDevice>& devices);

/** 디스크 여유 판정과 화면에 보일 문장입니다. */
struct FreeSpaceNotice {
  record::FreeSpace verdict = record::FreeSpace::kUnknown;
  std::string text;  // kLow, kShort일 때만 있습니다. 나머지는 빈 문자열
};

/**
 * 디스크 여유를 판정하고 낮음(kLow)과 부족(kShort)이면 그 문장을 만듭니다.
 *
 * @param free_bytes FreeBytesAt의 결과
 * @param estimated_bytes record::EstimateShotBytes의 결과
 * @return 판정과 문장. 예비량은 kFreeSpaceReserveBytes, 경고 기준은 kFreeSpaceLowBytes
 */
FreeSpaceNotice DescribeFreeSpace(std::optional<int64_t> free_bytes, int64_t estimated_bytes);

/**
 * 촬영을 끝까지 저장하지 못했을 때 화면에 보일 문장을 만듭니다.
 *
 * @param free_bytes 실패를 안 시점에 다시 읽은 남은 바이트. 예비량(kFreeSpaceReserveBytes) 미만이면 공간 부족을 원인으로 적습니다
 * @param folder 저장된 부분이 남아 있는 촬영 폴더
 * @param detail 공간 부족이 아닐 때 덧붙일 파일 이름이나 오류 문장. 비어도 됩니다
 */
std::string DescribeSaveFailure(std::optional<int64_t> free_bytes, const std::string& folder, const std::string& detail);

/**
 * 촬영 폴더를 만들기 전에 경로 길이와 디스크 여유를 검사합니다.
 *
 * @param root 저장 위치(UTF-8)
 * @param layout 검사할 배치. 같은 초 재촬영 번호까지 넣은 배치(record::NumberLayout)를 주면 가장 긴 경우를 검사합니다
 * @param estimated_bytes record::EstimateShotBytes의 결과
 * @param free_bytes FreeBytesAt의 결과. nullopt이면 디스크 여유는 검사하지 않습니다
 * @param max_path_length MaxPathLength의 결과. nullopt이면 경로 길이는 검사하지 않습니다
 * @return 촬영을 시작하면 안 되는 이유(한국어). 통과하면 빈 문자열
 * @note 경로 길이는 분석 결과(perfusion/<판>/<폴더 이름>_pi_timecourse.png)와 results 폴더로 모은 결과까지 셉니다
 *       (record::LongestShotPathLength).
 */
std::string CheckShotLimits(const std::string& root, const record::ShotLayout& layout, int64_t estimated_bytes,
                            std::optional<int64_t> free_bytes, std::optional<int32_t> max_path_length);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_SHOT_FOLDER_H_
