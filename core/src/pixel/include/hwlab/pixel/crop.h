#ifndef HWLAB_PIXEL_CROP_H_
#define HWLAB_PIXEL_CROP_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "hwlab/frame.h"
#include "hwlab/pixel/rect.h"

namespace hwlab::pixel {

/**
 * 영역을 행 사이 빈 바이트 없이 담을 때의 크기를 구합니다.
 *
 * @param format 화소 형식
 * @param rect 영역
 * @return 바이트 수. NV12는 Y 평면 뒤에 UV 평면이 붙은 크기. 영역이 비었으면 0
 */
size_t PackedBytes(PixelFormat format, const Rect& rect);

/**
 * 영역을 잘라 행 사이 빈 바이트 없이 복사합니다.
 *
 * @param frame 원본 프레임
 * @param rect 자를 영역. IsAligned를 통과해야 합니다
 * @param out 결과. 크기가 PackedBytes와 같아야 합니다
 * @return 복사했으면 true. 영역이 격자에 맞지 않거나 out 크기가 다르면 쓰지 않고 false
 * @note 색차 격자에 맞지 않는 UYVY 영역을 자르면 U와 V가 뒤바뀌므로 거부합니다.
 */
bool CopyRect(const FrameView& frame, const Rect& rect, std::span<uint8_t> out);

/**
 * 영역을 가운데 세로선에서 폭이 같은 두 영역으로 나눕니다. 두 발을 따로 저장하기 위한 것입니다.
 *
 * 가운데 선은 옮기지 않고 폭을 안쪽으로만 줄입니다. 사용자가 정한 영역 밖 화소가 저장 파일에 들어가지 않게 하기 위해서입니다.
 *
 * @param rect 나눌 영역. FitToGrid를 거친 값
 * @param format 화소 형식
 * @param width_step 두 영역의 폭이 맞춰야 할 배수. 색차 격자의 배수여야 합니다
 * @return 왼쪽과 오른쪽 영역. 나눌 수 없으면 둘 다 빈 영역
 */
std::pair<Rect, Rect> SplitLeftRight(const Rect& rect, PixelFormat format, int32_t width_step);

}  // namespace hwlab::pixel

#endif  // HWLAB_PIXEL_CROP_H_
