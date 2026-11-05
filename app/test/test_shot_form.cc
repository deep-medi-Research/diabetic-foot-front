#include <QDateTime>
#include <QTemporaryDir>
#include <QTest>

#include "remembered_values.h"
#include "shot_form.h"

namespace {

// 이름과 생년월일을 채운 입력입니다. 혈압 칸과 발목 혈압 단계는 비어 있습니다.
struct Rig {
  QTemporaryDir root;
  RememberedValues remembered = RememberedValues::ThisRunOnly();
  ShotForm form{root.path(), &remembered};

  Rig() {
    form.setName(QStringLiteral("시험"));
    form.setBirth(QStringLiteral("19600101"));
  }

  void FillPressures() {
    form.setArmSystolic(QStringLiteral("120"));
    form.setArmDiastolic(QStringLiteral("80"));
    form.setAnkleSystolic(QStringLiteral("135"));
    form.setAnkleDiastolic(QStringLiteral("84"));
    form.setAnkleLevel(QStringLiteral("D"));
  }

  bool PressuresEmpty() const {
    return form.armSystolic().isEmpty() && form.armDiastolic().isEmpty() && form.ankleSystolic().isEmpty() &&
           form.ankleDiastolic().isEmpty() && form.ankleLevel().isEmpty();
  }
};

}  // namespace

class ShotFormTest : public QObject {
  Q_OBJECT

 private slots:
  // 상완과 발목 혈압, 발목 혈압 단계를 비우면 입력할 것에 나오고 촬영할 수 없습니다. 채우면 촬영 정보에 들어갑니다.
  void RequiresPressuresAndAnkleLevel() {
    Rig rig;
    QCOMPARE(rig.form.missing(), QStringLiteral("입력할 것: 상완 혈압, 발목 혈압, 발목 혈압 단계"));
    QVERIFY(!rig.form.ready());
    QVERIFY(rig.form.folderPreview().isEmpty());

    rig.FillPressures();
    rig.form.setAnkleDiastolic(QStringLiteral("84 "));  // 입력 마스크가 비운 자리의 공백

    QVERIFY(rig.form.ready());
    const auto info = rig.form.InfoAt(QDateTime(QDate(2026, 10, 29), QTime(13, 0, 0)));
    QCOMPARE(info.arm_pressure.systolic_mmhg, 120);
    QCOMPARE(info.arm_pressure.diastolic_mmhg, 80);
    QCOMPARE(info.ankle_pressure.systolic_mmhg, 135);
    QCOMPARE(info.ankle_pressure.diastolic_mmhg, 84);
    QCOMPARE(QString::fromStdString(info.ankle_level), QStringLiteral("D"));
    QCOMPARE(rig.form.folderPreview(), QStringLiteral("시험_19600101/D/날짜_시각"));
  }

  // 한 자리는 입력하는 중으로 보고 입력할 것에 넣습니다. 수축기가 이완기보다 크지 않거나 범위 밖이면 이유를 보입니다.
  void ExplainsBadPressure() {
    Rig rig;
    rig.FillPressures();

    rig.form.setArmDiastolic(QStringLiteral("8"));
    QCOMPARE(rig.form.missing(), QStringLiteral("입력할 것: 상완 이완기"));
    QVERIFY(rig.form.armPressureError().isEmpty());

    rig.form.setArmDiastolic(QStringLiteral("130"));
    QCOMPARE(rig.form.armPressureError(), QStringLiteral("수축기가 이완기보다 커야 합니다"));
    QVERIFY(rig.form.missing().isEmpty());
    QVERIFY(!rig.form.ready());

    rig.form.setArmSystolic(QStringLiteral("310"));
    QCOMPARE(rig.form.armPressureError(), QStringLiteral("혈압은 20~300mmHg입니다"));
    QVERIFY(rig.form.anklePressureError().isEmpty());

    rig.form.setAnkleSystolic(QStringLiteral("1a0"));
    QCOMPARE(rig.form.anklePressureError(), QStringLiteral("혈압은 숫자로 씁니다"));
  }

