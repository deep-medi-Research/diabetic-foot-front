#ifndef HWLAB_APP_SHOT_FORM_H_
#define HWLAB_APP_SHOT_FORM_H_

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <vector>

#include "hwlab/record/shot.h"

class RememberedValues;

/**
 * 환자 정보와 저장 위치 입력의 ViewModel입니다.
 *
 * 입력한 글자를 그대로 보관하고, 검사는 core(record::ValidateShot)로 해서 무엇이 잘못됐는지 문장으로 반환합니다.
 * 화면(QML)은 이 값을 그리고 편집한 글자를 set 함수로 전달하기만 합니다.
 *
 * @note 이름, 생년월일, 혈압은 다음 실행에 남기지 않습니다. 앞 환자의 정보로 다음 환자를 찍는 일을 막기 위함입니다(기존 앱과 같음).
 *       저장 위치만 남깁니다. 같은 이유로 이름이나 생년월일을 바꾸면 혈압 칸과 발목 혈압 단계를 비웁니다.
 */
class ShotForm : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("CaptureController가 만듭니다")

  Q_PROPERTY(QString name READ name NOTIFY changed)
  Q_PROPERTY(QString birth READ birth NOTIFY changed)
  Q_PROPERTY(QString armSystolic READ armSystolic NOTIFY changed)
  Q_PROPERTY(QString armDiastolic READ armDiastolic NOTIFY changed)
  Q_PROPERTY(QString ankleSystolic READ ankleSystolic NOTIFY changed)
  Q_PROPERTY(QString ankleDiastolic READ ankleDiastolic NOTIFY changed)
  Q_PROPERTY(QString ankleLevel READ ankleLevel NOTIFY changed)
  Q_PROPERTY(QVariantList ankleLevels READ ankleLevels CONSTANT)
  /** 발목 혈압 단계 버튼 아래의 작은 글자입니다. 고르기 전에는 단계의 뜻을, 고른 뒤에는 계산식과 값(mmHg)을 보입니다. */
  Q_PROPERTY(QString ankleLevelText READ ankleLevelText NOTIFY changed)
  Q_PROPERTY(QString root READ root NOTIFY changed)
  Q_PROPERTY(QUrl rootUrl READ rootUrl NOTIFY changed)
  Q_PROPERTY(QString nameError READ nameError NOTIFY changed)
  Q_PROPERTY(QString birthError READ birthError NOTIFY changed)
  Q_PROPERTY(QString armPressureError READ armPressureError NOTIFY changed)
  Q_PROPERTY(QString anklePressureError READ anklePressureError NOTIFY changed)
  Q_PROPERTY(QString ankleLevelError READ ankleLevelError NOTIFY changed)
  Q_PROPERTY(QString missing READ missing NOTIFY changed)
  /** 잘못 입력해 빨간 글자가 뜬 칸 이름입니다(예: "고칠 것: 상완 혈압"). 없으면 빈 문자열입니다. */
  Q_PROPERTY(QString toFix READ toFix NOTIFY changed)
  Q_PROPERTY(bool ready READ ready NOTIFY changed)
  Q_PROPERTY(QString folderPreview READ folderPreview NOTIFY changed)
  /**
   * 저장 위치에 이미 있는 촬영의 환자 목록입니다. 최근에 찍은 순서이고 항목마다 {name, birth, last}입니다.
   *
   * @note 저장 위치의 환자 폴더(`이름_생년월일`)와 이전 배치의 날짜 폴더(`yyyyMMdd/이름_생년월일`)에서 읽으므로
   *       따로 저장하는 정보가 없습니다.
   */
  Q_PROPERTY(QVariantList recentPatients READ recentPatients NOTIFY recentChanged)

 public:
  /**
   * @param root 처음 저장 위치
   * @param remembered_values 화면에서 고른 저장 위치를 기억합니다. 이 객체보다 오래 있어야 합니다
   */
  ShotForm(QString root, RememberedValues* remembered_values, QObject* parent = nullptr);

  QString name() const { return name_; }
  QString birth() const { return birth_; }
  QString armSystolic() const { return arm_systolic_; }
  QString armDiastolic() const { return arm_diastolic_; }
  QString ankleSystolic() const { return ankle_systolic_; }
  QString ankleDiastolic() const { return ankle_diastolic_; }
  QString ankleLevel() const { return ankle_level_; }
  QVariantList ankleLevels() const;
  QString ankleLevelText() const { return ankle_level_text_; }
  QString root() const;
  QUrl rootUrl() const { return QUrl::fromLocalFile(root_); }
  QString nameError() const { return name_error_; }
  QString birthError() const { return birth_error_; }
  QString armPressureError() const { return arm_pressure_error_; }
  QString anklePressureError() const { return ankle_pressure_error_; }
  QString ankleLevelError() const { return ankle_level_error_; }
  QString missing() const { return missing_; }
  QString toFix() const { return to_fix_; }
  bool ready() const {
    return missing_.isEmpty() && name_error_.isEmpty() && birth_error_.isEmpty() && arm_pressure_error_.isEmpty() &&
           ankle_pressure_error_.isEmpty() && ankle_level_error_.isEmpty();
  }
  QString folderPreview() const { return folder_preview_; }
  QVariantList recentPatients() const { return recent_; }

  /** 최근 환자 하나를 고릅니다. 이름과 생년월일을 함께 채웁니다. */
  Q_INVOKABLE void pickPatient(const QString& name, const QString& birth);

  /** 저장 위치를 다시 읽어 최근 환자 목록을 만듭니다. 촬영을 저장한 뒤와 저장 위치를 바꾼 뒤 부릅니다. */
  Q_INVOKABLE void refreshRecent();

  /** 입력한 이름입니다. 앞뒤 공백은 폴더 이름을 만들 때 뺍니다. */
  Q_INVOKABLE void setName(const QString& text);

  /** 입력한 생년월일입니다. 0000-00-00 형식이고, 숫자 여덟 자리만 주면 형식에 맞춰 둡니다. */
  Q_INVOKABLE void setBirth(const QString& text);

  /** 상완에서 잰 수축기 혈압(mmHg)입니다. 입력한 글자 그대로 받습니다. */
  Q_INVOKABLE void setArmSystolic(const QString& text);

  /** 상완에서 잰 이완기 혈압(mmHg)입니다. */
  Q_INVOKABLE void setArmDiastolic(const QString& text);

  /** 발목에서 잰 수축기 혈압(mmHg)입니다. */
  Q_INVOKABLE void setAnkleSystolic(const QString& text);

  /** 발목에서 잰 이완기 혈압(mmHg)입니다. */
  Q_INVOKABLE void setAnkleDiastolic(const QString& text);

  /** 고른 발목 혈압 단계입니다. record::kAnkleLevels 중 하나이고, 빈 문자열은 고르지 않음입니다. */
  Q_INVOKABLE void setAnkleLevel(const QString& level);

  /** 폴더 선택 창에서 고른 저장 위치입니다. */
  Q_INVOKABLE void setRootUrl(const QUrl& url);

  /** 화면에서 마지막으로 고른 저장 위치입니다. 없으면 빈 문자열입니다. */
  static QString RememberedRoot(const RememberedValues& remembered_values);

  /** 저장 위치(UTF-8)입니다. */
  std::string RootUtf8() const { return root_.toStdString(); }

  /**
   * 지금 입력과 촬영 시각으로 촬영 정보를 만듭니다.
   *
   * @param when 촬영 시각. 한 번만 읽어 넘겨야 자정 앞뒤에서 폴더 날짜와 파일 시각이 갈리지 않습니다
   */
  hwlab::record::ShotInfo InfoAt(const QDateTime& when) const;

 signals:
  void changed();
  void recentChanged();

 private:
  void SetPressureText(QString* field, const QString& text);
  void ClearPressures();
  void Revalidate();

  QString root_;
  RememberedValues* const remembered_values_;
  QVariantList recent_;
  QString name_;
  QString birth_;
  QString arm_systolic_;
  QString arm_diastolic_;
  QString ankle_systolic_;
  QString ankle_diastolic_;
  QString ankle_level_;
  QString ankle_level_text_;
  QString name_error_;
  QString birth_error_;
  QString arm_pressure_error_;
  QString ankle_pressure_error_;
  QString ankle_level_error_;
  QString missing_;
  QString to_fix_;
  QString folder_preview_;
};

#endif  // HWLAB_APP_SHOT_FORM_H_
