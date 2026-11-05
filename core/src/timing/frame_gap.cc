#include "hwlab/timing/frame_gap.h"

#include <algorithm>

namespace hwlab::timing {

std::vector<FrameGap> FindFrameGaps(std::span<const Timestamp> times, int64_t period_us) {
  std::vector<FrameGap> gaps;
  if (period_us <= 0) {
    return gaps;
  }
  for (size_t i = 1; i < times.size(); ++i) {
    const int64_t interval_us = IntervalUs(times[i - 1], times[i]);
    // 한 장이 빠지면 간격이 주기의 2배가 되므로, 1.5배 이하의 흔들림은 누락으로 세지 않습니다.
    if (interval_us * 2 > period_us * 3) {
      const int64_t periods = (interval_us + period_us / 2) / period_us;
      gaps.push_back({i - 1, interval_us, periods - 1});
    }
  }
  return gaps;
}

int64_t MedianIntervalUs(std::span<const Timestamp> times) {
  if (times.size() < 2) return 0;
  std::vector<int64_t> intervals;
  intervals.reserve(times.size() - 1);
  for (size_t i = 1; i < times.size(); ++i) intervals.push_back(IntervalUs(times[i - 1], times[i]));
  const auto middle = intervals.begin() + static_cast<ptrdiff_t>(intervals.size() / 2);
  std::nth_element(intervals.begin(), middle, intervals.end());
  return *middle;
}

bool MatchesPeriod(int64_t median_interval_us, int64_t period_us, int32_t tolerance_percent) {
  if (median_interval_us <= 0 || period_us <= 0) return false;
  const int64_t diff = median_interval_us > period_us ? median_interval_us - period_us : period_us - median_interval_us;
  return diff * 100 <= period_us * tolerance_percent;
}

FrameRateCheck CheckFrameRate(std::span<const Timestamp> times, int64_t period_us) {
  FrameRateCheck out;
  if (period_us <= 0 || times.size() < 2) return out;
  const int64_t origin = times.front().us;
  constexpr int64_t kMergeUs = 1'000'000;
  for (size_t i = 1; i < times.size(); ++i) {
    const int64_t interval_us = IntervalUs(times[i - 1], times[i]);
    // 1.2배 초과이면서 1.5배 이하만 셉니다. 1.5배를 넘으면 누락(FindFrameGaps)입니다.
    if (interval_us * 5 <= period_us * 6 || interval_us * 2 > period_us * 3) continue;
    ++out.slow_intervals;
    const int64_t start = times[i - 1].us - origin;
    const int64_t end = times[i].us - origin;
    if (!out.spans.empty() && start - out.spans.back().end_us <= kMergeUs) {
      out.spans.back().end_us = end;
      ++out.spans.back().intervals;
    } else {
      out.spans.push_back({start, end, 1});
    }
  }
  // 1초 창: 주기로 1초에 들어가는 간격 수만큼의 프레임을 묶어 그 사이 시간으로 fps를 잽니다.
  const size_t window = std::max<size_t>(1, static_cast<size_t>((1'000'000 + period_us / 2) / period_us));
  const size_t span = std::min(window, times.size() - 1);
  double lowest = 0;
  for (size_t k = 0; k + span < times.size(); ++k) {
    const int64_t elapsed = IntervalUs(times[k], times[k + span]);
    if (elapsed <= 0) continue;
    const double fps = static_cast<double>(span) * 1e6 / static_cast<double>(elapsed);
    if (lowest == 0 || fps < lowest) lowest = fps;
  }
  out.min_fps = lowest;
  return out;
}

int64_t CountMissingFrames(std::span<const FrameGap> gaps) {
  int64_t missing = 0;
  for (const FrameGap& gap : gaps) {
    missing += gap.missing_frames;
  }
  return missing;
}

}  // namespace hwlab::timing
