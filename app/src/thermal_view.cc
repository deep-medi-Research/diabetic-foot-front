#include "thermal_view.h"

#include <QDateTime>

#include <cmath>
#include <cstring>
#include <utility>

#include "hwlab/pixel/foot_warmth.h"
#include "hwlab/pixel/rect.h"

namespace {

// 표시 범위에서 가장 낮은 0.1%와 가장 높은 0.1% 화소를 뺍니다. 뜨거운 점 하나가 전체 대비를 낮추지 않게 하기 위함입니다.
constexpr int32_t kLowPermille = 1;
constexpr int32_t kHighPermille = 999;

}  // namespace

ThermalView::ThermalView(bool available, QString absent_reason, int64_t record_period_us, QObject* parent)
    : QObject(parent),
      available_(available),
      absent_reason_(std::move(absent_reason)),
      record_period_us_(record_period_us) {
  if (available_) converter_ = std::thread([this] { ConvertLoop(); });
}

ThermalView::~ThermalView() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
  }
  wake_.notify_all();
  if (converter_.joinable()) converter_.join();
}

std::function<void(const hwlab::FrameView&)> ThermalView::Sink() {
  return [this](const hwlab::FrameView& view) {
    if (view.format != hwlab::PixelFormat::kY16 || view.plane[0] == nullptr) return;
    const size_t row_bytes = static_cast<size_t>(view.width) * 2;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      raw_.resize(row_bytes * static_cast<size_t>(view.height));
      for (int32_t y = 0; y < view.height; ++y) {
        std::memcpy(raw_.data() + row_bytes * static_cast<size_t>(y),
                    view.plane[0] + static_cast<size_t>(view.row_stride[0]) * static_cast<size_t>(y), row_bytes);
      }
      raw_width_ = view.width;
      raw_height_ = view.height;
      dirty_ = true;
    }
    wake_.notify_one();
  };
}

void ThermalView::ConvertLoop() {
  std::vector<uint8_t> raw;
  std::vector<uint8_t> rgb;
  for (;;) {
    int32_t width = 0;
    int32_t height = 0;
    hwlab::pixel::Palette palette;
    std::optional<std::pair<hwlab::pixel::Rect, hwlab::pixel::Rect>> warmth_rects;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [this] { return quit_ || dirty_; });
      if (quit_) return;
      dirty_ = false;
      // 변환하는 동안 수신 스레드가 기다리지 않도록 화소를 꺼내 두고 잠금을 풉니다.
      raw = raw_;
      width = raw_width_;
      height = raw_height_;
      palette = pending_palette_;
      warmth_rects = warmth_rects_;
    }
    if (width <= 0 || height <= 0) continue;
    hwlab::FrameView view;
    view.format = hwlab::PixelFormat::kY16;
    view.width = width;
    view.height = height;
    view.plane[0] = raw.data();
    view.row_stride[0] = width * 2;
    view.stream = hwlab::StreamKind::kThermal;
    const hwlab::pixel::Y16Range range =
        hwlab::pixel::FindPercentileRange(view, {0, 0, width, height}, kLowPermille, kHighPermille);
    rgb.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    if (!hwlab::pixel::Y16ToRgbPreview(view, range, palette, 1, rgb)) continue;
    QImage image(width, height, QImage::Format_RGB888);
    for (int32_t y = 0; y < height; ++y) {
      std::memcpy(image.scanLine(y), rgb.data() + static_cast<size_t>(y) * width * 3, static_cast<size_t>(width) * 3);
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      image_ = std::move(image);
    }
    // 발 대비는 장마다 상자 두 개의 화소를 정렬하는 일이라(상자당 수만 화소) 변환과 같은 스레드에서 잽니다.
    int left = -1, right = -1;
    if (warmth_rects) {
      const auto inside = [&](const hwlab::pixel::Rect& r) {
        return !hwlab::pixel::IsEmpty(r) && r.x >= 0 && r.y >= 0 && r.x + r.width <= width && r.y + r.height <= height;
      };
      if (inside(warmth_rects->first) && inside(warmth_rects->second)) {
        left = hwlab::pixel::ThermalContrast(view, warmth_rects->first);
        right = hwlab::pixel::ThermalContrast(view, warmth_rects->second);
      }
    }
    QMetaObject::invokeMethod(this, [this, range, width, height, left, right] {
      range_ = range;
      frame_width_ = width;
      frame_height_ = height;
      ++preview_revision_;
      emit previewChanged();
      if (left >= 0) emit footContrast(left, right);
    }, Qt::QueuedConnection);
  }
}

