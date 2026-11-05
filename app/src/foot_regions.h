#ifndef HWLAB_APP_FOOT_REGIONS_H_
#define HWLAB_APP_FOOT_REGIONS_H_

#include <QObject>
#include <QRect>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <optional>
#include <utility>

#include "hwlab/frame.h"
#include "hwlab/pixel/rect.h"
#include "hwlab/pixel/side_regions.h"

class RememberedValues;

/**
 * 미리보기 한 장 위의 좌우 발 영역 두 개의 ViewModel입니다. RGB와 열화상이 하나씩 갖습니다.
 *
 * 좌표는 프레임 화소 기준이고, 이동과 크기 조절 결과를 core(pixel::ShiftInside, pixel::FitToGrid)로 프레임 안과
 * 색차 격자에 맞춥니다. 녹화에 쓸 수 있는지는 pixel::CheckSideRegions가 정하고 이 클래스는 그 결과를 문장으로 바꿉니다.
 *
 * @note 영역은 화소 기준(index 0이 화소 왼쪽)입니다. 환자의 어느 발인지는 CaptureController가 정하고 SetSideNames로 알려 줍니다.
 */
class FootRegions : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("CaptureController가 만듭니다")

  Q_PROPERTY(int frameWidth READ frameWidth NOTIFY changed)
  Q_PROPERTY(int frameHeight READ frameHeight NOTIFY changed)
  Q_PROPERTY(QRect left READ left NOTIFY changed)
  Q_PROPERTY(QRect right READ right NOTIFY changed)
  Q_PROPERTY(bool leftOk READ leftOk NOTIFY changed)
  Q_PROPERTY(bool rightOk READ rightOk NOTIFY changed)
  Q_PROPERTY(bool ready READ ready NOTIFY changed)
  Q_PROPERTY(QString problem READ problem NOTIFY changed)

 public:
  /**
   * @param settings_key 이 영역을 기억하는 이름(예: "regions/rgb")
   * @param format 프레임 화소 형식. 색차 격자를 정합니다
   * @param default_max_width 기본 영역 하나의 최대 폭. 0이면 프레임 폭의 절반
   * @param remembered_values 지난 영역을 읽고 저장한 영역을 기억합니다. 이 객체보다 오래 있어야 합니다
   */
  FootRegions(QString settings_key, hwlab::PixelFormat format, int32_t default_max_width,
              RememberedValues* remembered_values, QObject* parent = nullptr);

  int frameWidth() const { return frame_width_; }
  int frameHeight() const { return frame_height_; }
  QRect left() const { return ToQRect(regions_.left); }
  QRect right() const { return ToQRect(regions_.right); }
  bool leftOk() const { return left_ok_; }
  bool rightOk() const { return right_ok_; }
  bool ready() const { return frame_width_ > 0 && left_ok_ && right_ok_ && problem_.isEmpty(); }
  QString problem() const { return problem_; }

  /**
   * 프레임 크기를 정합니다. 크기가 바뀌면 저장해 둔 영역(같은 프레임 크기일 때)이나 기본 영역을 씁니다.
   *
   * @param width 프레임 폭
   * @param height 프레임 높이
   */
  void SetFrameSize(int32_t width, int32_t height);

  /**
   * 영역을 직접 정합니다. 명령줄 시험 옵션에 씁니다. 격자 맞춤과 저장은 하지 않고 검사만 합니다.
   *
   * @param regions 화소 왼쪽과 화소 오른쪽 영역
   */
  void SetRegions(const hwlab::pixel::SideRegions& regions);

  /**
   * 문제 문장에 쓸 상자 이름입니다(예: "오른발 상자"). 화소 왼쪽 발이 바뀔 때 부릅니다.
   */
  void SetSideNames(const QString& left, const QString& right);

  /** 지금 영역입니다(화소 왼쪽, 화소 오른쪽). */
  hwlab::pixel::SideRegions Regions() const { return regions_; }

  /** 녹화에 쓸 영역입니다. 프레임 크기를 모르거나 검사를 통과하지 않으면 nullopt입니다. */
  std::optional<std::pair<hwlab::pixel::Rect, hwlab::pixel::Rect>> ReadyPair() const;

  /**
   * 상자를 크기 그대로 옮깁니다. 프레임 밖으로 나가면 안으로 당기고 격자에 맞춥니다.
   *
   * @param side 0이면 화소 왼쪽 상자, 1이면 화소 오른쪽 상자
   * @param x 원하는 왼쪽 위 x(프레임 화소)
   * @param y 원하는 왼쪽 위 y(프레임 화소)
   */
  Q_INVOKABLE void moveBox(int side, int x, int y);

  /**
   * 상자의 모서리 하나를 옮겨 크기를 바꿉니다. 맞은편 모서리는 그대로 둡니다.
   *
   * @param side 0이면 화소 왼쪽 상자, 1이면 화소 오른쪽 상자
   * @param corner 0 왼쪽 위, 1 오른쪽 위, 2 왼쪽 아래, 3 오른쪽 아래
   * @param x 모서리의 새 x(프레임 화소). 프레임 밖이면 경계로 당깁니다
   * @param y 모서리의 새 y(프레임 화소)
   */
  Q_INVOKABLE void resizeBox(int side, int corner, int x, int y);

  /** 지금 영역을 다음 실행에 남깁니다. 끌기를 마칠 때 부릅니다. */
  Q_INVOKABLE void save();

 signals:
  void changed();

 private:
  static QRect ToQRect(const hwlab::pixel::Rect& r) { return QRect(r.x, r.y, r.width, r.height); }
  void Recheck();

  const QString settings_key_;
  const hwlab::PixelFormat format_;
  const int32_t default_max_width_;
  RememberedValues* const remembered_values_;
  int32_t frame_width_ = 0;
  int32_t frame_height_ = 0;
  hwlab::pixel::SideRegions regions_;
  QString left_name_ = QStringLiteral("왼쪽 상자");
  QString right_name_ = QStringLiteral("오른쪽 상자");
  bool left_ok_ = false;
  bool right_ok_ = false;
  QString problem_;
};

#endif  // HWLAB_APP_FOOT_REGIONS_H_
