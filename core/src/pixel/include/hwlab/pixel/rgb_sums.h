#ifndef HWLAB_PIXEL_RGB_SUMS_H_
#define HWLAB_PIXEL_RGB_SUMS_H_

#include <array>
#include <cstdint>

#include "hwlab/frame.h"
#include "hwlab/pixel/rect.h"

namespace hwlab::pixel {

/** 영역에서 고른 화소들의 R, G, B 합계와 채널별 포화 화소 수입니다. 배열은 R, G, B 순서입니다. */
struct RgbSums {
  int64_t pixel_count = 0;                // 읽은 화소 수
  std::array<int64_t, 3> sum{};           // 채널별 값의 합계
  std::array<int64_t, 3> saturated{};     // 채널 값이 255인 화소 수
};

/**
 * UYVY 영역의 채널별 RGB 합계와 포화 화소 수를 잽니다.
 *
 * 영역 왼쪽 위 화소부터 가로세로 step 화소마다 하나씩 읽고, 각 화소를 UyvyToRgbPreview와 같은 정수 계산(BT.601 제한 범위)으로
 * RGB로 바꿉니다. 그래서 화면에 보이는 색과 이 값이 같은 계산에서 나옵니다.
 *
 * @param uyvy UYVY 형식 프레임
 * @param rect 잴 영역. 프레임 안이어야 합니다. 색차 격자에 맞지 않아도 됩니다
 * @param step 가로세로 몇 화소마다 하나씩 읽을지. 1이면 모든 화소
 * @return 화소 수, 합계, 포화 화소 수. 형식이 UYVY가 아니거나 영역이 비었거나 프레임을 벗어나거나 step이 1보다 작으면 화소 수가 0
 * @note 화면 표시용입니다. 포화는 RGB로 바꾼 값 기준이라 밝기(Y)의 포화 화소(MeasureBrightness)와 수가 다를 수 있습니다.
 */
RgbSums MeasureRgb(const FrameView& uyvy, const Rect& rect, int32_t step);

}  // namespace hwlab::pixel

#endif  // HWLAB_PIXEL_RGB_SUMS_H_
