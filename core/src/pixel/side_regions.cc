#include "hwlab/pixel/side_regions.h"

#include <algorithm>
#include <utility>

namespace hwlab::pixel {

namespace {

int32_t RoundDown(int32_t value, int32_t grid) { return value / grid * grid; }

bool Intersects(const Rect& a, const Rect& b) {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

}  // namespace

SideRegions DefaultSideRegions(int32_t frame_width, int32_t frame_height, int32_t max_width, PixelFormat format) {
  const ChromaGrid grid = ChromaGridOf(format);
  // 가운데 세로선이 격자에 맞아야 두 영역 모두 격자에 맞습니다.
  const int32_t center = RoundDown(frame_width / 2, grid.x);
  const int32_t limit = max_width > 0 ? std::min(max_width, center) : center;
  const int32_t width = RoundDown(std::min(limit, frame_width - center), grid.x);
  const int32_t height = RoundDown(frame_height, grid.y);
  if (width <= 0 || height <= 0) return {};
  return {{center - width, 0, width, height}, {center, 0, width, height}};
}

Rect ShiftInside(const Rect& rect, PixelFormat format, int32_t frame_width, int32_t frame_height) {
  if (IsEmpty(rect) || frame_width <= 0 || frame_height <= 0) return {};
  const ChromaGrid grid = ChromaGridOf(format);
  const int32_t max_w = RoundDown(frame_width, grid.x);
  const int32_t max_h = RoundDown(frame_height, grid.y);
  const int32_t width = RoundDown(std::min(rect.width, max_w), grid.x);
  const int32_t height = RoundDown(std::min(rect.height, max_h), grid.y);
  if (width <= 0 || height <= 0) return {};
  // 끝을 먼저 프레임 안으로 당긴 뒤 시작을 격자에 맞춰 내리므로 크기는 바뀌지 않고 프레임 밖으로 나가지 않습니다.
  const int32_t x = RoundDown(std::clamp(rect.x, 0, max_w - width), grid.x);
  const int32_t y = RoundDown(std::clamp(rect.y, 0, max_h - height), grid.y);
  return {x, y, width, height};
}

std::vector<SideFinding> CheckSideRegions(const SideRegions& regions, PixelFormat format, int32_t frame_width,
                                          int32_t frame_height) {
  std::vector<SideFinding> findings;
  const std::pair<Side, const Rect*> sides[] = {{Side::kLeft, &regions.left}, {Side::kRight, &regions.right}};
  for (const auto& [side, rect] : sides) {
    if (!IsAligned(*rect, format, frame_width, frame_height)) findings.push_back({SideProblem::kNotAligned, side});
    if (!IsPortrait(*rect)) findings.push_back({SideProblem::kNotPortrait, side});
  }
  if (IsEmpty(regions.left) || IsEmpty(regions.right)) return findings;
  // 가운데를 비교하는 이유: 영역 크기가 달라도 화면에서 보이는 좌우 순서와 같은 결과를 내기 위함입니다.
  if (2 * regions.left.x + regions.left.width >= 2 * regions.right.x + regions.right.width) {
    findings.push_back({SideProblem::kWrongOrder, Side::kLeft});
    findings.push_back({SideProblem::kWrongOrder, Side::kRight});
  }
  if (Intersects(regions.left, regions.right)) {
    findings.push_back({SideProblem::kIntersecting, Side::kLeft});
    findings.push_back({SideProblem::kIntersecting, Side::kRight});
  }
  return findings;
}

}  // namespace hwlab::pixel
