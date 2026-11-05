#include <QSignalSpy>
#include <QTest>

#include <chrono>
#include <cstdint>
#include <vector>

#include "rgb_trace.h"

namespace {

constexpr int32_t kWidth = 64;
constexpr int32_t kHeight = 32;
constexpr int64_t kPreviewPeriodUs = 100'000;  // 미리보기 10장/초

// 모든 화소의 밝기가 y이고 색차가 없는(회색) UYVY 프레임입니다.
class GrayFrame {
 public:
  GrayFrame(uint8_t y, int64_t time_us) : bytes_(static_cast<size_t>(kWidth) * kHeight * 2) {
    for (size_t i = 0; i < bytes_.size(); i += 2) {
      bytes_[i] = 128;
      bytes_[i + 1] = y;
    }
    view_.format = hwlab::PixelFormat::kUyvy;
    view_.width = kWidth;
    view_.height = kHeight;
    view_.plane[0] = bytes_.data();
    view_.row_stride[0] = kWidth * 2;
    view_.time.us = time_us;
  }
  const hwlab::FrameView& view() const { return view_; }

 private:
  std::vector<uint8_t> bytes_;
  hwlab::FrameView view_;
};

const hwlab::pixel::SideRegions kBoxes{{0, 0, 32, 32}, {32, 0, 32, 32}};

// 수신 스레드 대신 시험 스레드에서 재고, 화면 스레드로 보낸 표본을 받아 둡니다.
void MeasureAndDeliver(RgbTrace& trace, uint8_t y, int64_t time_us) {
  trace.Measure(GrayFrame(y, time_us).view());
  QCoreApplication::processEvents();
}

std::vector<double> Times(const QVariantMap& series) {
  std::vector<double> out;
  for (const QVariant& t : series.value(QStringLiteral("t")).toList()) out.push_back(t.toDouble());
  return out;
}

}  // namespace

class RgbTraceTest : public QObject {
  Q_OBJECT

 private slots:
  // 구간 안 표본에 양쪽 바깥 표본을 하나씩 더 넣어 선이 구간 끝에서 끊기지 않게 합니다.
  void SeriesAddsOneSampleBeyondEachEnd() {
    RgbTrace trace;
    trace.SetBoxes(kBoxes, kWidth, kHeight);
    for (int i = 0; i <= 10; ++i) MeasureAndDeliver(trace, 126, i * kPreviewPeriodUs);

    const std::vector<double> t = Times(trace.series(0, 0.35, 0.65));
    QCOMPARE(t.size(), size_t{5});
    QVERIFY(qAbs(t.front() - 0.3) < 1e-9);
    QVERIFY(qAbs(t.back() - 0.7) < 1e-9);
    QCOMPARE(trace.startS(), 0.0);
    QVERIFY(qAbs(trace.endS() - 1.0) < 1e-9);
  }

  // 구간이 기록 밖이면 가장 가까운 끝 표본 하나만 돌려줍니다. 없는 대상은 빈 배열입니다.
  void SeriesOutsideHistoryKeepsNearestSample() {
    RgbTrace trace;
    trace.SetBoxes(kBoxes, kWidth, kHeight);
    for (int i = 0; i <= 10; ++i) MeasureAndDeliver(trace, 126, i * kPreviewPeriodUs);

    const std::vector<double> after = Times(trace.series(0, 5.0, 6.0));
    QCOMPARE(after.size(), size_t{1});
    QVERIFY(qAbs(after[0] - 1.0) < 1e-9);
    const std::vector<double> before = Times(trace.series(0, -2.0, -1.0));
    QCOMPARE(before.size(), size_t{1});
    QCOMPARE(before[0], 0.0);
    QVERIFY(Times(trace.series(2, 0.0, 1.0)).empty());
    QVERIFY(Times(trace.series(3, 0.0, 1.0)).empty());
  }

