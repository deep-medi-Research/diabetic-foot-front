#ifndef HWLAB_PIXEL_CONVERT_H_
#define HWLAB_PIXEL_CONVERT_H_

#include <cstdint>
#include <span>

#include "hwlab/frame.h"
#include "hwlab/pixel/rect.h"

namespace hwlab::pixel {

/** 열화상 원시값의 범위입니다. 온도가 아니라 센서 원시값입니다. */
struct Y16Range {
  uint16_t low = 0;
  uint16_t high = 0;
};

/**
 * 영역 안 원시값의 최솟값과 최댓값을 찾습니다.
 *
 * @param y16 Y16 형식 프레임
 * @param rect 찾을 영역
 * @return 최솟값과 최댓값. 영역이 비었거나 형식이 Y16이 아니면 {0, 0}
 */
Y16Range FindRange(const FrameView& y16, const Rect& rect);

/**
 * 열화상 원시값을 8비트 회색으로 폅니다.
 *
 * [low, high] 구간을 0~255에 맞추고 구간 밖 값은 양 끝으로 자릅니다. 구간이 한 값이면 모두 128입니다.
 *
 * @param y16 Y16 형식 프레임
 * @param range 펼 구간
 * @param out 결과. width * height 바이트
 * @return 썼으면 true. 형식이 Y16이 아니거나 out 크기가 다르면 쓰지 않고 false
 * @note 사람이 보는 확인용 영상에만 씁니다. 분석에는 원시값(raw16)을 씁니다.
 */
bool Y16ToGray8(const FrameView& y16, Y16Range range, std::span<uint8_t> out);

/**
 * 회색 영상을 색차가 없는 UYVY로 바꿉니다(U와 V는 128). 열화상 확인용 영상을 영상 파일로 쓸 때 씁니다.
 *
 * @param gray 회색 화소. width * height 바이트
 * @param width 폭. 짝수
 * @param height 높이
 * @param out 결과. width * height * 2 바이트
 * @return 썼으면 true. 폭이 홀수이거나 크기가 맞지 않으면 false
 */
bool Gray8ToUyvy(std::span<const uint8_t> gray, int32_t width, int32_t height, std::span<uint8_t> out);

/**
 * UYVY를 Y, U, V 평면(4:2:2 planar)으로 옮깁니다. FFV1 인코더가 평면 형식을 받기 때문입니다.
 *
 * 바이트 위치만 바뀌어 무손실이고, 되돌리면 원래 바이트와 같습니다.
 *
 * @param uyvy UYVY 형식 프레임. 폭은 짝수
 * @param y Y 평면. 행마다 width 바이트
 * @param y_stride Y 평면의 행 간격
 * @param u U 평면. 행마다 width / 2 바이트
 * @param u_stride U 평면의 행 간격
 * @param v V 평면. 행마다 width / 2 바이트
 * @param v_stride V 평면의 행 간격
 * @return 옮겼으면 true. 형식이 UYVY가 아니거나 폭이 홀수이거나 평면이 없으면 false
 */
bool UyvyToPlanar422(const FrameView& uyvy, uint8_t* y, int32_t y_stride, uint8_t* u, int32_t u_stride, uint8_t* v,
                     int32_t v_stride);

/**
 * 미리보기용 RGB 24비트 영상을 만듭니다(BT.601 제한 범위).
 *
 * @param uyvy UYVY 형식 프레임
 * @param step 가로세로 step 화소마다 하나씩 고릅니다. 1920x1200에서 4이면 480x300
 * @param out 결과. (width / step) * (height / step) * 3 바이트
 * @return 썼으면 true. 형식이 UYVY가 아니거나 out 크기가 다르면 false
 * @note 화면에만 씁니다. 분석이나 저장에는 쓰지 않습니다.
 */
bool UyvyToRgbPreview(const FrameView& uyvy, int32_t step, std::span<uint8_t> out);

/**
 * 열화상 미리보기의 색상표입니다. 화면 표시에만 씁니다.
 *
 * @note 색상표마다 256단계 표가 하나씩 있고, 기준점 사이는 선형 보간합니다. 기준점과 출처는 convert.cc에 있습니다.
 */
enum class Palette : uint8_t {
  kGray,          ///< 회색. R, G, B가 모두 Y16ToGray8의 결과와 같습니다
  kIron,          ///< 검정, 보라, 빨강, 주황, 노랑, 흰색 순서입니다. 낮은 값이 검정, 높은 값이 흰색입니다
  kRainbow,       ///< 남색, 파랑, 청록, 노랑, 빨강, 검붉은색 순서입니다(MATLAB jet와 같은 기준점)
  kHighContrast,  ///< 검정, 파랑, 청록, 초록, 노랑, 빨강, 자홍, 흰색 순서입니다. 한 단계당 색 변화가 가장 큽니다
};

/**
 * 화면에서 영상을 누를 때 다음으로 쓸 색상표입니다.
 *
 * @param palette 지금 색상표
 * @return 아이언, 흑백, 레인보우, 고대비 순서의 다음 색상표. 고대비 다음은 아이언입니다
 */
Palette NextPalette(Palette palette);

/** 한 화소의 색입니다. */
struct Rgb8 {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;

