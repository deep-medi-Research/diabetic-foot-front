#include <QTest>

#include <optional>

#include "hwlab/platform/thermal_camera.h"
#include "thermal_view.h"

namespace {

using hwlab::platform::CameraTemperature;
using hwlab::platform::LensShadingStatus;
using hwlab::platform::ThermalConnection;
using hwlab::platform::ThermalStatus;
using hwlab::record::LensShadingSource;
using hwlab::timing::LensShadingConditions;
using hwlab::timing::WarmupPhase;

constexpr int64_t kMinuteUs = 60'000'000;

// 데워진 카메라(FPA 98.21, 셔터 38.19 °C)를 받는 중인 상태입니다.
ThermalStatus Receiving(hwlab::timing::WarmupStatus warmup) {
  ThermalStatus status;
  status.attached = true;
  status.connection = ThermalConnection::kReceiving;
  status.received = 1;
  status.camera_temperature = CameraTemperature{98.21F, 22545, 38.19};
  status.warmup = warmup;
  return status;
}

LensShadingStatus Loaded(std::optional<LensShadingConditions> made) {
  return {{LensShadingSource::kLoaded, 1762400000}, std::string(), made};
}

}  // namespace

class ThermalViewTest : public QObject {
  Q_OBJECT

 private slots:
  void showsWarmupOnlyWhileReceivingWithTemperature() {
    ThermalView view(true, QString(), 0);
    ThermalStatus status = Receiving({WarmupPhase::kMeasuring, std::nullopt, 4 * kMinuteUs + 1});
    view.Update(status);
    QCOMPARE(view.warmup(), QStringLiteral("measuring"));
    QCOMPARE(view.warmupText(), QStringLiteral("예열 확인 중"));
    QCOMPARE(view.warmupDetail(), QStringLiteral("5분 뒤 판정"));

    status.camera_temperature.reset();
    ++status.received;
    view.Update(status);
    QCOMPARE(view.warmup(), QString());
    QCOMPARE(view.warmupDetail(), QString());

    status = Receiving({WarmupPhase::kReady, 0.0, 0});
    status.connection = ThermalConnection::kLost;
    view.Update(status);
    QCOMPARE(view.warmup(), QString());
  }

  void showsRiseRateAsShutterCelsius() {
    ThermalView view(true, QString(), 0);
    // FPA 값 분당 0.1은 셔터 온도 분당 0.146 °C입니다(FPA 값 1당 1.46 °C).
    view.Update(Receiving({WarmupPhase::kWarming, 0.1, 0}));
    QCOMPARE(view.warmupText(), QStringLiteral("예열 중"));
    QCOMPARE(view.warmupDetail(), QStringLiteral("지난 10분 분당 +0.15 °C"));

    view.Update(Receiving({WarmupPhase::kSettling, 0.02, 6 * kMinuteUs}));
    QCOMPARE(view.warmupText(), QStringLiteral("예열 마무리 중"));
    QCOMPARE(view.warmupDetail(), QStringLiteral("지난 10분 분당 +0.03 °C, 6분 뒤 완료"));
  }

  void asksRecalibrationForWarmingOrMovedCalibration() {
    ThermalView view(true, QString(), 0);
    ThermalStatus status = Receiving({WarmupPhase::kReady, 0.0, 0});

    status.lens_shading = Loaded(LensShadingConditions{35.0, 95.0, WarmupPhase::kSettling});
    view.Update(status);
    QCOMPARE(view.lensFit(), QStringLiteral("made-warming"));
    QVERIFY(view.lensRecalibrate());

    // 지금 FPA 98.21에서 0.6 낮은 97.61은 셔터 0.88 °C 차이로 기준 0.7 °C를 넘습니다.
    status.lens_shading = Loaded(LensShadingConditions{37.3, 97.61, WarmupPhase::kReady});
    ++status.received;
    view.Update(status);
    QCOMPARE(view.lensFit(), QStringLiteral("moved"));
    QCOMPARE(view.lensFitText(), QStringLiteral("보정 때보다 0.88 °C 다름, 다시 보정"));
    QVERIFY(view.lensRecalibrate());

    status.lens_shading = Loaded(LensShadingConditions{38.0, 98.0, WarmupPhase::kReady});
    ++status.received;
    view.Update(status);
    QCOMPARE(view.lensFit(), QStringLiteral("fits"));
    QCOMPARE(view.lensFitText(), QString());
    QVERIFY(!view.lensRecalibrate());

    status.lens_shading = Loaded(std::nullopt);
    ++status.received;
    view.Update(status);
    QCOMPARE(view.lensFitText(), QStringLiteral("보정 때 온도 기록 없음"));
    QVERIFY(!view.lensRecalibrate());
  }

  void waitsForWarmupBeforeJudgingCalibration() {
    ThermalView view(true, QString(), 0);
    ThermalStatus status = Receiving({WarmupPhase::kWarming, 0.1, 0});
    status.lens_shading = Loaded(LensShadingConditions{37.3, 97.0, WarmupPhase::kReady});
    view.Update(status);
    QCOMPARE(view.lensFit(), QStringLiteral("wait-warmup"));
    QVERIFY(!view.lensRecalibrate());

    status.lens_shading = LensShadingStatus{{LensShadingSource::kNone, std::nullopt}, std::string(), std::nullopt};
    ++status.received;
    view.Update(status);
    QCOMPARE(view.lensFit(), QString());
  }
};

QTEST_GUILESS_MAIN(ThermalViewTest)
#include "test_thermal_view.moc"
