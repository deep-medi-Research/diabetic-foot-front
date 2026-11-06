#include <QSignalSpy>
#include <QTest>

#include <memory>
#include <vector>

#include "analysis_controller.h"
#include "analysis_variants.h"
#include "remembered_values.h"

namespace {

// 관류 분석 프로그램을 띄우지 않는(Start를 부르지 않는) 판 두 개입니다. 판 순서와 켜고 끄는 규칙만 봅니다.
std::unique_ptr<AnalysisVariants> TwoLanes() {
  std::vector<std::unique_ptr<AnalysisController>> lanes;
  for (const char* id : {"a", "b"}) {
    hwlab::platform::AnalyzerVariant variant;
    variant.id = id;
    variant.label = id;
    lanes.push_back(std::make_unique<AnalysisController>(hwlab::platform::ProcessLaunch{}, variant));
  }
  return std::make_unique<AnalysisVariants>(std::move(lanes), true);
}

}  // namespace

class AnalysisVariantsTest : public QObject {
  Q_OBJECT

 private slots:
  // 녹화 중이면 모두 보류하고, 아니면 앞쪽에 켜 둔 판이 분석 중일 때 뒤쪽 판만 보류합니다.
  void holdsLaterVariantsWhileEarlierRuns() {
    using V = std::vector<bool>;
    QCOMPARE(VariantHolds(true, V{true, true}, V{false, false}), (V{true, true}));
    QCOMPARE(VariantHolds(false, V{true, true}, V{false, false}), (V{false, false}));
    QCOMPARE(VariantHolds(false, V{true, true}, V{true, false}), (V{false, true}));
    QCOMPARE(VariantHolds(false, V{true, true}, V{false, true}), (V{false, false}));
    QCOMPARE(VariantHolds(false, V{true, true, true}, V{true, true, false}), (V{false, true, true}));
    // 꺼 둔 판은 분석 중이어도(끄기 전에 넣은 촬영) 뒤 판을 막지 않습니다.
    QCOMPARE(VariantHolds(false, V{false, true}, V{true, false}), (V{false, false}));
  }

  // 판을 끄고 켤 수 있고, 마지막으로 켜 둔 판은 끄지 않습니다.
  void keepsAtLeastOneVariant() {
    auto variants = TwoLanes();
    QSignalSpy changed(variants.get(), &AnalysisVariants::changed);
    QCOMPARE(variants->Ids(), (QStringList{"a", "b"}));
    QCOMPARE(variants->enabledCount(), 2);
    QVERIFY(variants->setLaneEnabled("a", false));
    QCOMPARE(variants->enabledCount(), 1);
    QVERIFY(!variants->setLaneEnabled("b", false));
    QCOMPARE(variants->enabledCount(), 1);
    QVERIFY(!variants->setLaneEnabled("z", true));
    QVERIFY(variants->setLaneEnabled("a", true));
    QCOMPARE(variants->enabledCount(), 2);
    QVERIFY(changed.count() >= 2);
    QVERIFY(variants->lane("b") != nullptr);
    QVERIFY(variants->lane("z") == nullptr);
    QCOMPARE(variants->LabelOf("z"), QStringLiteral("z"));
  }

  // 기억한 값이 판을 끄고 있으면 그대로 따르고, 모든 판을 끄고 있으면 첫 판을 켭니다.
  void readsRememberedChoice() {
    RememberedValues remembered = RememberedValues::ThisRunOnly();
    remembered.SetValue("analysis/variant/a/enabled", false);
    remembered.SetValue("analysis/prestream", false);
    auto variants = TwoLanes();
    variants->UseRemembered(&remembered);
    QVERIFY(!qobject_cast<AnalysisController*>(variants->lane("a"))->enabled());
    QVERIFY(qobject_cast<AnalysisController*>(variants->lane("b"))->enabled());
    QVERIFY(!variants->prestream());
    variants->setPrestream(true);
    QCOMPARE(remembered.Value("analysis/prestream").toBool(), true);

    remembered.SetValue("analysis/variant/b/enabled", false);
    auto all_off = TwoLanes();
    all_off->UseRemembered(&remembered);
    QCOMPARE(all_off->enabledCount(), 1);
    QVERIFY(qobject_cast<AnalysisController*>(all_off->lane("a"))->enabled());
  }
};

QTEST_GUILESS_MAIN(AnalysisVariantsTest)
#include "test_analysis_variants.moc"
