#ifndef HWLAB_FRAME_H_
#define HWLAB_FRAME_H_

#include <cstdint>

#include "hwlab/stream.h"
#include "hwlab/timestamp.h"

namespace hwlab {

/** 프레임 화소의 형식입니다. */
enum class PixelFormat : uint8_t {
  kUyvy,  // RGB 카메라 무압축. 화소 2개가 4바이트(U Y0 V Y1)
  kNv12,  // Y 평면 뒤에 UV 평면
  kY16,   // 열화상 원시값. 화소 하나가 2바이트(리틀 엔디언)
};

/**
 * 프레임 한 장의 화소와 시각을 가리킵니다.
 *
 * 화소를 소유하지 않습니다. plane과 row_stride는 형식에 따라 씁니다.
 * UYVY와 Y16은 plane[0]만, NV12는 plane[0](Y)과 plane[1](UV)을 씁니다.
 *
 * @note 평면 포인터는 이 뷰를 받은 함수가 반환할 때까지만 유효합니다. 더 오래 쓰려면 복사해야 합니다.
 */
struct FrameView {
  PixelFormat format = PixelFormat::kUyvy;
  int32_t width = 0;
  int32_t height = 0;
  const uint8_t* plane[2] = {nullptr, nullptr};
  int32_t row_stride[2] = {0, 0};
  StreamKind stream = StreamKind::kRgb;
  FrameIndex index = 0;
  Timestamp time;
};

}  // namespace hwlab

#endif  // HWLAB_FRAME_H_
