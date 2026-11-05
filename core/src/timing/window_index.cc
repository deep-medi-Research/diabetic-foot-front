#include "hwlab/timing/window_index.h"

namespace hwlab::timing {

namespace {

// 음수에서도 아래로 내림하는 정수 나눗셈입니다.
int64_t FloorDiv(int64_t a, int64_t b) {
  const int64_t q = a / b;
  return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

}  // namespace

std::optional<int32_t> WindowIndexAt(int64_t t_us, int32_t count, int64_t window_us, int64_t stride_us) {
  if (count == 1) return 0;
  if (count < 1 || window_us <= 0 || stride_us <= 0) return std::nullopt;
  // 창 길이가 홀수 마이크로초여도 가운데를 정수로 다루려고 모든 값을 두 배로 셉니다.
  const int64_t d = 2 * t_us - window_us;
  const int64_t s = 2 * stride_us;
  int64_t k = FloorDiv(d, s);
  if (d - k * s > (k + 1) * s - d) ++k;
  if (k < 0) k = 0;
  if (k > count - 1) k = count - 1;
  return static_cast<int32_t>(k);
}

}  // namespace hwlab::timing
