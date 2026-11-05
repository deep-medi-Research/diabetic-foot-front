#include "hwlab/pixel/brightness.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace hwlab::pixel {

namespace {

bool IsInside(const Rect& rect, const FrameView& frame) {
  return !IsEmpty(rect) && rect.x >= 0 && rect.y >= 0 && rect.x + rect.width <= frame.width &&
         rect.y + rect.height <= frame.height;
}

// 화소 하나의 밝기를 읽는 위치입니다. 형식 분기를 행 루프 밖에 두어 녹화 중 30fps 쓰기 스레드의 부담을 줄입니다.
template <typename Read>
void Accumulate(const FrameView& frame, const Rect& rect, int32_t clip_level, Read read,
                Brightness* out) {
  for (int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    const uint8_t* row = frame.plane[0] + static_cast<ptrdiff_t>(y) * frame.row_stride[0];
    int64_t sum = 0;
    int64_t clipped = 0;
    for (int32_t x = rect.x; x < rect.x + rect.width; ++x) {
      const int32_t value = read(row, x);
      sum += value;
      clipped += value >= clip_level ? 1 : 0;
    }
    out->sum += sum;
    out->clipped_pixels += clipped;
  }
}

// 주파수 k(전체 길이에 들어가는 반복 횟수, 소수 허용) 성분의 세기 |X(k)|^2입니다.
double PowerAt(const std::vector<double>& values, double k) {
  const double step = 2.0 * std::numbers::pi * k / static_cast<double>(values.size());
  double re = 0.0;
  double im = 0.0;
  for (size_t i = 0; i < values.size(); ++i) {
    const double phase = step * static_cast<double>(i);
    re += values[i] * std::cos(phase);
    im -= values[i] * std::sin(phase);
  }
  return re * re + im * im;
}

// 가장 센 성분의 k를 [low, high] 안에서 간격 step으로 찾습니다.
double FindPeak(const std::vector<double>& values, double low, double high, double step, double* power) {
  double best_k = low;
  double best = -1.0;
  for (double k = low; k <= high + step * 0.5; k += step) {
    const double p = PowerAt(values, k);
    if (p > best) {
      best = p;
      best_k = k;
    }
  }
  *power = best;
  return best_k;
}

}  // namespace

Brightness MeasureBrightness(const FrameView& frame, const Rect& rect, int32_t clip_level) {
  Brightness brightness;
  if (frame.plane[0] == nullptr || !IsInside(rect, frame)) {
    return brightness;
  }
  switch (frame.format) {
    case PixelFormat::kUyvy:
      // 화소 쌍 4바이트가 U Y0 V Y1 순서라 Y는 화소마다 2바이트 간격, 1바이트 뒤에 있습니다.
      Accumulate(frame, rect, clip_level,
                 [](const uint8_t* row, int32_t x) { return int32_t{row[static_cast<ptrdiff_t>(x) * 2 + 1]}; },
                 &brightness);
      break;
    case PixelFormat::kNv12:
      Accumulate(frame, rect, clip_level, [](const uint8_t* row, int32_t x) { return int32_t{row[x]}; },
                 &brightness);
      break;
    case PixelFormat::kY16:
      Accumulate(frame, rect, clip_level,
                 [](const uint8_t* row, int32_t x) {
                   return row[static_cast<ptrdiff_t>(x) * 2] | (row[static_cast<ptrdiff_t>(x) * 2 + 1] << 8);
                 },
                 &brightness);
      break;
  }
  brightness.pixel_count = static_cast<int64_t>(rect.width) * rect.height;
  return brightness;
}

double MeanOf(const Brightness& brightness) {
  if (brightness.pixel_count == 0) {
    return 0.0;
  }
  return static_cast<double>(brightness.sum) / static_cast<double>(brightness.pixel_count);
}

