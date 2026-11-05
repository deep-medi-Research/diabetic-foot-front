#ifndef HWLAB_APP_RGB_TRACE_H_
#define HWLAB_APP_RGB_TRACE_H_

#include <QObject>
#include <QRect>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

#include "hwlab/frame.h"
#include "hwlab/pixel/rect.h"
#include "hwlab/pixel/side_regions.h"

/**
 * 미리보기 옆 RGB 패널의 ViewModel입니다. 두 발 상자와 관찰 영역의 채널별 평균과 포화 비율을 시간 순서로 모읍니다.
 *
 * 수신 스레드가 Measure로 미리보기 프레임마다 잰 값을 화면 스레드로 보내고, 화면 스레드가 대상마다 최근 5분을 둡니다.
 * 화면은 보이는 구간만 series로 받아 그립니다.
 *
 * 대상 번호는 0 화소 왼쪽 상자, 1 화소 오른쪽 상자, 2 관찰 영역입니다(FootRegions의 left, right와 같은 순서).
 *
 * 상자나 관찰 영역을 옮기면 그 대상의 기록을 지우고, 옮기기 전 위치로 잰 채 화면 스레드로 오던 표본도 버립니다.
 * 앞뒤 위치의 표본이 한 선으로 이어지지 않게 하기 위함입니다.
 *
 * @note 값은 미리보기와 같은 RGB 변환(pixel::MeasureRgb)으로 잽니다. 화면 표시용이고 녹화 파일과 사이드카에는 남기지 않습니다.
 */
