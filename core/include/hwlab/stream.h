#ifndef HWLAB_STREAM_H_
#define HWLAB_STREAM_H_

#include <cstdint>

namespace hwlab {

/** 프레임을 내는 장비의 종류입니다. 모든 프레임에 붙어 RGB와 열화상을 시각으로 맞추는 데 씁니다. */
enum class StreamKind : uint8_t {
  kRgb,
  kThermal,
};

/**
 * 스트림을 연 뒤 받은 순서입니다.
 *
 * @note 카메라가 빠뜨린 프레임에는 번호가 붙지 않으므로 번호로는 카메라 단 누락을 알 수 없습니다.
 *       누락은 프레임 시각으로 판정합니다(timing::FindFrameGaps).
 */
using FrameIndex = uint64_t;

}  // namespace hwlab

#endif  // HWLAB_STREAM_H_