  friend bool operator==(const Rgb8&, const Rgb8&) = default;
};

/**
 * 색상표에서 한 단계의 색을 고릅니다. Y16ToRgbPreview가 쓰는 것과 같은 표입니다.
 *
 * @param palette 색상표
 * @param level 0~255 단계. 0이 표시 구간의 아래 끝, 255가 위 끝입니다
 * @return 그 단계의 색
 */
Rgb8 PaletteColor(Palette palette, uint8_t level);

/**
 * 영역 안 원시값에서 아래쪽과 위쪽 일부 화소를 뺀 범위를 찾습니다.
 *
 * 영역의 화소를 값 순서로 세운 뒤 순위 round((n - 1) * permille / 1000)의 값을 고릅니다(0부터 셈).
 * low_permille 0과 high_permille 1000이면 FindRange와 같습니다.
 * 1000화소에서 high_permille 999는 가장 큰 1화소를, 995는 가장 큰 5화소를 뺍니다.
 *
 * @param y16 Y16 형식 프레임
 * @param rect 찾을 영역
 * @param low_permille 아래 끝의 천분위. 0~1000
 * @param high_permille 위 끝의 천분위. low_permille~1000
 * @return 고른 두 값. 영역이 비었거나 형식이 Y16이 아니거나 천분위가 범위 밖이면 {0, 0}
 * @note 결함 화소나 화면 안 고온 물체 몇 개가 표시 범위를 넓혀 발 영역의 대비가 낮아지는 것을 막기 위한 것입니다.
 *       화면 표시용이고 분석에는 쓰지 않습니다.
 */
Y16Range FindPercentileRange(const FrameView& y16, const Rect& rect, int32_t low_permille, int32_t high_permille);

/**
 * 열화상 원시값을 색상표로 칠한 미리보기용 RGB 24비트 영상을 만듭니다.
 *
 * 각 값을 Y16ToGray8과 같은 계산으로 0~255 단계로 바꾼 뒤 색상표에서 색을 고릅니다.
 * 바이트 순서는 R, G, B입니다.
 *
 * @param y16 Y16 형식 프레임
 * @param range 펼 구간. 구간 밖 값은 양 끝 색이고, 구간이 한 값이면 모두 128단계 색입니다
 * @param palette 색상표
 * @param step 가로세로 step 화소마다 하나씩 고릅니다. 640x480에서 2이면 320x240
 * @param out 결과. (width / step) * (height / step) * 3 바이트
 * @return 썼으면 true. 형식이 Y16이 아니거나 step이 1보다 작거나 out 크기가 다르면 쓰지 않고 false
 * @note 화면에만 씁니다. 색은 표시 구간에 따라 달라져 온도를 나타내지 않으므로 분석이나 저장에는 원시값(raw16)을 씁니다.
 */
bool Y16ToRgbPreview(const FrameView& y16, Y16Range range, Palette palette, int32_t step, std::span<uint8_t> out);

}  // namespace hwlab::pixel

#endif  // HWLAB_PIXEL_CONVERT_H_
