#ifndef HWLAB_TIMING_WINDOW_INDEX_H_
#define HWLAB_TIMING_WINDOW_INDEX_H_

#include <cstdint>
#include <optional>

namespace hwlab::timing {

/**
 * 녹화 시작에서 t_us만큼 지난 시각에 보여 줄 분석 창의 번호를 고릅니다.
 *
 * 창 k는 k * stride_us에서 시작해 window_us 동안이고, 그 가운데 시각은 k * stride_us + window_us / 2입니다.
 * 가운데 시각이 t_us에 가장 가까운 창을 고르며, 거리가 같으면 앞 창을 고릅니다. 결과는 0 ~ count - 1로 제한합니다.
 *
 * @param t_us 녹화 첫 프레임에서 지난 시간(마이크로초). 음수이면 창 0입니다
 * @param count 창 수
 * @param window_us 창 길이(마이크로초)
 * @param stride_us 창 시작 간격(마이크로초)
 * @return 창 번호. count가 1이면 항상 0입니다. count가 1보다 작거나 window_us나 stride_us가 0 이하이면 nullopt
 * @note 관류 분석 프로그램은 창마다 관류 지도 GIF를 한 장 만들고 그 장의 시각으로 창의 가운데 시각을 적습니다
 *       (창 10초, 간격 2초, 30초 녹화에서 창 11개, 가운데 5초 ~ 25초).
 */
std::optional<int32_t> WindowIndexAt(int64_t t_us, int32_t count, int64_t window_us, int64_t stride_us);

}  // namespace hwlab::timing

#endif  // HWLAB_TIMING_WINDOW_INDEX_H_
