#ifndef HWLAB_FLOW_CAPTURE_SPEC_H_
#define HWLAB_FLOW_CAPTURE_SPEC_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/flow/flow.h"
#include "hwlab/flow/settings_plan.h"
#include "hwlab/timing/settle.h"

namespace hwlab::flow {

/** 영상 파일의 저장 방식입니다. */
enum class VideoCodec : uint8_t {
  kFfv1,          // 무손실 압축, MKV. 디스크가 무압축 속도를 버티지 못하는 PC를 위한 기본값
  kUncompressed,  // 무압축 UYVY, AVI(OpenDML)
};

/**
 * 코덱에 맞는 영상 파일 확장자를 구합니다.
 *
 * @return "mkv"(FFV1) 또는 "avi"(무압축). 점은 넣지 않습니다
 */
const char* ExtensionOf(VideoCodec codec);

/**
 * 촬영 한 번의 선언입니다.
 *
 * 실행 순서는 이 값으로 core가 정하고, platform과 화면은 실행만 합니다. 보통 specs 폴더의 JSON 선언 파일에서 읽습니다(ParseCaptureSpec).
 */
struct CaptureSpec {
  std::string device;  // 장치 이름에 들어 있는 문자열
  int32_t width = 0;
  int32_t height = 0;
  SettingsSpec settings;
  timing::SettleRule settle;  // 카메라마다 실측한 값(설계 문서 표 5)
  int64_t target_frames = 0;
  int32_t clip_level = 0;     // 이 밝기 이상을 포화 화소로 셉니다
  int32_t buffer_frames = 0;  // 쓰기가 밀릴 때 버틸 프레임 수
  VideoCodec codec = VideoCodec::kFfv1;
  // 열화상 기록 주기(timing::RecordRateRule). 0이면 받은 프레임을 모두 기록합니다. 선언 파일의 필수 키입니다.
  // 관류 분석이 30초에 225장 이상을 요구하므로 111111(9.0Hz, 30Hz 장치에서 30초에 270장)을 씁니다
  int64_t thermal_record_period_us = 0;
};

/**
 * 촬영 선언에서 촬영 흐름의 규칙을 꺼냅니다.
 *
 * @return 안정화 실측값, 목표 프레임 수, 설정 방식
 */
FlowRule RuleOf(const CaptureSpec& spec);

/**
 * 촬영 선언의 폭과 높이 최댓값(화소)입니다.
 *
 * @note 장치의 한계가 아니라 UYVY 한 장의 바이트 수(폭 x 높이 x 2)가 int32 안에 들게 하는 값입니다.
 */
inline constexpr int32_t kMaxFrameSide = 16384;

/**
 * 직접 지정에서 받는 노출 단계의 최솟값입니다.
 *
 * @note UVC 노출 시간의 단위가 100us라 단계 -14(61us)부터는 카메라에 보낼 수 없습니다.
 */
inline constexpr int32_t kMinExposureStep = -13;

/**
 * 직접 지정에서 받는 노출 단계의 최댓값입니다.
 *
 * @note 단계 0은 1초입니다. sdk_frame_rate는 1 이상이라 1초보다 긴 노출은 어떤 fps의 한 프레임에도 들지 않습니다.
 */
inline constexpr int32_t kMaxExposureStep = 0;

/**
 * 게인의 최댓값입니다.
 *
 * @note UVC 게인 제어 값이 부호 없는 2바이트라는 형식의 한계입니다. 장치마다 실제 범위는 이보다 좁습니다.
 */
inline constexpr int32_t kMaxGain = 65535;

/**
 * 색온도(K)의 최댓값입니다. 최솟값은 1입니다.
 *
 * @note UVC 색온도 제어 값이 부호 없는 2바이트라는 형식의 한계입니다. 장치마다 실제 범위는 이보다 좁습니다.
 */
inline constexpr int32_t kMaxWhiteBalanceK = 65535;

/** 촬영 선언 검증에서 찾은 문제입니다. */
enum class SpecProblem : uint8_t {
  kNoDevice,
  kBadSize,          // 폭과 높이는 1~kMaxFrameSide이고 UYVY 색차 격자(폭 짝수)에 맞아야 합니다
  kBadFrameRate,
  kBadExposure,      // 직접 지정의 노출 단계가 kMinExposureStep~kMaxExposureStep 밖
  kBadGain,          // 0~kMaxGain 밖
  kBadWhiteBalance,  // 1~kMaxWhiteBalanceK 밖. 자동 맞춤도 색온도는 이 값으로 고정합니다
  kBadTargetFrames,
  kBadSettleRule,    // 반영 프레임과 안정화 프레임은 1 이상
  kBadClipLevel,     // 1~255
  kBadBufferFrames,
  kBadThermalRecordPeriod,  // 음수
};

/**
 * 촬영 선언의 값 범위를 검증합니다.
 *
 * @param spec 검증할 선언
 * @return 찾은 문제 목록. 비어 있으면 실행할 수 있는 선언입니다
 */
std::vector<SpecProblem> ValidateCaptureSpec(const CaptureSpec& spec);

/**
 * JSON 선언 파일의 내용을 읽습니다.
 *
 * 오타로 생긴 모르는 키를 조용히 넘기지 않고 거부합니다. 숫자는 정수만 받습니다.
 * int32 칸에 들지 않는 정수는 잘라 넣지 않고 거부합니다.
 *
 * @param json 선언 파일 내용(UTF-8)
 * @param error 실패하면 이유와 위치를 씁니다
 * @return 읽은 선언. 모르는 키, 빠진 키, 형식이 다른 값, int32 범위를 벗어난 값이 있으면 nullopt
 * @note 값 범위는 보지 않습니다. 읽은 뒤 ValidateCaptureSpec으로 검증합니다.
 */
std::optional<CaptureSpec> ParseCaptureSpec(const std::string& json, std::string* error);

}  // namespace hwlab::flow

#endif  // HWLAB_FLOW_CAPTURE_SPEC_H_
