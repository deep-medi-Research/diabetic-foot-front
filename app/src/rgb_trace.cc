#include "rgb_trace.h"

#include <QMetaObject>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <utility>

#include "hwlab/pixel/rgb_sums.h"

namespace {

// 미리보기 10장/초에서 5분입니다.
constexpr size_t kMaxSamples = 3000;
// 발 상자(1920x1200에서 약 640x1200)는 가로세로 2화소마다 읽어 상자 하나에 약 19만 화소입니다.
constexpr int32_t kBoxStep = 2;
// 관찰 영역은 모든 화소를 읽되, 큰 영역은 읽는 화소가 이 수 이하가 되게 간격을 넓힙니다.
constexpr int64_t kMaxAreaSamples = 40000;
// 프레임 시각이 이보다 크게 뛰거나 거꾸로 가면(카메라를 다시 연 경우) PC 시계로 이어 붙입니다.
constexpr int64_t kMaxFrameJumpUs = 2'000'000;

int32_t AreaStep(const hwlab::pixel::Rect& rect) {
  const int64_t pixels = static_cast<int64_t>(rect.width) * rect.height;
  if (pixels <= kMaxAreaSamples) return 1;
  return static_cast<int32_t>(std::ceil(std::sqrt(static_cast<double>(pixels) / kMaxAreaSamples)));
}

hwlab::pixel::Rect ClipToFrame(const hwlab::pixel::Rect& rect, int32_t width, int32_t height) {
  const int32_t x0 = std::max(0, rect.x);
  const int32_t y0 = std::max(0, rect.y);
  const int32_t x1 = std::min(width, rect.x + rect.width);
  const int32_t y1 = std::min(height, rect.y + rect.height);
  return {x0, y0, x1 - x0, y1 - y0};
}

}  // namespace

RgbTrace::RgbTrace(QObject* parent, std::chrono::milliseconds stale_after) : QObject(parent) {
  stale_timer_.setSingleShot(true);
  stale_timer_.setInterval(stale_after);
  connect(&stale_timer_, &QTimer::timeout, this, [this] {
    stale_ = true;
    emit staleChanged();
  });
}

double RgbTrace::startS() const {
  double start = 0.0;
  bool found = false;
  for (int target = 0; target < 2; ++target) {
    if (history_[target].empty()) continue;
    start = found ? std::min(start, history_[target].front().t_s) : history_[target].front().t_s;
    found = true;
  }
  return start;
}

double RgbTrace::endS() const {
  double end = 0.0;
  for (const auto& samples : history_) {
    if (!samples.empty()) end = std::max(end, samples.back().t_s);
  }
  return end;
}

QRect RgbTrace::area() const {
  if (!area_) return {};
  return QRect(area_->x, area_->y, area_->width, area_->height);
}

void RgbTrace::SetBoxes(const hwlab::pixel::SideRegions& boxes, int32_t frame_width, int32_t frame_height) {
  // 같은 위치로 다시 불려도(프레임 크기 알림 등) 기록을 지우지 않도록 바뀐 상자만 지웁니다.
  const bool left_moved = !(boxes.left == boxes_.left);
  const bool right_moved = !(boxes.right == boxes_.right);
  boxes_ = boxes;
  frame_width_ = frame_width;
  frame_height_ = frame_height;
  if (left_moved) ResetTarget(0);
  if (right_moved) ResetTarget(1);
  PublishTargets();
  if (left_moved || right_moved) emit sampled();
}

void RgbTrace::ResetTarget(int target) {
  ++generation_[static_cast<size_t>(target)];
  history_[static_cast<size_t>(target)].clear();
}

void RgbTrace::PublishTargets() {
  std::lock_guard<std::mutex> lock(targets_mutex_);
  targets_ = {boxes_, area_, generation_};
}

void RgbTrace::MarkFresh() {
  stale_timer_.start();
  if (!stale_) return;
  stale_ = false;
  emit staleChanged();
}

void RgbTrace::setArea(int x, int y, int width, int height) {
  hwlab::pixel::Rect rect{x, y, width, height};
  if (frame_width_ > 0) rect = ClipToFrame(rect, frame_width_, frame_height_);
  if (hwlab::pixel::IsEmpty(rect)) return;
  area_ = rect;
  ResetTarget(2);
  PublishTargets();
  emit areaChanged();
  emit sampled();
}

void RgbTrace::clearArea() {
  if (!area_) return;
  area_.reset();
  ResetTarget(2);
  PublishTargets();
  emit areaChanged();
  emit sampled();
}

