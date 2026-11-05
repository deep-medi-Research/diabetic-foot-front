#ifndef HWLAB_PLATFORM_TEMPLATE_MAP_H_
#define HWLAB_PLATFORM_TEMPLATE_MAP_H_

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "hwlab/platform/perfusion_detail.h"

namespace hwlab::platform {

/** 결과 창이 그리는 RGBA 영상입니다. 한 화소는 4바이트이고 줄 사이 여백이 없습니다. 투명 화소는 발 틀 밖입니다. */
struct RgbaImage {
  int32_t width = 0;
  int32_t height = 0;
  std::vector<uint8_t> rgba;
};

/**
 * PI 값을 색으로 바꿉니다. 관류 분석 프로그램 그림(`_pi_map.png`)과 같은 turbo 색표입니다.
 *
 * @param value PI(%)
 * @param top 색표 위끝 PI(%). 0과 top 사이를 파랑에서 빨강으로 칠합니다
 * @return {R, G, B}. value나 top이 유한하지 않거나 top이 0 이하면 회색입니다
 */
std::array<uint8_t, 3> TurboColor(double value, double top);

/**
 * 표준 발 틀 PI 지도의 색표 위끝입니다. 모든 시간 창과 두 발의 계산 칸 값 중 95번째 백분위수입니다.
 *
 * @param detail 결과 배열(HasTemplateMap이 true)
 * @return PI(%). 값이 없으면 nullopt
 * @note 녹화 하나 안에서 시간 창이 바뀌어도 같은 색이 같은 값이 되도록 시간 창마다 따로 정하지 않습니다.
 */
std::optional<double> TemplateMapTop(const PerfusionDetail& detail);

/**
 * 시간 창 하나의 표준 발 틀 PI 지도를 그립니다. 왼발을 왼쪽에, 오른발을 좌우반전해 오른쪽에 둡니다.
 *
 * @param detail 결과 배열(HasTemplateMap이 true)
 * @param window 시간 창 번호(0부터)
 * @param foot_height 발 그림 높이(화소). 발 길이가 이 높이입니다
 * @param top 색표 위끝 PI(%)(TemplateMapTop)
 * @return 그림. 틀 지도 값이 없거나 창 번호가 범위 밖이거나 foot_height가 2 미만이면 nullopt
 * @note 칸 사이 값은 이웃 계산 칸에서 쌍선형 보간합니다. 계산하지 않는 칸(발가락 사이 등)은 어두운 회색입니다.
 *       관류 분석 프로그램 그림(`_pi_map.png`)과 같은 배치입니다.
 */
std::optional<RgbaImage> DrawTemplateMap(const PerfusionDetail& detail, int32_t window, int32_t foot_height, double top);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_TEMPLATE_MAP_H_
