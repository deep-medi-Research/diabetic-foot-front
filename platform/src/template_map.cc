#include "hwlab/platform/template_map.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace hwlab::platform {

namespace {

// 계산하지 않는 칸(발가락 사이, 발 둘레)의 색입니다. 결과 창 바탕보다 조금 밝습니다.
constexpr std::array<uint8_t, 3> kGapColor = {34, 38, 44};

// 길이 위치 u에서 윤곽 한쪽 끝의 가로 위치입니다. outline_u는 0에서 1로 늘어납니다.
double Interpolate(const std::vector<double>& u, const std::vector<double>& x, double at) {
  if (at <= u.front()) return x.front();
  if (at >= u.back()) return x.back();
  const auto upper = std::upper_bound(u.begin(), u.end(), at);
  const size_t i = static_cast<size_t>(upper - u.begin());
  const double t = (at - u[i - 1]) / (u[i] - u[i - 1]);
  return x[i - 1] + t * (x[i] - x[i - 1]);
}

uint8_t Byte(double v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); }

// 발 하나를 그립니다. 영상 왼쪽이 윤곽의 왼쪽 끝(outline_left)입니다.
void DrawFoot(const PerfusionDetail& d, int32_t foot, int32_t window, double top, int32_t height, double x_min,
              int32_t width, bool mirror, int32_t x_offset, RgbaImage* image) {
  const int32_t rows = d.map_rows;
  const int32_t cols = d.map_cols;
  const auto cell = [&](int32_t r, int32_t c) -> double {
    if (r < 0 || r >= rows || c < 0 || c >= cols || d.map_valid[static_cast<size_t>(r * cols + c)] == 0) {
      return std::numeric_limits<double>::quiet_NaN();
    }
    return d.MapAt(foot, window, r, c);
  };
  for (int32_t y = 0; y < height; ++y) {
    const double u = static_cast<double>(y) / (height - 1);
    const double a = Interpolate(d.outline_u, d.outline_left, u);
    const double b = Interpolate(d.outline_u, d.outline_right, u);
    for (int32_t x = 0; x < width; ++x) {
      const double across = x_min + static_cast<double>(x) / (height - 1);
      if (across < a || across > b || b <= a) continue;
      const double fr = u * rows - 0.5;
      const double fc = (across - a) / (b - a) * cols - 0.5;
      const int32_t near_r = std::clamp(static_cast<int32_t>(std::lround(fr)), 0, rows - 1);
      const int32_t near_c = std::clamp(static_cast<int32_t>(std::lround(fc)), 0, cols - 1);
      std::array<uint8_t, 3> color = kGapColor;
      if (std::isfinite(cell(near_r, near_c))) {
        // 이웃 네 칸 중 계산 칸만 무게를 다시 나눠 섞습니다. 계산하지 않는 칸 쪽으로 색이 번지지 않습니다.
        const auto r0 = static_cast<int32_t>(std::floor(fr));
        const auto c0 = static_cast<int32_t>(std::floor(fc));
        const double tr = fr - r0, tc = fc - c0;
        double sum = 0, weight = 0;
        for (const auto& [r, c, w] : {std::tuple{r0, c0, (1 - tr) * (1 - tc)}, std::tuple{r0, c0 + 1, (1 - tr) * tc},
                                      std::tuple{r0 + 1, c0, tr * (1 - tc)}, std::tuple{r0 + 1, c0 + 1, tr * tc}}) {
          const double v = cell(r, c);
          if (!std::isfinite(v) || w <= 0) continue;
          sum += v * w;
          weight += w;
        }
        color = TurboColor(weight > 0 ? sum / weight : cell(near_r, near_c), top);
      }
      const int32_t px = x_offset + (mirror ? width - 1 - x : x);
      uint8_t* p = image->rgba.data() + (static_cast<size_t>(y) * image->width + px) * 4;
      p[0] = color[0];
      p[1] = color[1];
      p[2] = color[2];
      p[3] = 255;
    }
  }
}

}  // namespace

std::array<uint8_t, 3> TurboColor(double value, double top) {
  if (!std::isfinite(value) || !std::isfinite(top) || top <= 0) return {128, 128, 128};
  const double x = std::clamp(value / top, 0.0, 1.0);
  // Google turbo 색표의 다항식 근사입니다(Mikhailov 2019). matplotlib turbo와 눈으로 구분되지 않습니다.
  const double r = 0.13572138 + x * (4.61539260 + x * (-42.66032258 + x * (132.13108234 + x * (-152.94239396 + x * 59.28637943))));
  const double g = 0.09140261 + x * (2.19418839 + x * (4.84296658 + x * (-14.18503333 + x * (4.27729857 + x * 2.82956604))));
  const double b = 0.10667330 + x * (12.64194608 + x * (-60.58204836 + x * (110.36276771 + x * (-89.90310912 + x * 27.34824973))));
  return {Byte(r), Byte(g), Byte(b)};
}

std::optional<double> TemplateMapTop(const PerfusionDetail& detail) {
  if (!detail.HasTemplateMap()) return std::nullopt;
  const size_t per_map = static_cast<size_t>(detail.map_rows) * detail.map_cols;
  std::vector<double> values;
  for (size_t i = 0; i < detail.maps.size(); ++i) {
    if (detail.map_valid[i % per_map] != 0 && std::isfinite(detail.maps[i])) values.push_back(detail.maps[i]);
  }
  if (values.empty()) return std::nullopt;
  const size_t at = (values.size() - 1) * 95 / 100;
  std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(at), values.end());
  return values[at] > 0 ? std::optional<double>(values[at]) : std::nullopt;
}

std::optional<RgbaImage> DrawTemplateMap(const PerfusionDetail& detail, int32_t window, int32_t foot_height, double top) {
  if (!detail.HasTemplateMap() || foot_height < 2 || window < 0 ||
      window >= static_cast<int32_t>(detail.window_centers_s.size())) {
    return std::nullopt;
  }
  const double x_min = *std::min_element(detail.outline_left.begin(), detail.outline_left.end());
  const double x_max = *std::max_element(detail.outline_right.begin(), detail.outline_right.end());
  const auto foot_width = static_cast<int32_t>(std::ceil((x_max - x_min) * (foot_height - 1))) + 1;
  // 두 발 사이는 발 높이의 1/15입니다. 관류 분석 프로그램 그림과 비슷한 간격입니다.
  const int32_t gap = std::max(4, foot_height / 15);
  RgbaImage image;
  image.width = foot_width * 2 + gap;
  image.height = foot_height;
  image.rgba.assign(static_cast<size_t>(image.width) * image.height * 4, 0);
  DrawFoot(detail, 0, window, top, foot_height, x_min, foot_width, false, 0, &image);
  DrawFoot(detail, 1, window, top, foot_height, x_min, foot_width, true, foot_width + gap, &image);
  return image;
}

}  // namespace hwlab::platform
