#ifndef HWLAB_PIXEL_UYVY_RGB_H_
#define HWLAB_PIXEL_UYVY_RGB_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "hwlab/pixel/convert.h"

namespace hwlab::pixel {

/**
 * UYVY 행에서 화소 하나를 RGB로 바꿉니다(BT.601 제한 범위, 정수 계산).
 *
 * 미리보기(UyvyToRgbPreview)와 영역 RGB 합계(MeasureRgb)가 이 함수 하나를 써서, 화면의 색과 패널의 숫자가 같은 계산에서 나옵니다.
 *
 * @param row 행의 첫 바이트
 * @param x 화소 위치. 짝수와 홀수 화소가 같은 U, V를 씁니다
 * @return 0~255로 자른 R, G, B
 */
inline Rgb8 UyvyPixelToRgb(const uint8_t* row, int32_t x) {
  const uint8_t* pair = row + static_cast<ptrdiff_t>(x / 2) * 4;
  const int32_t c = (x % 2 == 0 ? pair[1] : pair[3]) - 16;
  const int32_t d = pair[0] - 128;
  const int32_t e = pair[2] - 128;
  return {static_cast<uint8_t>(std::clamp((298 * c + 409 * e + 128) >> 8, 0, 255)),
          static_cast<uint8_t>(std::clamp((298 * c - 100 * d - 208 * e + 128) >> 8, 0, 255)),
          static_cast<uint8_t>(std::clamp((298 * c + 516 * d + 128) >> 8, 0, 255))};
}

}  // namespace hwlab::pixel

#endif  // HWLAB_PIXEL_UYVY_RGB_H_