  // 관찰 영역을 바꾸기 전에 잰 표본이 바꾼 뒤에 와도 새 영역의 기록에 넣지 않습니다.
  void AreaChangeDropsSamplesMeasuredBefore() {
    RgbTrace trace;
    trace.SetBoxes(kBoxes, kWidth, kHeight);
    trace.setArea(0, 0, 8, 8);
    trace.Measure(GrayFrame(126, 0).view());
    trace.setArea(16, 8, 8, 8);
    QCoreApplication::processEvents();

    QVERIFY(!trace.latest(2).value(QStringLiteral("valid")).toBool());
    QVERIFY(trace.latest(0).value(QStringLiteral("valid")).toBool());

    MeasureAndDeliver(trace, 126, kPreviewPeriodUs);
    QVERIFY(trace.latest(2).value(QStringLiteral("valid")).toBool());
    QCOMPARE(Times(trace.series(2, 0.0, 1.0)).size(), size_t{1});
  }

  // 발 상자를 옮기면 그 상자의 기록을 지우고, 옮기기 전 위치로 잰 표본도 버립니다. 옮기지 않은 상자의 기록은 남깁니다.
  void MovingBoxResetsOnlyThatBox() {
    RgbTrace trace;
    trace.SetBoxes(kBoxes, kWidth, kHeight);
    MeasureAndDeliver(trace, 126, 0);
    QSignalSpy sampled(&trace, &RgbTrace::sampled);

    trace.Measure(GrayFrame(126, kPreviewPeriodUs).view());
    hwlab::pixel::SideRegions moved = kBoxes;
    moved.left = {0, 0, 16, 16};
    trace.SetBoxes(moved, kWidth, kHeight);
    QCOMPARE(sampled.count(), 1);
    QCoreApplication::processEvents();

    QVERIFY(!trace.latest(0).value(QStringLiteral("valid")).toBool());
    QCOMPARE(Times(trace.series(1, 0.0, 1.0)).size(), size_t{2});

    MeasureAndDeliver(trace, 126, 2 * kPreviewPeriodUs);
    QCOMPARE(Times(trace.series(0, 0.0, 1.0)).size(), size_t{1});
    QCOMPARE(Times(trace.series(1, 0.0, 1.0)).size(), size_t{3});

    // 같은 위치로 다시 불리면 기록을 지우지 않습니다.
    trace.SetBoxes(moved, kWidth, kHeight);
    QCOMPARE(Times(trace.series(0, 0.0, 1.0)).size(), size_t{1});
  }

  // 새 표본이 stale_after 넘게 오지 않으면 stale이 되고, 다음 표본이 오면 풀립니다. 마지막 표본은 그대로 읽힙니다.
  void BecomesStaleWithoutNewSamples() {
    RgbTrace trace(nullptr, std::chrono::milliseconds(50));
    trace.SetBoxes(kBoxes, kWidth, kHeight);
    QSignalSpy changed(&trace, &RgbTrace::staleChanged);
    QVERIFY(!trace.stale());

    MeasureAndDeliver(trace, 126, 0);
    QVERIFY(!trace.stale());
    QTRY_VERIFY_WITH_TIMEOUT(trace.stale(), 2000);
    QCOMPARE(changed.count(), 1);
    QVERIFY(trace.latest(0).value(QStringLiteral("valid")).toBool());

    MeasureAndDeliver(trace, 126, kPreviewPeriodUs);
    QVERIFY(!trace.stale());
    QCOMPARE(changed.count(), 2);
  }

  // 표본을 받은 적이 없으면 stale이 되지 않습니다. 표시할 값이 없어 "-"로 보이기 때문입니다.
  void NeverStaleBeforeFirstSample() {
    RgbTrace trace(nullptr, std::chrono::milliseconds(20));
    QTest::qWait(80);
    QVERIFY(!trace.stale());
  }
};

QTEST_GUILESS_MAIN(RgbTraceTest)
#include "test_rgb_trace.moc"
