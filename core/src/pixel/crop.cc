#include "hwlab/pixel/crop.h"

#include <algorithm>
#include <cstring>

namespace hwlab::pixel {

namespace {

int32_t BytesPerPixel(PixelFormat format) {
  switch (format) {
    case PixelFormat::kUyvy:
    case PixelFormat::kY16:
      return 2;
    case PixelFormat::kNv12:
      return 1;
  }
  return 0;
}

void CopyRows(const uint8_t* source, int32_t source_stride, int32_t first_row, size_t byte_offset,
              int32_t rows, size_t row_bytes, uint8_t* out) {
  for (int32_t r = 0; r < rows; ++r) {
    const uint8_t* from =
        source + static_cast<ptrdiff_t>(first_row + r) * source_stride + byte_offset;
    std::memcpy(out + static_cast<size_t>(r) * row_bytes, from, row_bytes);
  }
}

}  // namespace

size_t PackedBytes(PixelFormat format, const Rect& rect) {
  if (IsEmpty(rect)) {
    return 0;
  }
  const size_t pixels = static_cast<size_t>(rect.width) * static_cast<size_t>(rect.height);
  if (format == PixelFormat::kNv12) {
    return pixels + pixels / 2;
  }
  return pixels * static_cast<size_t>(BytesPerPixel(format));
}

bool CopyRect(const FrameView& frame, const Rect& rect, std::span<uint8_t> out) {
  if (frame.plane[0] == nullptr || !IsAligned(rect, frame.format, frame.width, frame.height) ||
      out.size() != PackedBytes(frame.format, rect)) {
    return false;
  }
  const size_t row_bytes = static_cast<size_t>(rect.width) * BytesPerPixel(frame.format);
  const size_t x_bytes = static_cast<size_t>(rect.x) * BytesPerPixel(frame.format);
  if (frame.format != PixelFormat::kNv12) {
    CopyRows(frame.plane[0], frame.row_stride[0], rect.y, x_bytes, rect.height, row_bytes,
             out.data());
    return true;
  }
  if (frame.plane[1] == nullptr) {
    return false;
  }
  CopyRows(frame.plane[0], frame.row_stride[0], rect.y, x_bytes, rect.height, row_bytes,
           out.data());
  // UV 평면은 2x2 화소마다 U, V 한 쌍이라 행 수는 절반이고 한 행의 바이트 수는 Y와 같습니다.
  CopyRows(frame.plane[1], frame.row_stride[1], rect.y / 2, x_bytes, rect.height / 2, row_bytes,
           out.data() + row_bytes * static_cast<size_t>(rect.height));
  return true;
}

std::pair<Rect, Rect> SplitLeftRight(const Rect& rect, PixelFormat format, int32_t width_step) {
  const ChromaGrid grid = ChromaGridOf(format);
  if (IsEmpty(rect) || width_step <= 0 || width_step % grid.x != 0) {
    return {};
  }
  // 가운데 선이 두 발의 경계라, 폭을 줄일 때도 가운데 선은 옮기지 않습니다.
  const int32_t middle = (rect.x + rect.width / 2) / grid.x * grid.x;
  const int32_t room = std::min(middle - rect.x, rect.x + rect.width - middle);
  const int32_t half_width = room / width_step * width_step;
  if (half_width <= 0) {
    return {};
  }
  return {Rect{middle - half_width, rect.y, half_width, rect.height},
          Rect{middle, rect.y, half_width, rect.height}};
}

}  // namespace hwlab::pixel
