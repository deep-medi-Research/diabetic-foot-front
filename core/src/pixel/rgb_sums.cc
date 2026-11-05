#include "hwlab/pixel/rgb_sums.h"

#include <cstddef>

#include "uyvy_rgb.h"

namespace hwlab::pixel {

RgbSums MeasureRgb(const FrameView& uyvy, const Rect& rect, int32_t step) {
  RgbSums out;
  if (uyvy.format != PixelFormat::kUyvy || uyvy.plane[0] == nullptr || step < 1 || IsEmpty(rect) || rect.x < 0 ||
      rect.y < 0 || rect.x + rect.width > uyvy.width || rect.y + rect.height > uyvy.height) {
    return out;
  }
  for (int32_t y = rect.y; y < rect.y + rect.height; y += step) {
    const uint8_t* row = uyvy.plane[0] + static_cast<ptrdiff_t>(y) * uyvy.row_stride[0];
    // 행 합계를 32비트로 모읍니다. 1920화소 x 255도 32비트 안이고, 수신 스레드에서 부르므로 64비트 덧셈을 줄입니다.
    int32_t sum[3] = {0, 0, 0};
    int32_t saturated[3] = {0, 0, 0};
    int32_t count = 0;
    for (int32_t x = rect.x; x < rect.x + rect.width; x += step) {
      const Rgb8 rgb = UyvyPixelToRgb(row, x);
      sum[0] += rgb.r;
      sum[1] += rgb.g;
      sum[2] += rgb.b;
      saturated[0] += rgb.r == 255 ? 1 : 0;
      saturated[1] += rgb.g == 255 ? 1 : 0;
      saturated[2] += rgb.b == 255 ? 1 : 0;
      ++count;
    }
    for (size_t c = 0; c < 3; ++c) {
      out.sum[c] += sum[c];
      out.saturated[c] += saturated[c];
    }
    out.pixel_count += count;
  }
  return out;
}

}  // namespace hwlab::pixel
