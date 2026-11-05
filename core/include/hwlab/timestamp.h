#ifndef HWLAB_TIMESTAMP_H_
#define HWLAB_TIMESTAMP_H_

#include <compare>
#include <cstdint>

namespace hwlab {

/**
 * 장비 또는 드라이버가 프레임에 붙인 시각(마이크로초)입니다.
 *
 * @note 기준점은 스트림을 연 platform이 정합니다. 서로 다른 스트림의 시각을 비교하려면 같은 기준점이어야 합니다.
 */
struct Timestamp {
  int64_t us = 0;

  friend constexpr bool operator==(Timestamp a, Timestamp b) = default;
  friend constexpr auto operator<=>(Timestamp a, Timestamp b) = default;
};

/**
 * 두 시각 사이의 간격을 구합니다.
 *
 * @param from 앞 시각
 * @param to 뒤 시각
 * @return to에서 from을 뺀 마이크로초. to가 from보다 이르면 음수
 */
constexpr int64_t IntervalUs(Timestamp from, Timestamp to) {
  return to.us - from.us;
}

}  // namespace hwlab

#endif  // HWLAB_TIMESTAMP_H_