class RgbTrace : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("CaptureController가 만듭니다")

  /** 발 상자 기록의 첫 표본과 마지막 표본 시각(초, 미리보기 시작 기준)입니다. 기록이 없으면 둘 다 0입니다. */
  Q_PROPERTY(double startS READ startS NOTIFY sampled)
  Q_PROPERTY(double endS READ endS NOTIFY sampled)
  Q_PROPERTY(bool hasArea READ hasArea NOTIFY areaChanged)
  /** 관찰 영역(프레임 화소). 없으면 빈 사각형입니다. */
  Q_PROPERTY(QRect area READ area NOTIFY areaChanged)
  /**
   * 표본을 받은 적이 있는데 stale_after 넘게 새 표본이 없으면 true입니다. 카메라가 프레임을 보내지 않는 동안 latest는 마지막
   * 표본을 그대로 돌려주므로, 화면은 이 값으로 숫자가 지금 값이 아님을 보입니다.
   */
  Q_PROPERTY(bool stale READ stale NOTIFY staleChanged)

 public:
  /**
   * @param parent Qt 부모
   * @param stale_after 마지막 표본 뒤 이 시간이 지나면 stale이 됩니다. 미리보기 10장/초에서 1초는 10장을 받지 못한 것입니다
   */
  explicit RgbTrace(QObject* parent = nullptr, std::chrono::milliseconds stale_after = std::chrono::seconds(1));

  double startS() const;
  double endS() const;
  bool hasArea() const { return area_.has_value(); }
  QRect area() const;
  bool stale() const { return stale_; }

  /**
   * 잴 발 상자와 프레임 크기를 정합니다. 화면 스레드에서 상자가 바뀔 때 부릅니다.
   *
   * 위치가 바뀐 상자는 기록을 지우고, 앞 위치로 잰 표본이 뒤늦게 와도 버립니다. 위치가 같은 상자의 기록은 남깁니다.
   *
   * @param boxes 화소 왼쪽과 화소 오른쪽 상자
   * @param frame_width 프레임 폭. 관찰 영역을 프레임 안으로 자를 때 씁니다
   * @param frame_height 프레임 높이
   */
  void SetBoxes(const hwlab::pixel::SideRegions& boxes, int32_t frame_width, int32_t frame_height);

  /**
   * 미리보기 프레임 한 장에서 두 상자와 관찰 영역을 재고 화면 스레드로 보냅니다. 수신 스레드에서 부릅니다.
   *
   * @param view UYVY 프레임. 호출이 끝나면 무효입니다
   * @note 수신 스레드를 늦추지 않도록 발 상자는 가로세로 2화소마다, 관찰 영역은 읽는 화소가 kMaxAreaSamples 이하가 되는
   *       간격으로 읽습니다. 1920x1200 발 상자 둘을 읽어도 미리보기 한 장(0.1초)마다 수 ms입니다.
   */
  void Measure(const hwlab::FrameView& view);

  /**
   * 관찰 영역을 정합니다. 앞 관찰 영역의 기록은 지웁니다.
   *
   * @param x 왼쪽 위 x(프레임 화소)
   * @param y 왼쪽 위 y(프레임 화소)
   * @param width 폭. 프레임 밖은 잘라 냅니다
   * @param height 높이
   */
  Q_INVOKABLE void setArea(int x, int y, int width, int height);

  /** 관찰 영역과 그 기록을 지웁니다. */
  Q_INVOKABLE void clearArea();

  /**
   * 대상의 마지막 표본입니다.
   *
   * @param target 0 화소 왼쪽 상자, 1 화소 오른쪽 상자, 2 관찰 영역
   * @return {valid, r, g, b, satR, satG, satB}. 평균은 0~255, 포화는 255인 화소의 비율(%)입니다. 표본이 없으면 valid가 false
   * @note 새 표본이 오지 않아도 마지막 표본을 돌려줍니다. 지금 값인지는 stale로 구분합니다.
   */
  Q_INVOKABLE QVariantMap latest(int target) const;

  /**
   * 대상의 [from_s, to_s] 구간 표본입니다. 선이 구간 끝에서 끊기지 않게 양쪽 바깥 표본을 하나씩 더 넣습니다.
   *
   * @param target 0 화소 왼쪽 상자, 1 화소 오른쪽 상자, 2 관찰 영역
   * @return {t, r, g, b, satR, satG, satB}. 모두 같은 길이의 배열이고 t는 초, 포화는 비율(0~1)입니다
   */
  Q_INVOKABLE QVariantMap series(int target, double from_s, double to_s) const;

 signals:
  /** 표본을 받았거나 발 상자, 관찰 영역의 기록을 지웠습니다. 화면은 이때 latest와 series를 다시 읽습니다. */
  void sampled();
  void areaChanged();
  void staleChanged();

 private:
  struct Sample {
    double t_s = 0.0;
    std::array<float, 3> mean{};
    std::array<float, 3> saturated{};  // 채널 값이 255인 화소의 비율(0~1)
  };

  // 수신 스레드가 읽는 대상입니다. generation은 대상(0 왼쪽 상자, 1 오른쪽 상자, 2 관찰 영역)의 위치를 바꿀 때마다 늘어,
  // 바꾸기 전에 잰 표본을 버리는 데 씁니다.
  struct Targets {
    hwlab::pixel::SideRegions boxes;
    std::optional<hwlab::pixel::Rect> area;
    std::array<uint64_t, 3> generation{};
  };

  void Append(int target, const Sample& sample);
  void PublishTargets();
  void ResetTarget(int target);
  void MarkFresh();

  // 화면 스레드에서만 씁니다.
  std::array<std::deque<Sample>, 3> history_;
  std::optional<hwlab::pixel::Rect> area_;
  std::array<uint64_t, 3> generation_{};
  bool stale_ = false;
  QTimer stale_timer_;
  int32_t frame_width_ = 0;
  int32_t frame_height_ = 0;
  hwlab::pixel::SideRegions boxes_;

  // 화면 스레드가 쓰고 수신 스레드가 복사해 읽습니다.
  std::mutex targets_mutex_;
  Targets targets_;

  // 수신 스레드에서만 씁니다. 표본 시각을 프레임 시각으로 이어 붙이는 기준입니다.
  std::optional<int64_t> last_frame_us_;
  std::chrono::steady_clock::time_point last_steady_;
  double last_t_s_ = 0.0;
};

#endif  // HWLAB_APP_RGB_TRACE_H_
