#ifndef HWLAB_APP_ANALYSIS_VARIANTS_H_
#define HWLAB_APP_ANALYSIS_VARIANTS_H_

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

#include "analysis_controller.h"

class RememberedValues;

/**
 * 판마다 분석 프로그램을 보류할지 정합니다.
 *
 * @param recording 안정화나 녹화 중이면 true. 이때는 모든 판을 보류합니다
 * @param enabled 판마다 켜 두었는지(판 순서)
 * @param active 판마다 분석 중이거나 대기열에 촬영이 있는지(판 순서)
 * @return 판마다 보류 여부. 앞쪽에 켜 둔 판 중 분석 중이거나 대기 중인 판이 있으면 그 뒤의 판을 보류합니다
 * @note 판을 차례로 돌려, 녹화가 끝난 뒤 첫 판의 결과가 두 판을 함께 돌릴 때보다 먼저 나오게 합니다.
 */
std::vector<bool> VariantHolds(bool recording, const std::vector<bool>& enabled, const std::vector<bool>& active);

/**
 * 분석 판마다 하나씩 띄운 관류 분석(AnalysisController)의 묶음입니다. 촬영 화면의 분석 칸과 판 고르기가 이 객체를 봅니다.
 *
 * - 촬영 하나를 켜 둔 판 모두로 분석합니다. 판 순서대로 분석하고, 앞 판의 분석이 끝나야 다음 판에 보냅니다(VariantHolds)
 * - 녹화 중 미리 분석(첫 장 발 분할, 영상 미리 읽기)은 켜 둔 판 모두에 보냅니다
 * - 켜 둔 판과 미리 분석 스위치는 기억한 값에 남깁니다. 마지막으로 켜 둔 판은 끌 수 없습니다
 */
