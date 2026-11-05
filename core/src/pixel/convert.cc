#include "hwlab/pixel/convert.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "uyvy_rgb.h"

namespace hwlab::pixel {

namespace {

uint16_t ReadY16(const uint8_t* row, int32_t x) {
  return static_cast<uint16_t>(row[static_cast<ptrdiff_t>(x) * 2] | (row[static_cast<ptrdiff_t>(x) * 2 + 1] << 8));
}

// 원시값을 [low, high] 구간에서 0~255 단계로 바꿉니다. span이 0이면 호출하지 않습니다.
uint8_t ToLevel(uint16_t value, Y16Range range, int32_t span) {
  const int32_t v = std::clamp<int32_t>(value, range.low, range.high) - range.low;
  return static_cast<uint8_t>((v * 255 + span / 2) / span);
}

struct PaletteStop {
  int32_t level;
  uint8_t r, g, b;
};

using PaletteTable = std::array<Rgb8, 256>;

// 첫 기준점은 0단계, 마지막 기준점은 255단계여야 합니다. 기준점 사이는 반올림한 선형 보간입니다.
template <size_t N>
constexpr PaletteTable MakeTable(const PaletteStop (&stops)[N]) {
  PaletteTable table{};
  for (size_t i = 0; i + 1 < N; ++i) {
    const PaletteStop& a = stops[i];
    const PaletteStop& b = stops[i + 1];
    const int32_t d = b.level - a.level;
    for (int32_t level = a.level; level <= b.level; ++level) {
      const int32_t wa = b.level - level;
      const int32_t wb = level - a.level;
      table[static_cast<size_t>(level)] = {static_cast<uint8_t>((a.r * wa + b.r * wb + d / 2) / d),
                                           static_cast<uint8_t>((a.g * wa + b.g * wb + d / 2) / d),
                                           static_cast<uint8_t>((a.b * wa + b.b * wb + d / 2) / d)};
    }
  }
  return table;
}

// kGray 기준점: 단계 값을 R, G, B에 그대로 씁니다. Y16ToGray8과 같은 값입니다.
constexpr PaletteStop kGrayStops[] = {{0, 0, 0, 0}, {255, 255, 255, 255}};

// kIron 기준점: 열화상 표시에서 흔한 검정, 보라, 빨강, 주황, 노랑, 흰색 순서를 이 레포에서 직접 정한 값입니다.
// 공개된 표준 표가 없고, 제조사 SDK 색상표(ApplyColorMap)의 값은 쓰지 않았습니다.
constexpr PaletteStop kIronStops[] = {
    {0, 0, 0, 0},       {50, 90, 0, 150},   {110, 210, 20, 40},
    {160, 250, 120, 0}, {210, 255, 220, 0}, {255, 255, 255, 255},
};

// kRainbow 기준점: MATLAB jet(OpenCV COLORMAP_JET도 같음)의 정의입니다. 남색(0, 0, 0.5)에서 시작해 1/8, 3/8, 5/8, 7/8
// 지점에 파랑, 청록, 노랑, 빨강을 두고 검붉은색(0.5, 0, 0)으로 끝납니다. 지점은 255배 해 가장 가까운 단계로 반올림했습니다.
constexpr PaletteStop kRainbowStops[] = {
    {0, 0, 0, 128},   {32, 0, 0, 255},  {96, 0, 255, 255},
    {159, 255, 255, 0}, {223, 255, 0, 0}, {255, 128, 0, 0},
};

// kHighContrast 기준점: RGB 정육면체의 꼭짓점 8개를 한 번에 한 채널만 바뀌는 순서로 같은 간격(255 * i / 7 반올림)에 둔
// 값으로, 이 레포에서 정했습니다. 약 36단계마다 한 채널이 0과 255 사이를 오가고, 같은 색이 두 번 나오지 않습니다.
constexpr PaletteStop kHighContrastStops[] = {
    {0, 0, 0, 0},     {36, 0, 0, 255},    {73, 0, 255, 255},   {109, 0, 255, 0},
    {146, 255, 255, 0}, {182, 255, 0, 0}, {219, 255, 0, 255}, {255, 255, 255, 255},
};

constexpr PaletteTable kGrayTable = MakeTable(kGrayStops);
constexpr PaletteTable kIronTable = MakeTable(kIronStops);
constexpr PaletteTable kRainbowTable = MakeTable(kRainbowStops);
constexpr PaletteTable kHighContrastTable = MakeTable(kHighContrastStops);

const PaletteTable& TableOf(Palette palette) {
  switch (palette) {
    case Palette::kGray: return kGrayTable;
    case Palette::kIron: return kIronTable;
    case Palette::kRainbow: return kRainbowTable;
    case Palette::kHighContrast: return kHighContrastTable;
  }
  return kGrayTable;
}

}  // namespace

Y16Range FindRange(const FrameView& y16, const Rect& rect) {
  if (y16.format != PixelFormat::kY16 || y16.plane[0] == nullptr || !IsAligned(rect, y16.format, y16.width, y16.height)) {
    return {};
  }
  uint16_t low = UINT16_MAX;
  uint16_t high = 0;
  for (int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    const uint8_t* row = y16.plane[0] + static_cast<ptrdiff_t>(y) * y16.row_stride[0];
    for (int32_t x = rect.x; x < rect.x + rect.width; ++x) {
      const uint16_t v = ReadY16(row, x);
      low = std::min(low, v);
      high = std::max(high, v);
    }
  }
  return {low, high};
}

bool Y16ToGray8(const FrameView& y16, Y16Range range, std::span<uint8_t> out) {
  if (y16.format != PixelFormat::kY16 || y16.plane[0] == nullptr || y16.width <= 0 || y16.height <= 0 ||
      out.size() != static_cast<size_t>(y16.width) * static_cast<size_t>(y16.height)) {
    return false;
  }
  // 범위가 한 값이면 모두 중간 회색으로 둡니다. 0으로 나누지 않기 위함입니다.
  const int32_t span = std::max<int32_t>(range.high - range.low, 0);
  for (int32_t y = 0; y < y16.height; ++y) {
    const uint8_t* row = y16.plane[0] + static_cast<ptrdiff_t>(y) * y16.row_stride[0];
    uint8_t* dst = out.data() + static_cast<size_t>(y) * static_cast<size_t>(y16.width);
    for (int32_t x = 0; x < y16.width; ++x) {
      if (span == 0) {
        dst[x] = 128;
        continue;
      }
      dst[x] = ToLevel(ReadY16(row, x), range, span);
    }
  }
  return true;
}

Y16Range FindPercentileRange(const FrameView& y16, const Rect& rect, int32_t low_permille, int32_t high_permille) {
  if (y16.format != PixelFormat::kY16 || y16.plane[0] == nullptr || !IsAligned(rect, y16.format, y16.width, y16.height) ||
      low_permille < 0 || high_permille > 1000 || low_permille > high_permille) {
    return {};
  }
  // 값이 16비트라 정렬 대신 도수분포로 순위를 찾습니다. 화소 수와 관계없이 메모리가 일정합니다.
  std::vector<uint32_t> histogram(65536, 0);
  for (int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    const uint8_t* row = y16.plane[0] + static_cast<ptrdiff_t>(y) * y16.row_stride[0];
    for (int32_t x = rect.x; x < rect.x + rect.width; ++x) ++histogram[ReadY16(row, x)];
  }
  const int64_t last = static_cast<int64_t>(rect.width) * rect.height - 1;
  const int64_t low_rank = (last * low_permille + 500) / 1000;
  const int64_t high_rank = (last * high_permille + 500) / 1000;
  Y16Range range;
  int64_t seen = 0;
  bool low_found = false;
  for (int32_t v = 0; v < 65536; ++v) {
    seen += histogram[v];
    if (!low_found && seen > low_rank) {
      range.low = static_cast<uint16_t>(v);
      low_found = true;
    }
    if (seen > high_rank) {
      range.high = static_cast<uint16_t>(v);
      break;
    }
  }
  return range;
}

Palette NextPalette(Palette palette) {
  switch (palette) {
    case Palette::kIron: return Palette::kGray;
    case Palette::kGray: return Palette::kRainbow;
    case Palette::kRainbow: return Palette::kHighContrast;
    case Palette::kHighContrast: return Palette::kIron;
  }
  return Palette::kIron;
}

Rgb8 PaletteColor(Palette palette, uint8_t level) { return TableOf(palette)[level]; }

bool Y16ToRgbPreview(const FrameView& y16, Y16Range range, Palette palette, int32_t step, std::span<uint8_t> out) {
  if (y16.format != PixelFormat::kY16 || y16.plane[0] == nullptr || step < 1) return false;
  const int32_t w = y16.width / step;
  const int32_t h = y16.height / step;
  if (w <= 0 || h <= 0 || out.size() != static_cast<size_t>(w) * static_cast<size_t>(h) * 3) return false;
  const int32_t span = std::max<int32_t>(range.high - range.low, 0);
  const PaletteTable& table = TableOf(palette);
  for (int32_t oy = 0; oy < h; ++oy) {
    const uint8_t* row = y16.plane[0] + static_cast<ptrdiff_t>(oy * step) * y16.row_stride[0];
    uint8_t* dst = out.data() + static_cast<size_t>(oy) * static_cast<size_t>(w) * 3;
    for (int32_t ox = 0; ox < w; ++ox) {
      const uint8_t level = span == 0 ? 128 : ToLevel(ReadY16(row, ox * step), range, span);
      const Rgb8& color = table[level];
      dst[ox * 3 + 0] = color.r;
      dst[ox * 3 + 1] = color.g;
      dst[ox * 3 + 2] = color.b;
    }
  }
  return true;
}

bool Gray8ToUyvy(std::span<const uint8_t> gray, int32_t width, int32_t height, std::span<uint8_t> out) {
  if (width <= 0 || height <= 0 || width % 2 != 0 ||
      gray.size() != static_cast<size_t>(width) * static_cast<size_t>(height) || out.size() != gray.size() * 2) {
    return false;
  }
  for (size_t i = 0; i < gray.size(); ++i) {
    out[i * 2] = 128;
    out[i * 2 + 1] = gray[i];
  }
  return true;
}

bool UyvyToPlanar422(const FrameView& uyvy, uint8_t* y, int32_t y_stride, uint8_t* u, int32_t u_stride, uint8_t* v,
                     int32_t v_stride) {
  if (uyvy.format != PixelFormat::kUyvy || uyvy.plane[0] == nullptr || uyvy.width <= 0 || uyvy.width % 2 != 0 ||
      uyvy.height <= 0 || y == nullptr || u == nullptr || v == nullptr) {
    return false;
  }
  const int32_t pairs = uyvy.width / 2;
  for (int32_t row = 0; row < uyvy.height; ++row) {
    const uint8_t* src = uyvy.plane[0] + static_cast<ptrdiff_t>(row) * uyvy.row_stride[0];
    uint8_t* dy = y + static_cast<ptrdiff_t>(row) * y_stride;
    uint8_t* du = u + static_cast<ptrdiff_t>(row) * u_stride;
    uint8_t* dv = v + static_cast<ptrdiff_t>(row) * v_stride;
    for (int32_t p = 0; p < pairs; ++p) {
      du[p] = src[p * 4];
      dy[p * 2] = src[p * 4 + 1];
      dv[p] = src[p * 4 + 2];
      dy[p * 2 + 1] = src[p * 4 + 3];
    }
  }
  return true;
}

bool UyvyToRgbPreview(const FrameView& uyvy, int32_t step, std::span<uint8_t> out) {
  if (uyvy.format != PixelFormat::kUyvy || uyvy.plane[0] == nullptr || step < 1) return false;
  const int32_t w = uyvy.width / step;
  const int32_t h = uyvy.height / step;
  if (w <= 0 || h <= 0 || out.size() != static_cast<size_t>(w) * static_cast<size_t>(h) * 3) return false;
  for (int32_t oy = 0; oy < h; ++oy) {
    const uint8_t* row = uyvy.plane[0] + static_cast<ptrdiff_t>(oy * step) * uyvy.row_stride[0];
    uint8_t* dst = out.data() + static_cast<size_t>(oy) * static_cast<size_t>(w) * 3;
    for (int32_t ox = 0; ox < w; ++ox) {
      const Rgb8 rgb = UyvyPixelToRgb(row, ox * step);
      dst[ox * 3 + 0] = rgb.r;
      dst[ox * 3 + 1] = rgb.g;
      dst[ox * 3 + 2] = rgb.b;
    }
  }
  return true;
}

}  // namespace hwlab::pixel
