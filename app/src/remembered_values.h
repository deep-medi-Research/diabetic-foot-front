#ifndef HWLAB_APP_REMEMBERED_VALUES_H_
#define HWLAB_APP_REMEMBERED_VALUES_H_

#include <QHash>
#include <QString>
#include <QVariant>

/**
 * 화면에서 고른 값 중 다음 실행이 이어 쓰는 값(기억한 값)을 읽고 씁니다.
 *
 * 저장 위치, 발 상자 영역, 화면 왼쪽 상자의 발, LED와 PPG 변환기, 카메라 설정이 모두 이 객체를 거칩니다.
 * 다음 실행에 남길지는 만들 때 한 번 정하고, 쓰는 쪽은 남길지 묻지 않고 같은 방법으로 읽고 씁니다.
 *
 * @note 남기는 곳은 QSettings(Windows는 레지스트리 HKCU\Software\deepmedi\hwlab)입니다.
 *       시험 실행은 이번 실행 동안만 메모리에 두어, 시험에서 바꾼 값이 장비 PC의 실제 값을 덮어쓰지 않게 합니다.
 */
class RememberedValues {
 public:
  /** 다음 실행에 남깁니다. QGuiApplication의 조직과 앱 이름을 정한 뒤에 만듭니다. */
  static RememberedValues Saved() { return RememberedValues(true); }
  /** 이번 실행 동안만 둡니다. 처음에는 비어 있어 지난 실행의 값을 읽지 않습니다. */
  static RememberedValues ThisRunOnly() { return RememberedValues(false); }

  /** 다음 실행에 남기는지입니다. */
  bool saved() const { return saved_; }

  /**
   * 기억한 값을 읽습니다.
   *
   * @param key "shot/root"처럼 '/'로 묶은 이름
   * @return 없으면 무효한 QVariant
   */
  QVariant Value(const QString& key) const;

  /** 값을 기억합니다. */
  void SetValue(const QString& key, const QVariant& value);

 private:
  explicit RememberedValues(bool saved) : saved_(saved) {}

  bool saved_;
  QHash<QString, QVariant> this_run_;  // saved_가 false일 때 쓰는 값
};

#endif  // HWLAB_APP_REMEMBERED_VALUES_H_