class AnalysisVariants : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("main에서 판 목록으로 만듭니다")

  /** 판별 관류 분석(AnalysisController) 목록입니다. 판 이름 순입니다. */
  Q_PROPERTY(QList<QObject*> lanes READ lanes CONSTANT)
  /** 켜 둔 판 중 하나라도 분석 중이거나 대기열에 촬영이 있으면 true입니다. 촬영 버튼을 막는 데 씁니다. */
  Q_PROPERTY(bool blocksShot READ blocksShot NOTIFY changed)
  /** 분석 중인 판의 남은 시간 문장("v7 약 5초 남음")입니다. 분석 중인 판이 없으면 비어 있습니다. */
  Q_PROPERTY(QString remainingText READ remainingText NOTIFY changed)
  /** 켜 둔 판 수입니다. */
  Q_PROPERTY(int enabledCount READ enabledCount NOTIFY changed)
  /** 녹화 중 미리 분석을 쓸 수 있는지입니다. `--no-prestream`으로 띄우면 false이고 화면에서 스위치를 숨깁니다. */
  Q_PROPERTY(bool prestreamAvailable READ prestreamAvailable CONSTANT)
  /**
   * 녹화 중 미리 분석 스위치입니다. 켜면 녹화가 시작될 때 켜 둔 판 모두에 첫 장 발 분할과 영상 미리 읽기를 요청합니다.
   * 기본은 켬이고 기억한 값에 남깁니다. 다음 녹화부터 적용합니다.
   *
   * @note 노트북 실측(2026-10-27, 촬영 13건, 판 하나): 녹화가 끝난 뒤 분석 완료까지 5.3~14.7초, 끄면 약 27초입니다.
   */
  Q_PROPERTY(bool prestream READ prestream WRITE setPrestream NOTIFY prestreamChanged)

 public:
  /**
   * @param lanes 판마다 하나씩 만든 관류 분석. 이 객체가 가집니다. 판 이름 순으로 줍니다
   * @param prestream_available 관류 분석 프로그램을 `--presegment-threads`, `--prestream`과 함께 띄웠으면 true
   */
  AnalysisVariants(std::vector<std::unique_ptr<AnalysisController>> lanes, bool prestream_available,
                   QObject* parent = nullptr);
  ~AnalysisVariants() override;

  /**
   * 켜 둔 판과 미리 분석 스위치를 기억한 값에서 읽고, 바뀌면 남깁니다. 기억한 값이 없으면 모든 판과 스위치를 켭니다.
   *
   * @param remembered 값을 읽고 쓰는 곳. 이 객체보다 오래 살아야 합니다. nullptr이면 기억하지 않습니다
   */
  void UseRemembered(RememberedValues* remembered);

  /** 판마다 관류 분석 프로그램을 띄웁니다. */
  void Start();
  /** 앱을 닫을 때 부릅니다. 판마다 진행 중인 분석과 대기열을 취소하고 관류 분석 프로그램을 끝냅니다. */
  void Shutdown();

  QList<QObject*> lanes() const;
  bool blocksShot() const;
  QString remainingText() const;
  int enabledCount() const;
  bool prestreamAvailable() const { return prestream_available_; }
  bool prestream() const { return prestream_; }
  void setPrestream(bool on);

  /**
   * 판을 켜거나 끕니다. 마지막으로 켜 둔 판은 끄지 않습니다.
   *
   * @param id 판 이름
   * @param on 켤지
   * @return 바꿨으면 true
   */
  Q_INVOKABLE bool setLaneEnabled(const QString& id, bool on);
  /** 판 이름으로 찾은 관류 분석입니다. 없으면 nullptr입니다. */
  Q_INVOKABLE QObject* lane(const QString& id) const;
  /** 판 이름 목록(판 순서)입니다. */
  QStringList Ids() const;
  /** 판 이름의 화면 이름입니다. 없으면 id를 그대로 돌려줍니다. */
  QString LabelOf(const QString& id) const;

  /** 이 촬영 폴더를 켜 둔 판 모두가 분석했는지입니다. 결과 창을 "분석 중"으로 열지 정할 때 씁니다. */
  bool Finished(const QString& folder) const;

 public slots:
  /** 안정화와 녹화 중에는 true. 판 모두에 새 요청을 보내지 않습니다. */
  void setHold(bool hold);
  /** AnalysisController::onRgbSaved를 켜 둔 판 모두에 넘깁니다. 뒤쪽 판은 앞 판이 끝날 때까지 보류합니다. */
  void onRgbSaved(const QString& folder, bool ready);
  /** AnalysisController::onShotSaved를 켜 둔 판 모두에 넘깁니다. */
  void onShotSaved(const QString& folder, bool ready);
  /** AnalysisController::onFirstFramesWritten을 켜 둔 판 모두에 넘깁니다. */
  void onFirstFramesWritten(const QString& left_video, const QString& right_video);

 signals:
  void changed();
  void prestreamChanged();
  /**
   * 판 하나가 촬영 하나의 분석을 끝냈습니다.
   *
   * @param folder 촬영 폴더
   * @param ok 성공이면 true
   * @param summary_path 요약 JSON 경로
   * @param variant 판 이름
   */
  void finished(const QString& folder, bool ok, const QString& summary_path, const QString& variant);
  /** 판 하나의 그림과 관류 지도 GIF를 다 그렸습니다(AnalysisController::figuresReady). */
  void figuresReady(const QString& folder, const QString& variant);
  /**
   * 판 하나의 분석결과 폴더 복사가 끝났습니다.
   *
   * @param folder 촬영 폴더
   * @param text 이 촬영의 판별 복사 결과를 판 이름과 함께 이은 문장
   */
  void collected(const QString& folder, const QString& text);

 private:
  // 앞 판이 분석 중이면 뒤 판을 보류합니다. pending이면 아직 넣지 않은 촬영을 앞 판이 곧 받는다고 봅니다.
  void UpdateHolds(bool pending = false);
  // 화면 스레드에서 관류 분석 프로그램 상태를 읽으므로, 판의 changed 신호마다 부릅니다.
  void OnLaneChanged();

  std::vector<std::unique_ptr<AnalysisController>> lanes_;
  const bool prestream_available_;
  bool prestream_ = true;
  bool recording_ = false;
  RememberedValues* remembered_ = nullptr;
  // 마지막 촬영의 판별 복사 문장입니다. 촬영이 바뀌면 비웁니다.
  QString collect_folder_;
  QMap<QString, QString> collect_texts_;
};

#endif  // HWLAB_APP_ANALYSIS_VARIANTS_H_
