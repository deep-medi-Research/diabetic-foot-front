#include "hwlab/pixel/rect.h"

#include <algorithm>

namespace hwlab::pixel {

namespace {

int32_t RoundUp(int32_t value, int32_t grid) { return (value + grid - 1) / grid * grid; }

int32_t RoundDown(int32_t value, int32_t grid) { return value / grid * grid; }

}  // namespace

ChromaGrid ChromaGridOf(PixelFormat format) {
  switch (format) {
    case PixelFormat::kUyvy:
      return {2, 1};
    case PixelFormat::kNv12:
      return {2, 2};
    case PixelFormat::kY16:
      return {1, 1};
  }
  return {1, 1};
}

Rect FitToGrid(const Rect& rect, PixelFormat format, int32_t frame_width, int32_t frame_height) {
  if (IsEmpty(rect) || frame_width <= 0 || frame_height <= 0) {
    return {};
  }
  const ChromaGrid grid = ChromaGridOf(format);
  // 프레임 경계로 먼저 자른 뒤 시작은 올리고 끝은 내려, 원래 영역 밖의 화소가 들어오지 않게 합니다.
  const int32_t left = RoundUp(std::clamp(rect.x, 0, frame_width), grid.x);
  const int32_t top = RoundUp(std::clamp(rect.y, 0, frame_height), grid.y);
  const int32_t right = RoundDown(std::clamp(rect.x + rect.width, 0, frame_width), grid.x);
  const int32_t bottom = RoundDown(std::clamp(rect.y + rect.height, 0, frame_height), grid.y);
  if (right <= left || bottom <= top) {
    return {};
  }
  return {left, top, right - left, bottom - top};
}

bool IsAligned(const Rect& rect, PixelFormat format, int32_t frame_width, int32_t frame_height) {
  return !IsEmpty(rect) && FitToGrid(rect, format, frame_width, frame_height) == rect;
}

}  // namespace hwlab::pixel
