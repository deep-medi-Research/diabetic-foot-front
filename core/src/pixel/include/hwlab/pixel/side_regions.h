#ifndef HWLAB_PIXEL_SIDE_REGIONS_H_
#define HWLAB_PIXEL_SIDE_REGIONS_H_

#include <cstdint>
#include <vector>

#include "hwlab/frame.h"
#include "hwlab/pixel/rect.h"

namespace hwlab::pixel {

/**
 * 한 프레임 안에서 따로 저장할 좌우 영역 두 개입니다. 화면에서 사용자가 발 하나씩 정합니다.
 *
 * @note left와 right는 화소 기준(x가 작은 쪽이 left)이고 환자의 발과는 무관합니다. 환자의 어느 발인지는
 *       record::ShotInfo::pixel_left_foot가 정합니다.
 */
struct SideRegions {
  Rect left;   // 화소 왼쪽 영역
  Rect right;  // 화소 오른쪽 영역
  friend constexpr bool operator==(const SideRegions&, const SideRegions&) = default;
};

/** 좌우 영역 중 어느 쪽인지입니다. */
enum class Side : uint8_t {
  kLeft,   // 화소 왼쪽
  kRight,  // 화소 오른쪽
};

/** 좌우 영역 검사에서 찾은 문제입니다. */
enum class SideProblem : uint8_t {
  kNotAligned,   // 비었거나, 프레임 밖이거나, 색차 격자에 맞지 않음(IsAligned가 false)
  kNotPortrait,  // 높이가 폭보다 크지 않음. 관류 분석은 세로로 긴 영상만 받습니다
  kWrongOrder,   // 화소 왼쪽 영역의 가운데가 화소 오른쪽 영역의 가운데보다 왼쪽에 있지 않음
  kIntersecting, // 두 영역이 같은 화소를 포함함
};

/** 좌우 영역 검사에서 찾은 문제 하나입니다. */
struct SideFinding {
  SideProblem problem = SideProblem::kNotAligned;
  Side side = Side::kLeft;  // 문제가 있는 영역. 두 영역 사이의 문제(순서, 교차)는 kLeft와 kRight를 하나씩 넣습니다
  friend constexpr bool operator==(const SideFinding&, const SideFinding&) = default;
};

/**
 * 프레임 가운데 세로선에서 나눈 기본 좌우 영역을 만듭니다.
 *
 * 두 영역은 가운데 세로선에 맞닿고 폭은 max_width와 프레임 폭의 절반 중 작은 값을 색차 격자에 맞춰 내린 값입니다.
 * 높이는 프레임 높이 전체입니다.
 *
 * @param frame_width 프레임 폭
 * @param frame_height 프레임 높이
 * @param max_width 영역 하나의 최대 폭. 0 이하이면 프레임 폭의 절반을 씁니다
 * @param format 화소 형식
 * @return 좌우 영역. 프레임이 너무 작아 나눌 수 없으면 둘 다 빈 영역
 * @note 1920x1200 UYVY에 max_width 640이면 (320, 0, 640, 1200)과 (960, 0, 640, 1200)입니다.
 */
SideRegions DefaultSideRegions(int32_t frame_width, int32_t frame_height, int32_t max_width, PixelFormat format);

/**
 * 영역의 크기를 유지한 채 프레임 안으로 옮기고 색차 격자에 맞춥니다. 화면에서 영역을 끌어 옮길 때 씁니다.
 *
 * @param rect 옮길 영역
 * @param format 화소 형식
 * @param frame_width 프레임 폭
 * @param frame_height 프레임 높이
 * @return 프레임 안의 영역. 크기가 프레임보다 크면 프레임 크기로 줄이고, 시작과 크기는 격자의 배수로 내립니다.
 *         rect가 비었거나 프레임이 비면 빈 영역
 */
Rect ShiftInside(const Rect& rect, PixelFormat format, int32_t frame_width, int32_t frame_height);

/**
 * 좌우 영역을 녹화에 쓸 수 있는지 검사합니다.
 *
 * 검사 순서: 영역마다 격자 맞춤(kNotAligned)과 세로형(kNotPortrait), 그 다음 두 영역의 순서(kWrongOrder)와 교차(kIntersecting)입니다.
 *
 * @param regions 좌우 영역
 * @param format 화소 형식
 * @param frame_width 프레임 폭
 * @param frame_height 프레임 높이
 * @return 찾은 문제 목록. 비어 있으면 녹화에 쓸 수 있습니다
 */
std::vector<SideFinding> CheckSideRegions(const SideRegions& regions, PixelFormat format, int32_t frame_width,
                                          int32_t frame_height);

}  // namespace hwlab::pixel

#endif  // HWLAB_PIXEL_SIDE_REGIONS_H_
