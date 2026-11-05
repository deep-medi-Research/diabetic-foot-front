#include <QDirIterator>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "capture_controller.h"
#include "hwlab/flow/capture_spec.h"
#include "hwlab/platform/fake_camera.h"
#include "remembered_values.h"
#include "shutdown_watch.h"

namespace {

constexpr int kTargetFrames = 300;  // 가짜 카메라 1ms 간격이면 0.3초

hwlab::flow::CaptureSpec SmallSpec() {
  QFile file(QStringLiteral(HWLAB_SPEC_DIR "/see3cam_manual_30fps.json"));
  file.open(QIODevice::ReadOnly);
  std::string error;
  auto spec = hwlab::flow::ParseCaptureSpec(file.readAll().toStdString(), &error);
  spec->width = 64;
  spec->height = 32;
  spec->target_frames = kTargetFrames;
  spec->codec = hwlab::flow::VideoCodec::kUncompressed;
  return *spec;
}

// 장비 없이 미리보기까지 띄운 촬영 화면입니다. 환자 정보를 채워 촬영 버튼을 누를 수 있습니다.
struct Rig {
  QTemporaryDir out;
  RememberedValues remembered = RememberedValues::ThisRunOnly();
  hwlab::platform::FakeRgbCamera* camera = nullptr;
  std::unique_ptr<CaptureController> controller;

  explicit Rig(int64_t stall_after = -1, int32_t wall_period_us = 1000) {
    hwlab::platform::FakeCameraScript script;
    script.width = 64;
    script.height = 32;
    script.initial = {false, -5, 4, false, 4600, 60, true};
    script.stall_after = stall_after;
    script.wall_period_us = wall_period_us;
    auto fake = std::make_unique<hwlab::platform::FakeRgbCamera>(script);
    camera = fake.get();
    controller = std::make_unique<CaptureController>(std::move(fake), SmallSpec(), out.path(), &remembered, nullptr,
                                                     QStringLiteral("시험"), hwlab::record::ThermalAbsence::kNoDevice,
                                                     hwlab::platform::SavedVideos::kFull, hwlab::record::LightingInfo{});
    controller->shot()->setName(QStringLiteral("시험"));
    controller->shot()->setBirth(QStringLiteral("19600101"));
    controller->shot()->setArmSystolic(QStringLiteral("120"));
    controller->shot()->setArmDiastolic(QStringLiteral("80"));
    controller->shot()->setAnkleSystolic(QStringLiteral("130"));
    controller->shot()->setAnkleDiastolic(QStringLiteral("85"));
    controller->shot()->setAnkleLevel(QStringLiteral("D"));
  }

  bool Shoot() {
    controller->startRecording();
    if (!controller->thermalPrompt().isEmpty()) controller->startWithoutThermal();
    return QTest::qWaitFor([this] { return controller->recording(); }, 5000);
  }

  // 출력 폴더에서 이름이 suffix로 끝나는 파일 수입니다.
  int CountFiles(const QString& suffix) const {
    int n = 0;
    QDirIterator it(out.path(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) n += it.next().endsWith(suffix) ? 1 : 0;
    return n;
  }
};

}  // namespace

class CaptureControllerTest : public QObject {
  Q_OBJECT