Flicker SummarizeFlicker(std::span<const int64_t> sums, int64_t pixel_count, double min_periodic_fraction) {
  Flicker flicker;
  flicker.frame_count = static_cast<int64_t>(sums.size());
  if (sums.size() < 4 || pixel_count <= 0) {
    return flicker;
  }
  const size_t n = sums.size();
  std::vector<double> means(n);
  for (size_t i = 0; i < n; ++i) {
    means[i] = static_cast<double>(sums[i]) / static_cast<double>(pixel_count);
  }
  double total = 0.0;
  for (const double m : means) total += m;
  flicker.mean = total / static_cast<double>(n);
  double squares = 0.0;
  for (const double m : means) squares += (m - flicker.mean) * (m - flicker.mean);
  flicker.stddev = std::sqrt(squares / static_cast<double>(n));
  const auto [lowest, highest] = std::minmax_element(means.begin(), means.end());
  flicker.peak_to_peak = *highest - *lowest;

  // 최소제곱 직선을 빼서 느린 변화가 긴 주기로 잡히지 않게 합니다.
  const double center = static_cast<double>(n - 1) / 2.0;
  double slope_num = 0.0;
  double slope_den = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const double x = static_cast<double>(i) - center;
    slope_num += x * (means[i] - flicker.mean);
    slope_den += x * x;
  }
  const double slope = slope_num / slope_den;
  std::vector<double> residual(n);
  double variance = 0.0;
  for (size_t i = 0; i < n; ++i) {
    residual[i] = means[i] - flicker.mean - slope * (static_cast<double>(i) - center);
    variance += residual[i] * residual[i];
  }
  variance /= static_cast<double>(n);
  // 정수 합계를 나눈 값의 반올림 오차보다 작은 흔들림은 일정한 밝기로 봅니다.
  if (variance <= 1e-18 * std::max(1.0, flicker.mean * flicker.mean)) {
    return flicker;
  }

  // 정수 k로 봉우리를 고른 뒤 주변을 0.01, 0.0001 간격으로 좁혀 소수 주기를 찾습니다.
  const double half = static_cast<double>(n / 2);
  double power = 0.0;
  double k = FindPeak(residual, 2.0, half, 1.0, &power);
  k = FindPeak(residual, std::max(2.0, k - 1.0), std::min(half, k + 1.0), 0.01, &power);
  k = FindPeak(residual, std::max(2.0, k - 0.01), std::min(half, k + 0.01), 0.0001, &power);

  // 진폭 A인 사인파 하나는 |X|^2 = (A n / 2)^2, 분산 A^2 / 2라 비율이 1이 됩니다. 나이퀴스트 성분은 그 2배로 잡혀 나눕니다.
  const double amplitude_power = 2.0 * power / (static_cast<double>(n) * static_cast<double>(n));
  const double nyquist_scale = (n % 2 == 0 && k >= half - 1e-9) ? 0.5 : 1.0;
  flicker.periodic_fraction = std::min(1.0, amplitude_power * nyquist_scale / variance);
  if (flicker.periodic_fraction >= min_periodic_fraction) {
    flicker.dominant_period_frames = static_cast<double>(n) / k;
  }
  return flicker;
}

LightResponse JudgeLightResponse(std::span<const double> before, std::span<const double> after,
                                 int32_t sent_delta_percent, const LightResponseRule& rule) {
  if (rule.window_frames <= 0 || rule.min_mean_change <= 0.0 || sent_delta_percent == 0) return LightResponse::kUnknown;
  const auto window = static_cast<size_t>(rule.window_frames);
  if (before.size() < window || after.size() < window) return LightResponse::kUnknown;
  double before_sum = 0.0;
  for (const double m : before.last(window)) before_sum += m;
  double after_sum = 0.0;
  for (const double m : after.first(window)) after_sum += m;
  const double change = (after_sum - before_sum) / static_cast<double>(window);
  const double toward = sent_delta_percent > 0 ? change : -change;
  return toward >= rule.min_mean_change ? LightResponse::kApplied : LightResponse::kNotSeen;
}

}  // namespace hwlab::pixel
