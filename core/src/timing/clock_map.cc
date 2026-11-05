#include "hwlab/timing/clock_map.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hwlab::timing {

namespace {

struct Point {
  double x = 0;
  double y = 0;
};

int64_t MaxResidualUs(const ClockMap& map, std::span<const ClockSample> samples) {
  int64_t max_residual = 0;
  for (const ClockSample& s : samples) {
    max_residual = std::max(max_residual, IntervalUs(ToCommonTime(map, s.device), s.host));
  }
  return max_residual;
}

// 기울기를 정할 근거가 부족한 입력은 시계 속도가 같다고 보고 가장 짧은 수신 지연에 맞춥니다.
ClockMap FitOffsetOnly(std::span<const ClockSample> samples) {
  ClockMap map;
  map.offset_us = std::numeric_limits<int64_t>::max();
  for (const ClockSample& s : samples) map.offset_us = std::min(map.offset_us, IntervalUs(s.device, s.host));
  map.max_residual_us = MaxResidualUs(map, samples);
  return map;
}

// 0 이하이면 o, a, b가 시계 방향이거나 한 직선 위라 a는 아래 껍질의 꼭짓점이 아닙니다.
double Cross(const Point& o, const Point& a, const Point& b) {
  return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

}  // namespace

ClockMap FitClockMap(std::span<const ClockSample> samples) {
  if (samples.empty()) return {};
  for (size_t i = 1; i < samples.size(); ++i) {
    if (samples[i].device < samples[i - 1].device) return FitOffsetOnly(samples);
  }
  // 표본 둘로 정한 기울기는 두 표본의 수신 지연 차이를 그대로 받아 실제 속도 차이(수백 ppm)보다 크게 틀립니다.
  if (samples.size() <= 2) return FitOffsetOnly(samples);

  // 첫 표본 기준의 차이로 계산해 큰 시각값(부팅 뒤 마이크로초)에서도 double 정밀도를 지킵니다.
  const Timestamp device0 = samples.front().device;
  const Timestamp host0 = samples.front().host;
  std::vector<Point> points;
  points.reserve(samples.size());
  for (const ClockSample& s : samples) {
    const Point p{static_cast<double>(IntervalUs(device0, s.device)), static_cast<double>(IntervalUs(host0, s.host))};
    if (!points.empty() && points.back().x == p.x) {
      points.back().y = std::min(points.back().y, p.y);
      continue;
    }
    points.push_back(p);
  }
  if (points.size() < 2) return FitOffsetOnly(samples);

  std::vector<Point> hull;
  for (const Point& p : points) {
    while (hull.size() >= 2 && Cross(hull[hull.size() - 2], hull.back(), p) <= 0) hull.pop_back();
    hull.push_back(p);
  }

  double mean_x = 0;
  for (const Point& p : points) mean_x += p.x / static_cast<double>(points.size());
  size_t edge = 0;
  while (edge + 2 < hull.size() && hull[edge + 1].x < mean_x) ++edge;
  const Point& a = hull[edge];
  const Point& b = hull[edge + 1];
  const double rate = (b.y - a.y) / (b.x - a.x);
  if (!(rate > 0)) return FitOffsetOnly(samples);

  ClockMap map;
  map.rate = rate;
  const double intercept = a.y - rate * a.x;
  map.offset_us = static_cast<int64_t>(
      std::llround(static_cast<double>(host0.us) + intercept - rate * static_cast<double>(device0.us)));
  map.max_residual_us = MaxResidualUs(map, samples);
  return map;
}

Timestamp ToCommonTime(const ClockMap& map, Timestamp device) {
  return {map.offset_us + static_cast<int64_t>(std::llround(map.rate * static_cast<double>(device.us)))};
}

std::vector<ClockSegment> FitClockSegments(std::span<const ClockSample> samples, int64_t gap_us) {
  std::vector<ClockSegment> segments;
  if (samples.empty()) return segments;
  std::vector<size_t> starts = {0};
  for (size_t i = 1; i < samples.size(); ++i) {
    if (samples[i].device < samples[i - 1].device) {
      const auto last = std::max_element(samples.begin(), samples.end(),
                                         [](const ClockSample& a, const ClockSample& b) { return a.device < b.device; });
      segments.push_back({last->device, FitClockMap(samples)});
      return segments;
    }
    const int64_t extra_us =
        IntervalUs(samples[i - 1].host, samples[i].host) - IntervalUs(samples[i - 1].device, samples[i].device);
    if (gap_us > 0 && extra_us > gap_us) starts.push_back(i);
  }
  starts.push_back(samples.size());

  const auto part = [&](size_t k) { return samples.subspan(starts[k], starts[k + 1] - starts[k]); };
  size_t longest = 0;
  for (size_t k = 1; k + 1 < starts.size(); ++k) {
    const auto a = part(k);
    const auto b = part(longest);
    if (IntervalUs(a.front().device, a.back().device) > IntervalUs(b.front().device, b.back().device)) longest = k;
  }
  // 빈틈 앞 구간은 표본이 한두 개라 기울기를 정할 수 없으므로 가장 긴 구간의 속도 비를 함께 씁니다.
  const ClockMap fitted = FitClockMap(part(longest));
  for (size_t k = 0; k + 1 < starts.size(); ++k) {
    const auto p = part(k);
    ClockMap map = fitted;
    if (k != longest) {
      map.offset_us = std::numeric_limits<int64_t>::max();
      for (const ClockSample& s : p) {
        map.offset_us = std::min(map.offset_us, IntervalUs(ToCommonTime(ClockMap{0, map.rate, 0}, s.device), s.host));
      }
      map.max_residual_us = MaxResidualUs(map, p);
    }
    segments.push_back({p.back().device, map});
  }
  return segments;
}

Timestamp ToCommonTime(std::span<const ClockSegment> segments, Timestamp device) {
  if (segments.empty()) return device;
  for (const ClockSegment& s : segments) {
    if (device <= s.last_device) return ToCommonTime(s.map, device);
  }
  return ToCommonTime(segments.back().map, device);
}

std::vector<size_t> NearestFrames(std::span<const Timestamp> reference, std::span<const Timestamp> other) {
  std::vector<size_t> nearest;
  if (other.empty()) return nearest;
  nearest.reserve(reference.size());
  const auto distance = [](Timestamp a, Timestamp b) { return a < b ? IntervalUs(a, b) : IntervalUs(b, a); };
  // 두 목록이 오름차순이면 가장 가까운 위치도 앞으로만 움직이므로 한 번 훑어 찾습니다.
  size_t j = 0;
  for (const Timestamp r : reference) {
    while (j + 1 < other.size() && distance(other[j + 1], r) < distance(other[j], r)) ++j;
    nearest.push_back(j);
  }
  return nearest;
}

}  // namespace hwlab::timing
