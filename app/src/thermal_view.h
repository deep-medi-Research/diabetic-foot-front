#ifndef HWLAB_APP_THERMAL_VIEW_H_
#define HWLAB_APP_THERMAL_VIEW_H_

#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>
#include <thread>
#include <vector>

#include "hwlab/frame.h"
#include "hwlab/pixel/convert.h"
#include "hwlab/pixel/rect.h"
#include "hwlab/platform/capture.h"

/**
 * 열화상 미리보기와 열화상 스트림 상태의 ViewModel입니다.
 *
 * 열화상 수신 스레드가 넘긴 Y16 프레임을 복사만 해 두고, 이 클래스의 변환 스레드가 표시 범위를 찾아 RGB로 칠합니다.
 * 화면(QML)은 "image://thermal/<번호>"로 영상을 받아 가고 상태 문장을 그리기만 합니다.
 *
 * @note 열화상 SDK는 프레임을 보관하지 않아 수신 스레드에서 변환하면 프레임이 빠지고(기존 앱 실측 20~25fps),
 *       화면 스레드에서 변환하면 화면 갱신이 늦어지므로 변환은 전용 스레드에서만 합니다.
 */
class ThermalView : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("CaptureController가 만듭니다")

  Q_PROPERTY(bool available READ available CONSTANT)
  Q_PROPERTY(QString absentReason READ absentReason CONSTANT)
  Q_PROPERTY(QString state READ state NOTIFY statusChanged)
  Q_PROPERTY(QString stateText READ stateText NOTIFY statusChanged)
  Q_PROPERTY(QString deviceName READ deviceName NOTIFY statusChanged)
  /**
   * 지금 연결의 렌즈 셰이딩 보정 상태입니다. "loaded", "corrected", "failed", "none"(보정 파일 없음),
   * 열기 전이거나 끊긴 동안이거나 보정 기능이 없으면 빈 문자열입니다.
   */
  Q_PROPERTY(QString lensShading READ lensShading NOTIFY statusChanged)
  /** 렌즈 셰이딩 보정 상태 문장입니다. 예: "렌즈 보정 적용(10/23 16:40 보정)". 상태가 비면 빈 문자열입니다. */
  Q_PROPERTY(QString lensShadingText READ lensShadingText NOTIFY statusChanged)
  /** 렌즈 셰이딩 보정 요청을 아직 끝내지 않았는지입니다. 보정하는 동안 열화상 프레임이 오지 않습니다. */
  Q_PROPERTY(bool lensShadingBusy READ lensShadingBusy NOTIFY statusChanged)
  /**
   * 카메라 온도로 잰 예열 단계입니다(timing::WarmupPhase). "measuring", "warming", "settling", "ready"이고,
   * 수신 중이 아니거나 카메라 온도를 주지 않는 카메라는 빈 문자열입니다.
   */
  Q_PROPERTY(QString warmup READ warmup NOTIFY statusChanged)
  /** 예열 단계 문장입니다. 예: "예열 중". warmup이 비면 빈 문자열입니다. */
  Q_PROPERTY(QString warmupText READ warmupText NOTIFY statusChanged)
  /** 예열 단계의 근거입니다. 예: "지난 10분 분당 +0.12 °C", "4분 뒤 판정". warmup이 비면 빈 문자열입니다. */
  Q_PROPERTY(QString warmupDetail READ warmupDetail NOTIFY statusChanged)
  /**
   * 적용된 렌즈 보정의 판정입니다(timing::LensShadingFit). "unknown"(보정 때 기록 없음),
   * "made-warming", "wait-warmup", "moved", "fits"이고, 보정이 없거나 warmup이 비면 빈 문자열입니다.
   */
  Q_PROPERTY(QString lensFit READ lensFit NOTIFY statusChanged)
  /** lensFit 문장입니다. 예: "보정 때보다 0.9 °C 다름, 다시 보정". "fits"와 빈 lensFit은 빈 문자열입니다. */
  Q_PROPERTY(QString lensFitText READ lensFitText NOTIFY statusChanged)
  /** 다시 보정하라는 알림을 보일 때 true입니다. lensFit이 "made-warming"이나 "moved"인 때입니다. */
  Q_PROPERTY(bool lensRecalibrate READ lensRecalibrate NOTIFY statusChanged)
  Q_PROPERTY(double measuredFps READ measuredFps NOTIFY statusChanged)
  Q_PROPERTY(int written READ written NOTIFY statusChanged)
  Q_PROPERTY(int dropped READ dropped NOTIFY statusChanged)
  Q_PROPERTY(int missing READ missing NOTIFY statusChanged)
  Q_PROPERTY(QString error READ error NOTIFY statusChanged)
  Q_PROPERTY(double recordPeriodMs READ recordPeriodMs CONSTANT)
  Q_PROPERTY(double recordHz READ recordHz CONSTANT)
  Q_PROPERTY(QString paletteName READ paletteName NOTIFY paletteChanged)
  Q_PROPERTY(int rangeLow READ rangeLow NOTIFY previewChanged)
  Q_PROPERTY(int rangeHigh READ rangeHigh NOTIFY previewChanged)
  Q_PROPERTY(int previewRevision READ previewRevision NOTIFY previewChanged)

 public:
  /**
   * @param available 열화상 카메라를 Capture에 넘겼는지
   * @param absent_reason available이 false일 때 화면에 보일 문장
   * @param record_period_us 촬영 선언의 열화상 기록 주기. 0이면 받은 프레임을 모두 기록합니다
   */
  ThermalView(bool available, QString absent_reason, int64_t record_period_us, QObject* parent = nullptr);
  ~ThermalView() override;
  ThermalView(const ThermalView&) = delete;
  ThermalView& operator=(const ThermalView&) = delete;

  /**
   * Capture::SetThermalPreviewSink에 넘길 함수입니다.
   *
   * @return 열화상 수신 스레드에서 불리는 함수. 프레임을 복사하고 변환 스레드를 깨우기만 합니다
   * @note 이 객체가 Capture보다 오래 있어야 합니다.
   */
  std::function<void(const hwlab::FrameView&)> Sink();

  /**
   * Capture 상태의 열화상 부분을 반영합니다. 화면 스레드에서 부릅니다.
   *
   * @param status Capture::Status().thermal
   */
  void Update(const hwlab::platform::ThermalStatus& status);

  /** 마지막으로 변환한 미리보기 영상입니다. 어느 스레드에서 불러도 됩니다. */
  QImage LatestImage();

  /** 다음 색상표(아이언, 흑백, 레인보우, 고대비 순서, pixel::NextPalette)로 바꿉니다. 마지막 프레임을 새 색상표로 다시 칠합니다. */
  Q_INVOKABLE void nextPalette();

  /**
   * 색상표를 정합니다. 명령줄 시험 옵션(--palette)과 nextPalette가 씁니다. 화면 스레드에서 부릅니다.
   *
   * @param palette 쓸 색상표
   */
  void SetPalette(hwlab::pixel::Palette palette);

  bool available() const { return available_; }
  QString absentReason() const { return absent_reason_; }
  QString state() const;
  QString stateText() const;
  QString deviceName() const { return device_name_; }
  QString lensShading() const;
  QString lensShadingText() const;
  bool lensShadingBusy() const { return status_.lens_shading_busy; }
  QString warmup() const;
  QString warmupText() const;
  QString warmupDetail() const;
  QString lensFit() const;
  QString lensFitText() const;
  bool lensRecalibrate() const;
  double measuredFps() const { return measured_fps_; }
  int written() const { return static_cast<int>(status_.written); }
  int dropped() const { return static_cast<int>(status_.dropped); }
  int missing() const { return static_cast<int>(status_.missing); }
  QString error() const { return QString::fromStdString(status_.error); }
  double recordPeriodMs() const { return static_cast<double>(record_period_us_) / 1000.0; }
  double recordHz() const { return record_period_us_ > 0 ? 1e6 / static_cast<double>(record_period_us_) : 0.0; }
  QString paletteName() const;
  int rangeLow() const { return range_.low; }
  int rangeHigh() const { return range_.high; }
  int previewRevision() const { return preview_revision_; }

  /** 마지막으로 변환한 열화상 프레임의 폭입니다. 받기 전에는 0입니다. 화면 스레드의 값입니다. */
  int32_t FrameWidth() const { return frame_width_; }

  /** 마지막으로 변환한 열화상 프레임의 높이입니다. 받기 전에는 0입니다. 화면 스레드의 값입니다. */
  int32_t FrameHeight() const { return frame_height_; }

  /**
   * 장치 이름을 알립니다. 화면 스레드에서 부릅니다.
   *
   * @param name ThermalCamera::DeviceName. 열기 전에는 빈 문자열
   */
  void SetDeviceName(const QString& name);

  /**
   * 발 대비(pixel::ThermalContrast)를 잴 두 발 상자를 정합니다. 화면 스레드에서 부릅니다. 변환 스레드가 다음 장부터 씁니다.
   *
   * @param rects 화소 왼쪽, 오른쪽 상자. nullopt이면 재지 않습니다
   */
  void SetWarmthRegions(const std::optional<std::pair<hwlab::pixel::Rect, hwlab::pixel::Rect>>& rects);

 signals:
  void statusChanged();
  void paletteChanged();
  void previewChanged();
  /** 미리보기 한 장의 두 발 대비(원시값)입니다. 화소 왼쪽, 오른쪽 상자 순서입니다. 상자를 정했을 때만 보냅니다. */
  void footContrast(int left, int right);

 private:
  void ConvertLoop();
  std::optional<hwlab::timing::WarmupStatus> ShownWarmup() const;
  std::optional<hwlab::timing::LensShadingFit> ShownLensFit() const;

  const bool available_;
  const QString absent_reason_;
  const int64_t record_period_us_;
  hwlab::platform::ThermalStatus status_;
  QString device_name_;
  double measured_fps_ = 0.0;
  hwlab::pixel::Palette palette_ = hwlab::pixel::Palette::kIron;  // 화면 스레드의 값
  hwlab::pixel::Y16Range range_;                                    // 화면 스레드의 값
  int preview_revision_ = 0;
  int32_t frame_width_ = 0;   // 화면 스레드의 값
  int32_t frame_height_ = 0;  // 화면 스레드의 값

  std::mutex mutex_;
  std::condition_variable wake_;
  bool quit_ = false;                     // mutex_
  bool dirty_ = false;                    // mutex_. 변환하지 않은 프레임이나 색상표 변경이 있음
  hwlab::pixel::Palette pending_palette_ = hwlab::pixel::Palette::kIron;  // mutex_
  std::vector<uint8_t> raw_;              // mutex_. 마지막으로 받은 Y16 화소
  int32_t raw_width_ = 0;                 // mutex_
  int32_t raw_height_ = 0;                // mutex_
  QImage image_;                          // mutex_
  std::optional<std::pair<hwlab::pixel::Rect, hwlab::pixel::Rect>> warmth_rects_;  // mutex_
  std::thread converter_;
};

/** QML Image가 "image://thermal/<번호>"로 열화상 미리보기를 받아 가는 곳입니다. */
class ThermalImageProvider : public QQuickImageProvider {
 public:
  explicit ThermalImageProvider(ThermalView* view) : QQuickImageProvider(QQuickImageProvider::Image), view_(view) {}
  QImage requestImage(const QString&, QSize* size, const QSize&) override {
    QImage image = view_->LatestImage();
    if (size != nullptr) *size = image.size();
    return image;
  }

 private:
  ThermalView* view_;
};

#endif  // HWLAB_APP_THERMAL_VIEW_H_
