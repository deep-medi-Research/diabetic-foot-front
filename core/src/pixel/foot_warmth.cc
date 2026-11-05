#include "hwlab/pixel/foot_warmth.h"

#include <algorithm>

#include "hwlab/pixel/convert.h"

namespace hwlab::pixel {

int32_t ThermalContrast(const FrameView& y16, const Rect& rect) {
  const Y16Range range = FindPercentileRange(y16, rect, 250, 900);
  return static_cast<int32_t>(range.high) - static_cast<int32_t>(range.low);
}

void FootWarmthCheck::Reset() { recent_.clear(); }

void FootWarmthCheck::Push(const std::array<int32_t, 2>& contrasts) {
  recent_.push_back(contrasts);
  while (recent_.size() > static_cast<size_t>(kWarmFrames)) recent_.pop_front();
}

Warmth FootWarmthCheck::State(int side) const {
  if (recent_.size() < static_cast<size_t>(kWarmFrames)) return Warmth::kChecking;
  return Lowest(side) >= kWarmContrastMin ? Warmth::kWarm : Warmth::kCold;
}

int32_t FootWarmthCheck::Lowest(int side) const {
  if (recent_.empty() || side < 0 || side > 1) return 0;
  int32_t lowest = recent_.front()[static_cast<size_t>(side)];
  for (const auto& c : recent_) lowest = std::min(lowest, c[static_cast<size_t>(side)]);
  return lowest;
}

}  // namespace hwlab::pixel