 private slots:
  // 녹화하지 않을 때 창을 닫으면 바로 카메라를 정리하고 실행 전 설정으로 되돌립니다.
  void ClosesRightAwayWhenIdle() {
    Rig rig;
    QString error;
    QVERIFY2(rig.controller->Start(&error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(rig.controller->canShoot(), 5000);
    QSignalSpy finished(rig.controller.get(), &CaptureController::shutdownFinished);

    rig.controller->shutdown();

    QVERIFY(!rig.controller->savingBeforeClose());
    QVERIFY(finished.wait(5000));
    QVERIFY(rig.controller->closeReady());
    QVERIFY(rig.controller->restoreReport().isEmpty());
    QCOMPARE(rig.camera->State(), (hwlab::CameraState{false, -5, 4, false, 4600, 60, true}));
  }

  // 녹화 중에 창을 닫으면 녹화를 멈추고 촬영 마무리(사이드카, 매니페스트, 분석 입력 확인)를 끝낸 뒤 카메라를 정리합니다.
  // 마무리를 기다리는 동안은 제한 시간을 걸지 않습니다. 걸면 마무리 시간까지 카메라 정리 20초에 들어갑니다.
  void SavesShotBeforeClosingDuringRecording() {
    Rig rig;
    QString error;
    QVERIFY2(rig.controller->Start(&error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(rig.controller->canShoot(), 5000);
    QVERIFY(rig.Shoot());
    QSignalSpy finished(rig.controller.get(), &CaptureController::shutdownFinished);
    QSignalSpy saved(rig.controller.get(), &CaptureController::shotSaved);

    rig.controller->shutdown();

    QVERIFY(rig.controller->savingBeforeClose());
    bool armed_while_saving = false;
    QVERIFY(QTest::qWaitFor([&] {
      if (rig.controller->savingBeforeClose() && ShutdownLimitArmed()) armed_while_saving = true;
      return !finished.isEmpty();
    }, 20000));
    QVERIFY(!armed_while_saving);
    QCOMPARE(saved.count(), 1);
    QVERIFY(rig.controller->closeReady());
    QCOMPARE(rig.CountFiles(QStringLiteral(".avi")), 1);
    QVERIFY(rig.CountFiles(QStringLiteral(".json")) >= 1);
  }

  // 남은 공간이 10 GB 미만이면 촬영 전에 노란 안내가 보이고 촬영은 막히지 않습니다.
  void WarnsBeforeRecordingWhenSpaceIsLow() {
    Rig rig;
    QString error;
    QVERIFY2(rig.controller->Start(&error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(rig.controller->canShoot(), 5000);
    QSignalSpy changed(rig.controller.get(), &CaptureController::freeSpaceChanged);

    rig.controller->SetFreeBytesOverride(9'200'000'000);

    QCOMPARE(rig.controller->freeSpaceLevel(), QStringLiteral("low"));
    QCOMPARE(rig.controller->freeSpaceText(),
             QStringLiteral("저장 공간이 9.2 GB 남았습니다. 10 GB 미만이면 촬영 중 공간이 부족할 수 있습니다. 다른 위치로 옮기거나 공간을 비워 주세요."));
    QVERIFY(rig.controller->canShoot());
    QCOMPARE(changed.count(), 1);
    QVERIFY(rig.Shoot());
  }

  // 한 번 촬영을 저장할 공간이 없으면 누르기 전에 빨간 이유가 보이고 촬영 버튼이 꺼지며, 눌러도 폴더를 만들지 않습니다.
  void BlocksRecordingWhenSpaceIsShort() {
    Rig rig;
    QString error;
    QVERIFY2(rig.controller->Start(&error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(rig.controller->canShoot(), 5000);

    rig.controller->SetFreeBytesOverride(120'000'000);

    QCOMPARE(rig.controller->freeSpaceLevel(), QStringLiteral("short"));
    QVERIFY(rig.controller->freeSpaceText().startsWith(QStringLiteral("디스크 여유 부족: 남은 120 MB")));
    QVERIFY(!rig.controller->canShoot());
    rig.controller->startRecording();
    QCOMPARE(rig.controller->error(), rig.controller->freeSpaceText());
    QVERIFY(!rig.controller->shotInProgress());
    QCOMPARE(rig.CountFiles(QStringLiteral(".json")), 0);

    rig.controller->SetFreeBytesOverride(50'000'000'000);

    QCOMPARE(rig.controller->freeSpaceLevel(), QString());
    QCOMPARE(rig.controller->freeSpaceText(), QString());
    QVERIFY(rig.controller->canShoot());
  }

  // 녹화 중에 카메라가 프레임을 보내지 않으면 녹화가 끝나지 않습니다. 정한 시간(30초) 뒤에는 마무리 없이 카메라를 정리하고 닫습니다.
  void ClosesWhenRecordingNeverEnds() {
    // 실제 30fps로 보내 안정화(75장)가 끝나고 녹화 중인 150장째에서 멈춥니다.
    Rig rig(/*stall_after=*/150, /*wall_period_us=*/33333);
    QString error;
    QVERIFY2(rig.controller->Start(&error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(rig.controller->canShoot(), 5000);
    QVERIFY(rig.Shoot());
    // 쓴 장 수가 0.5초 동안 그대로이면 카메라가 멈춘 것입니다.
    QVERIFY(QTest::qWaitFor([&] {
      const int before = rig.controller->written();
      QTest::qWait(500);
      return before > 0 && rig.controller->written() == before;
    }, 15000));
    QVERIFY(rig.controller->recording());
    QSignalSpy finished(rig.controller.get(), &CaptureController::shutdownFinished);

    rig.controller->shutdown();

    QVERIFY(finished.wait(45000));
    QVERIFY(rig.controller->closeReady());
    QCOMPARE(rig.camera->Interrupts(), 1);
  }
};

QTEST_MAIN(CaptureControllerTest)
#include "test_capture_controller.moc"