  // 고르기 전에는 단계의 뜻을, 고르면 발목 혈압으로 계산한 값을 버튼 아래에 보입니다.
  void ShowsAnkleLevelValue() {
    Rig rig;
    QCOMPARE(rig.form.ankleLevels(), QVariantList({QStringLiteral("0"), QStringLiteral("V"), QStringLiteral("D"),
                                                   QStringLiteral("M"), QStringLiteral("S")}));
    QCOMPARE(rig.form.ankleLevelText(), QStringLiteral("V 이완기 - 50, D 이완기, M (수축기 + 이완기) / 2, S 수축기"));

    rig.form.setAnkleLevel(QStringLiteral("M"));
    QCOMPARE(rig.form.ankleLevelText(), QStringLiteral("발목 혈압을 입력하면 계산합니다"));
    rig.form.setAnkleLevel(QStringLiteral("0"));
    QCOMPARE(rig.form.ankleLevelText(), QStringLiteral("0 mmHg"));

    rig.FillPressures();
    QCOMPARE(rig.form.ankleLevelText(), QStringLiteral("발목 이완기 = 84 mmHg"));
    rig.form.setAnkleLevel(QStringLiteral("V"));
    QCOMPARE(rig.form.ankleLevelText(), QStringLiteral("발목 이완기 - 50 = 34 mmHg"));
    rig.form.setAnkleLevel(QStringLiteral("M"));
    QCOMPARE(rig.form.ankleLevelText(), QStringLiteral("(발목 수축기 + 이완기) / 2 = 109.5 mmHg"));
    rig.form.setAnkleLevel(QStringLiteral("S"));
    QCOMPARE(rig.form.ankleLevelText(), QStringLiteral("발목 수축기 = 135 mmHg"));
    QVERIFY(rig.form.ready());
  }

  // 발목 이완기가 50mmHg보다 낮아 v가 음수이면 이유를 보이고 촬영할 수 없습니다.
  void RejectsNegativeAnkleLevel() {
    Rig rig;
    rig.FillPressures();
    rig.form.setAnkleDiastolic(QStringLiteral("45"));
    rig.form.setAnkleLevel(QStringLiteral("V"));

    QCOMPARE(rig.form.ankleLevelError(), QStringLiteral("V는 발목 이완기가 50mmHg 이상일 때 고를 수 있습니다"));
    QVERIFY(rig.form.ankleLevelText().isEmpty());
    QVERIFY(!rig.form.ready());

    rig.form.setAnkleLevel(QStringLiteral("D"));
    QVERIFY(rig.form.ankleLevelError().isEmpty());
    QVERIFY(rig.form.ready());
  }

  // 빨간 글자가 뜬 칸은 고칠 것에 모여 촬영 버튼 위에 보입니다. 빈 칸은 입력할 것에만 나옵니다.
  void ListsFieldsToFix() {
    Rig rig;
    QVERIFY(rig.form.toFix().isEmpty());

    rig.FillPressures();
    rig.form.setArmDiastolic(QStringLiteral("130"));
    QCOMPARE(rig.form.toFix(), QStringLiteral("고칠 것: 상완 혈압"));

    rig.form.setAnkleDiastolic(QStringLiteral("45"));
    rig.form.setAnkleLevel(QStringLiteral("V"));
    QCOMPARE(rig.form.toFix(), QStringLiteral("고칠 것: 상완 혈압, 발목 혈압 단계"));
    QVERIFY(rig.form.missing().isEmpty());

    rig.form.setArmDiastolic(QStringLiteral("80"));
    rig.form.setAnkleLevel(QStringLiteral("D"));
    QVERIFY(rig.form.toFix().isEmpty());
    QVERIFY(rig.form.ready());

    rig.form.setName(QStringLiteral("시험/"));
    QCOMPARE(rig.form.toFix(), QStringLiteral("고칠 것: 이름"));
    QCOMPARE(rig.form.missing(), QStringLiteral("입력할 것: 상완 혈압, 발목 혈압, 발목 혈압 단계"));
  }

  // 이름이나 생년월일을 바꾸면 혈압 칸과 발목 혈압 단계를 비웁니다. 같은 환자를 다시 고르거나 이름 앞뒤 공백만 바꾸면 그대로 둡니다.
  void ClearsPressureWhenPatientChanges() {
    Rig rig;
    rig.FillPressures();

    rig.form.pickPatient(QStringLiteral("시험"), QStringLiteral("1960-01-01"));
    rig.form.setName(QStringLiteral("시험 "));
    QCOMPARE(rig.form.armSystolic(), QStringLiteral("120"));
    QCOMPARE(rig.form.ankleLevel(), QStringLiteral("D"));
    QVERIFY(rig.form.ready());

    rig.form.setName(QStringLiteral("시험2"));
    QVERIFY(rig.PressuresEmpty());
    QCOMPARE(rig.form.missing(), QStringLiteral("입력할 것: 상완 혈압, 발목 혈압, 발목 혈압 단계"));

    rig.FillPressures();
    rig.form.setBirth(QStringLiteral("19600102"));
    QVERIFY(rig.PressuresEmpty());

    rig.FillPressures();
    rig.form.pickPatient(QStringLiteral("김철수"), QStringLiteral("1955-11-03"));
    QVERIFY(rig.PressuresEmpty());
  }
};

QTEST_GUILESS_MAIN(ShotFormTest)
#include "test_shot_form.moc"