void RgbTrace::Measure(const hwlab::FrameView& view) {
  Targets targets;
  {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    targets = targets_;
  }
  // 표본 시각은 프레임 시각의 간격으로 늘립니다. 수신 스레드가 늦게 불려도 간격이 고르게 남습니다.
  const auto now = std::chrono::steady_clock::now();
  double t_s = 0.0;
  if (last_frame_us_) {
    const int64_t step_us = view.time.us - *last_frame_us_;
    t_s = last_t_s_ + (step_us >= 0 && step_us <= kMaxFrameJumpUs
                           ? static_cast<double>(step_us) / 1e6
                           : std::chrono::duration<double>(now - last_steady_).count());
  }
  last_frame_us_ = view.time.us;
  last_steady_ = now;
  last_t_s_ = t_s;

  const auto make = [t_s](const hwlab::pixel::RgbSums& sums) {
    std::optional<Sample> sample;
    if (sums.pixel_count <= 0) return sample;
    sample.emplace();
    sample->t_s = t_s;
    const auto n = static_cast<double>(sums.pixel_count);
    for (size_t c = 0; c < 3; ++c) {
      sample->mean[c] = static_cast<float>(static_cast<double>(sums.sum[c]) / n);
      sample->saturated[c] = static_cast<float>(static_cast<double>(sums.saturated[c]) / n);
    }
    return sample;
  };
  const std::optional<Sample> left = make(hwlab::pixel::MeasureRgb(view, targets.boxes.left, kBoxStep));
  const std::optional<Sample> right = make(hwlab::pixel::MeasureRgb(view, targets.boxes.right, kBoxStep));
  std::optional<Sample> area;
  if (targets.area) {
    const hwlab::pixel::Rect rect = ClipToFrame(*targets.area, view.width, view.height);
    if (!hwlab::pixel::IsEmpty(rect)) area = make(hwlab::pixel::MeasureRgb(view, rect, AreaStep(rect)));
  }
  const std::array<uint64_t, 3> generation = targets.generation;
  QMetaObject::invokeMethod(this, [this, left, right, area, generation] {
    const std::array<const std::optional<Sample>*, 3> samples{&left, &right, &area};
    bool appended = false;
    for (int target = 0; target < 3; ++target) {
      const std::optional<Sample>& sample = *samples[static_cast<size_t>(target)];
      if (!sample || generation[static_cast<size_t>(target)] != generation_[static_cast<size_t>(target)]) continue;
      Append(target, *sample);
      appended = true;
    }
    if (appended) MarkFresh();
    emit sampled();
  }, Qt::QueuedConnection);
}

void RgbTrace::Append(int target, const Sample& sample) {
  std::deque<Sample>& samples = history_[static_cast<size_t>(target)];
  samples.push_back(sample);
  while (samples.size() > kMaxSamples) samples.pop_front();
}

QVariantMap RgbTrace::latest(int target) const {
  if (target < 0 || target > 2 || history_[static_cast<size_t>(target)].empty()) {
    return {{QStringLiteral("valid"), false}};
  }
  const Sample& s = history_[static_cast<size_t>(target)].back();
  return {{QStringLiteral("valid"), true},
          {QStringLiteral("r"), s.mean[0]},
          {QStringLiteral("g"), s.mean[1]},
          {QStringLiteral("b"), s.mean[2]},
          {QStringLiteral("satR"), s.saturated[0] * 100.0},
          {QStringLiteral("satG"), s.saturated[1] * 100.0},
          {QStringLiteral("satB"), s.saturated[2] * 100.0}};
}

QVariantMap RgbTrace::series(int target, double from_s, double to_s) const {
  QVariantList t, r, g, b, sat_r, sat_g, sat_b;
  if (target >= 0 && target <= 2) {
    const std::deque<Sample>& samples = history_[static_cast<size_t>(target)];
    auto first = std::lower_bound(samples.begin(), samples.end(), from_s,
                                  [](const Sample& s, double v) { return s.t_s < v; });
    auto last = std::upper_bound(samples.begin(), samples.end(), to_s,
                                 [](double v, const Sample& s) { return v < s.t_s; });
    if (first != samples.begin()) --first;
    if (last != samples.end()) ++last;
    const auto n = static_cast<qsizetype>(std::distance(first, last));
    for (QVariantList* list : {&t, &r, &g, &b, &sat_r, &sat_g, &sat_b}) list->reserve(n);
    for (auto it = first; it != last; ++it) {
      t.append(it->t_s);
      r.append(it->mean[0]);
      g.append(it->mean[1]);
      b.append(it->mean[2]);
      sat_r.append(it->saturated[0]);
      sat_g.append(it->saturated[1]);
      sat_b.append(it->saturated[2]);
    }
  }
  return {{QStringLiteral("t"), t},         {QStringLiteral("r"), r},
          {QStringLiteral("g"), g},         {QStringLiteral("b"), b},
          {QStringLiteral("satR"), sat_r},  {QStringLiteral("satG"), sat_g},
          {QStringLiteral("satB"), sat_b}};
}