void ThermalView::SetWarmthRegions(const std::optional<std::pair<hwlab::pixel::Rect, hwlab::pixel::Rect>>& rects) {
  std::lock_guard<std::mutex> lock(mutex_);
  warmth_rects_ = rects;
}

QImage ThermalView::LatestImage() {
  std::lock_guard<std::mutex> lock(mutex_);
  return image_;
}

void ThermalView::nextPalette() { SetPalette(hwlab::pixel::NextPalette(palette_)); }

QString ThermalView::paletteName() const {
  using hwlab::pixel::Palette;
  switch (palette_) {
    case Palette::kIron: return QStringLiteral("아이언");
    case Palette::kGray: return QStringLiteral("흑백");
    case Palette::kRainbow: return QStringLiteral("레인보우");
    case Palette::kHighContrast: return QStringLiteral("고대비");
  }
  return {};
}

void ThermalView::SetPalette(hwlab::pixel::Palette palette) {
  if (palette == palette_) return;
  palette_ = palette;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_palette_ = palette_;
    // 끊긴 동안에도 누른 결과가 보이도록 마지막 프레임을 다시 칠합니다.
    if (raw_width_ > 0) dirty_ = true;
  }
  wake_.notify_one();
  emit paletteChanged();
}

void ThermalView::Update(const hwlab::platform::ThermalStatus& status) {
  const double fps = std::round(status.measured_fps * 10.0) / 10.0;
  if (status.connection == status_.connection && status.received == status_.received &&
      status.written == status_.written && status.dropped == status_.dropped && status.missing == status_.missing &&
      status.error == status_.error && status.lens_shading == status_.lens_shading &&
      status.lens_shading_busy == status_.lens_shading_busy && fps == measured_fps_) {
    return;
  }
  status_ = status;
  measured_fps_ = fps;
  emit statusChanged();
}

void ThermalView::SetDeviceName(const QString& name) {
  if (name.isEmpty() || name == device_name_) return;
  device_name_ = name;
  emit statusChanged();
}

QString ThermalView::lensShading() const {
  using hwlab::record::LensShadingSource;
  if (!status_.lens_shading) return {};
  switch (status_.lens_shading->record.source) {
    case LensShadingSource::kNone:
      return QStringLiteral("none");
    case LensShadingSource::kLoaded:
      return QStringLiteral("loaded");
    case LensShadingSource::kCorrected:
      return QStringLiteral("corrected");
    case LensShadingSource::kFailed:
      return QStringLiteral("failed");
  }
  return {};
}

QString ThermalView::lensShadingText() const {
  if (status_.lens_shading_busy) return QStringLiteral("렌즈 보정 중");
  const QString key = lensShading();
  if (key.isEmpty()) return {};
  if (key == QStringLiteral("none")) return QStringLiteral("렌즈 보정 없음");
  if (key == QStringLiteral("failed")) return QStringLiteral("렌즈 보정 실패");
  const std::optional<int64_t> saved = status_.lens_shading->record.file_unix_s;
  // 보정한 날짜를 보여 다시 보정할 때를 사람이 판단하게 합니다. 저장하지 못했으면 이번 실행에만 적용됩니다.
  if (!saved) {
    return key == QStringLiteral("corrected") ? QStringLiteral("렌즈 보정 적용(저장 못 함)") : QStringLiteral("렌즈 보정 적용");
  }
  const QString when = QDateTime::fromSecsSinceEpoch(*saved).toString(QStringLiteral("M/d HH:mm"));
  return QStringLiteral("렌즈 보정 적용(%1 보정)").arg(when);
}

QString ThermalView::state() const {
  using hwlab::platform::ThermalConnection;
  if (!available_) return QStringLiteral("absent");
  if (status_.connection == ThermalConnection::kReceiving) return QStringLiteral("receiving");
  // 한 번이라도 받은 뒤 수신 중이 아니면 끊김입니다. 다시 여는 동안 연결 상태가 닫힘과 대기를 오가기 때문입니다.
  if (status_.connection == ThermalConnection::kLost || status_.received > 0) return QStringLiteral("lost");
  return QStringLiteral("searching");
}

QString ThermalView::stateText() const {
  const QString key = state();
  if (key == QStringLiteral("absent")) return absent_reason_;
  if (key == QStringLiteral("receiving")) return QStringLiteral("수신 중");
  if (key == QStringLiteral("lost")) return QStringLiteral("끊김, 다시 찾는 중");
  return QStringLiteral("찾는 중");
}
