#ifndef HWLAB_PIXEL_BRIGHTNESS_H_
#define HWLAB_PIXEL_BRIGHTNESS_H_

#include <cstdint>
#include <optional>
#include <span>

#include "hwlab/frame.h"
#include "hwlab/pixel/rect.h"

namespace hwlab::pixel {

/** 영역 안 모든 화소의 밝기 합계입니다. 밝기는 UYVY와 NV12에서 Y, Y16에서 원시값입니다. */
struct Brightness {
  int64_t pixel_count = 0;
  int64_t sum = 0;
  int64_t clipped_pixels = 0;  // 밝기가 clip_level 이상인 화소 수
};

/**
 * 영역의 밝기 합계와 포화 화소 수를 잽니다.
 *
 * 표본을 건너뛰지 않고 모든 화소를 읽습니다. 블랙 레벨 변화(약 +7)를 표본 오차 없이 비교하기 위해서입니다.
 *
 * @param frame 잴 프레임
 * @param rect 잴 영역
 * @param clip_level 이 밝기 이상을 포화 화소로 셉니다
 * @return 화소 수, 합계, 포화 화소 수. 영역이 비었거나 프레임을 벗어나면 화소 수가 0
 */
Brightness MeasureBrightness(const FrameView& frame, const Rect& rect, int32_t clip_level);

/**
 * 평균 밝기를 구합니다.
 *
 * @param brightness MeasureBrightness의 결과
 * @return 합계를 화소 수로 나눈 값. 화소 수가 0이면 0
 */
double MeanOf(const Brightness& brightness);

/**
 * 녹화한 프레임들의 영역 밝기가 프레임마다 얼마나 흔들리는지 요약한 값입니다.
 *
 * LED를 PWM으로 조절하면 PWM 주기와 노출 시간이 맞지 않을 때 노출 하나에 들어가는 켜짐 시간이 프레임마다 달라지고,
 * 프레임 밝기가 PWM 주파수와 fps의 차이로 정해지는 느린 주기(맥놀이)로 오르내립니다. 이 값으로 그 주기를 찾습니다.
 * 밝기 단위는 화소 하나의 밝기(UYVY와 NV12의 Y)입니다.
 */
struct Flicker {
  int64_t frame_count = 0;
  double mean = 0.0;                // 프레임 평균 밝기들의 평균
  double stddev = 0.0;              // 프레임 평균 밝기들의 표준편차(모집단)
  double peak_to_peak = 0.0;        // 가장 밝은 프레임과 가장 어두운 프레임의 평균 밝기 차
  double periodic_fraction = 0.0;   // 가장 큰 주기 성분이 흔들림(직선 추세를 뺀 분산)에서 차지하는 비율, 0~1
  std::optional<double> dominant_period_frames;  // 주된 주기(프레임 수, 소수). 주기 성분이 기준보다 약하면 비어 있음
};

/**
 * 프레임별 영역 밝기 합계로 깜빡임을 요약합니다.
 *
 * 직선 추세(블랙 레벨이나 LED 온도에 따른 느린 변화)를 뺀 뒤 주파수 성분을 소수 단위로 찾습니다.
 * 찾는 주기는 2프레임부터 프레임 수의 절반까지입니다. 두 번 이상 반복되지 않은 주기는 추세와 구분하지 않습니다.
 *
 * @param sums 프레임 순서대로의 영역 밝기 합계(Brightness::sum). 모든 프레임의 영역 크기가 같아야 합니다
 * @param pixel_count 영역의 화소 수(Brightness::pixel_count)
 * @param min_periodic_fraction 가장 큰 주기 성분의 비율이 이 값 이상일 때만 주기로 봅니다(예: 0.5)
 * @return 요약. 프레임이 4장 미만이거나 pixel_count가 0 이하이면 frame_count만 채웁니다
 * @note 주기는 fps로 접힌 값입니다. PWM 주파수를 f, fps를 r라 하면 r / |f - k r| (k는 f / r에 가장 가까운 정수)
 *       프레임입니다. 노출 시간이 PWM 주기의 정수배이면 켜짐 시간이 프레임마다 같아 주기가 나타나지 않습니다.
 */
Flicker SummarizeFlicker(std::span<const int64_t> sums, int64_t pixel_count, double min_periodic_fraction);

/** LED 변경이 영상 밝기에 나타났는지 판정한 결과입니다. 사이드카 light.response에 남깁니다. */
enum class LightResponse : uint8_t {
  kUnknown,   // 판정하지 않음(기준값 없음, 프레임 부족, 보낸 밝기 변화가 0이거나 모름)
  kApplied,   // 보낸 방향으로 기준 이상 바뀜
  kNotSeen,   // 기준 이상 바뀌지 않았거나 반대 방향으로 바뀜
};

/**
 * LED 변경의 반영 판정 기준입니다. LED 보드와 카메라 조합의 실측값이라 platform이 넘깁니다.
 *
 * @note 실측하지 않았습니다. window_frames나 min_mean_change가 0 이하이면 판정하지 않습니다(kUnknown).
 */
struct LightResponseRule {
  int32_t applied_frame = 0;     // LED 명령 뒤 새 밝기로 보는 첫 프레임(명령 뒤 받은 프레임을 1부터 셈)
  int32_t window_frames = 0;     // 명령 전과 반영 뒤에 각각 평균할 프레임 수
  double min_mean_change = 0.0;  // 반영으로 보는 평균 밝기(화소 하나의 Y) 변화의 최솟값
};

/**
 * LED 변경이 영상 밝기에 나타났는지 판정합니다.
 *
 * 명령 전 마지막 window_frames장과 반영 뒤 처음 window_frames장의 평균 밝기를 비교해, 보낸 방향으로 min_mean_change 이상
 * 바뀌었으면 반영으로 봅니다.
 *
 * @param before 명령 전 프레임의 평균 밝기(MeanOf), 시간 순서. 마지막 window_frames장을 씁니다
 * @param after 반영 프레임(applied_frame)부터의 평균 밝기, 시간 순서. 처음 window_frames장을 씁니다
 * @param sent_delta_percent 보낸 밝기에서 직전 밝기를 뺀 값(%)
 * @param rule 판정 기준
 * @return 기준값이 없거나 프레임이 모자라거나 sent_delta_percent가 0이면 kUnknown
 * @note 영상이 포화되었거나 자동 맞춤이 노출을 다시 맞춘 뒤의 프레임이면 LED가 바뀌어도 밝기가 바뀌지 않아 kNotSeen이 됩니다.
 *       장치가 LED 밝기를 돌려주지 않으므로 이 판정이 명령이 반영되었는지 볼 수 있는 유일한 근거입니다.
 */
LightResponse JudgeLightResponse(std::span<const double> before, std::span<const double> after,
                                 int32_t sent_delta_percent, const LightResponseRule& rule);

}  // namespace hwlab::pixel

#endif  // HWLAB_PIXEL_BRIGHTNESS_H_
