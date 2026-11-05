#ifndef HWLAB_PIXEL_RECT_H_
#define HWLAB_PIXEL_RECT_H_

#include <cstdint>

#include "hwlab/frame.h"

namespace hwlab::pixel {

/** 프레임 안의 사각 영역입니다. 화소 단위이고 좌상단이 원점입니다. */
struct Rect {
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;

  friend constexpr bool operator==(const Rect& a, const Rect& b) = default;
};

/** 폭이나 높이가 0 이하이면 true입니다. */
constexpr bool IsEmpty(const Rect& rect) { return rect.width <= 0 || rect.height <= 0; }

/**
 * 영역이 세로로 긴지(높이가 폭보다 큰지) 판정합니다.
 *
 * @param rect 영역
 * @return 비어 있지 않고 높이가 폭보다 크면 true
 * @note 관류 분석은 발 하나씩 세로로 긴 영상만 받습니다. 좌우 분할 녹화 전에 두 영역을 이 함수로 확인합니다.
 */
constexpr bool IsPortrait(const Rect& rect) { return !IsEmpty(rect) && rect.height > rect.width; }

/** 색차를 공유하는 화소 묶음의 크기입니다. 영역의 시작과 크기가 이 배수여야 성분이 밀리지 않습니다. */
struct ChromaGrid {
  int32_t x = 1;
  int32_t y = 1;
};

/**
 * 화소 형식의 색차 격자를 구합니다.
 *
 * @param format 화소 형식
 * @return UYVY는 가로 2, NV12는 가로 2 세로 2, Y16은 1
 */
ChromaGrid ChromaGridOf(PixelFormat format);

/**
 * 영역을 프레임 안으로 줄이고 색차 격자에 맞춥니다.
 *
 * 시작은 올리고 끝은 내려서 원래 영역보다 커지지 않습니다.
 *
 * @param rect 원래 영역
 * @param format 화소 형식
 * @param frame_width 프레임 폭
 * @param frame_height 프레임 높이
 * @return 맞춘 영역. 남는 것이 없으면 빈 영역
 */
Rect FitToGrid(const Rect& rect, PixelFormat format, int32_t frame_width, int32_t frame_height);

/**
 * 영역이 프레임 안에 있고 색차 격자에 맞는지 판정합니다.
 *
 * @return 그대로 자를 수 있으면 true. 빈 영역이면 false
 */
bool IsAligned(const Rect& rect, PixelFormat format, int32_t frame_width, int32_t frame_height);

}  // namespace hwlab::pixel

#endif  // HWLAB_PIXEL_RECT_H_
