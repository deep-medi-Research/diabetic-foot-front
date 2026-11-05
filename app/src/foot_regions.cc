#include "foot_regions.h"

#include <QSize>
#include <QStringList>

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <vector>

#include "remembered_values.h"

namespace {

using hwlab::pixel::Rect;
using hwlab::pixel::Side;
using hwlab::pixel::SideFinding;
using hwlab::pixel::SideProblem;

// 상자를 줄일 수 있는 최소 크기(프레임 화소)입니다. 모서리 둘이 한 점에 모여 다시 잡을 수 없게 되는 것을 막습니다.
constexpr int32_t kMinSide = 32;

bool Has(const std::vector<SideFinding>& findings, SideProblem problem, Side side) {
  return std::find(findings.begin(), findings.end(), SideFinding{problem, side}) != findings.end();
}

}  // namespace

FootRegions::FootRegions(QString settings_key, hwlab::PixelFormat format, int32_t default_max_width,
                         RememberedValues* remembered_values, QObject* parent)
    : QObject(parent),
      settings_key_(std::move(settings_key)),
      format_(format),
      default_max_width_(default_max_width),
      remembered_values_(remembered_values) {}

void FootRegions::SetFrameSize(int32_t width, int32_t height) {
  if (width == frame_width_ && height == frame_height_) return;
  frame_width_ = width;
  frame_height_ = height;
  regions_ = hwlab::pixel::DefaultSideRegions(width, height, default_max_width_, format_);
  // 저장한 영역은 같은 프레임 크기에서만 씁니다. 해상도가 바뀌면 같은 숫자가 다른 위치를 가리키기 때문입니다.
  if (remembered_values_->Value(settings_key_ + QStringLiteral("/frame")).toSize() == QSize(width, height)) {
    const QRect l = remembered_values_->Value(settings_key_ + QStringLiteral("/left")).toRect();
    const QRect r = remembered_values_->Value(settings_key_ + QStringLiteral("/right")).toRect();
    const Rect left = hwlab::pixel::ShiftInside({l.x(), l.y(), l.width(), l.height()}, format_, width, height);
    const Rect right = hwlab::pixel::ShiftInside({r.x(), r.y(), r.width(), r.height()}, format_, width, height);
    if (!hwlab::pixel::IsEmpty(left) && !hwlab::pixel::IsEmpty(right)) regions_ = {left, right};
  }
  Recheck();
}

void FootRegions::SetRegions(const hwlab::pixel::SideRegions& regions) {
  regions_ = regions;
  Recheck();
}

void FootRegions::SetSideNames(const QString& left, const QString& right) {
  if (left == left_name_ && right == right_name_) return;
  left_name_ = left;
  right_name_ = right;
  Recheck();
}

std::optional<std::pair<Rect, Rect>> FootRegions::ReadyPair() const {
  if (!ready()) return std::nullopt;
  return std::make_pair(regions_.left, regions_.right);
}

void FootRegions::moveBox(int side, int x, int y) {
  if (frame_width_ <= 0 || (side != 0 && side != 1)) return;
  Rect& rect = side == 0 ? regions_.left : regions_.right;
  const Rect moved = hwlab::pixel::ShiftInside({x, y, rect.width, rect.height}, format_, frame_width_, frame_height_);
  if (moved == rect || hwlab::pixel::IsEmpty(moved)) return;
  rect = moved;
  Recheck();
}

void FootRegions::resizeBox(int side, int corner, int x, int y) {
  if (frame_width_ <= 0 || (side != 0 && side != 1) || corner < 0 || corner > 3) return;
  Rect& rect = side == 0 ? regions_.left : regions_.right;
  const bool moves_left = corner == 0 || corner == 2;
  const bool moves_top = corner == 0 || corner == 1;
  // 맞은편 모서리를 고정하고, 움직이는 모서리는 프레임 안과 최소 크기 안으로 당깁니다.
  const int32_t fixed_x = moves_left ? rect.x + rect.width : rect.x;
  const int32_t fixed_y = moves_top ? rect.y + rect.height : rect.y;
  const int32_t new_x = moves_left ? std::clamp(x, 0, std::max(0, fixed_x - kMinSide))
                                   : std::clamp(x, std::min(frame_width_, fixed_x + kMinSide), frame_width_);
  const int32_t new_y = moves_top ? std::clamp(y, 0, std::max(0, fixed_y - kMinSide))
                                  : std::clamp(y, std::min(frame_height_, fixed_y + kMinSide), frame_height_);
  const Rect wanted{std::min(fixed_x, new_x), std::min(fixed_y, new_y), std::abs(new_x - fixed_x),
                    std::abs(new_y - fixed_y)};
  const Rect fitted = hwlab::pixel::FitToGrid(wanted, format_, frame_width_, frame_height_);
  if (fitted == rect || hwlab::pixel::IsEmpty(fitted)) return;
  rect = fitted;
  Recheck();
}

void FootRegions::save() {
  if (frame_width_ <= 0) return;
  remembered_values_->SetValue(settings_key_ + QStringLiteral("/frame"), QSize(frame_width_, frame_height_));
  remembered_values_->SetValue(settings_key_ + QStringLiteral("/left"), left());
  remembered_values_->SetValue(settings_key_ + QStringLiteral("/right"), right());
}

void FootRegions::Recheck() {
  const auto findings = hwlab::pixel::CheckSideRegions(regions_, format_, frame_width_, frame_height_);
  QStringList lines;
  const std::pair<Side, const QString*> sides[] = {{Side::kLeft, &left_name_}, {Side::kRight, &right_name_}};
  for (const auto& [side, name] : sides) {
    const Rect& r = side == Side::kLeft ? regions_.left : regions_.right;
    if (Has(findings, SideProblem::kNotAligned, side)) {
      lines << QStringLiteral("%1가 프레임 밖이거나 비어 있습니다").arg(*name);
    } else if (Has(findings, SideProblem::kNotPortrait, side)) {
      lines << QStringLiteral("%1가 세로로 길지 않습니다(폭 %2, 높이 %3)").arg(*name).arg(r.width).arg(r.height);
    }
  }
  if (Has(findings, SideProblem::kWrongOrder, Side::kLeft)) {
    lines << QStringLiteral("%1가 %2보다 오른쪽에 있습니다").arg(left_name_, right_name_);
  }
  if (Has(findings, SideProblem::kIntersecting, Side::kLeft)) {
    lines << QStringLiteral("두 상자가 같은 화소를 포함합니다");
  }
  left_ok_ = findings.empty() || std::none_of(findings.begin(), findings.end(), [](const SideFinding& f) {
               return f.side == Side::kLeft;
             });
  right_ok_ = findings.empty() || std::none_of(findings.begin(), findings.end(), [](const SideFinding& f) {
                return f.side == Side::kRight;
              });
  problem_ = frame_width_ > 0 ? lines.join(QStringLiteral("\n")) : QString();
  emit changed();
}
