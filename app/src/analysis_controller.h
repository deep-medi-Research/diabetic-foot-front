#ifndef HWLAB_APP_ANALYSIS_CONTROLLER_H_
#define HWLAB_APP_ANALYSIS_CONTROLLER_H_

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QElapsedTimer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>
#include <string>

#include "hwlab/platform/analyzer.h"

/**
 * 관류 분석 막대의 ViewModel입니다. 분석 판 하나(AnalyzerVariant)의 관류 분석 프로그램 하나를 맡습니다.
 *
 * 촬영이 저장되면(CaptureController::shotSaved) 분석 입력 확인을 통과한 촬영을 관류 분석 프로그램 대기열에 넣고,
 * 관류 분석 프로그램 상태를 0.2초마다 읽어 화면 문장으로 바꿉니다. 관류 분석 프로그램 파일이 없으면 분석만 하지 않고 촬영은 막지 않습니다.
 * 분석이 성공하면 결과 파일을 저장 위치의 분석결과 폴더로 복사하고(화면 스레드 밖), 그 결과도 막대에 보입니다.
 * 결과는 촬영 폴더의 `perfusion/<판>`에 쓰고, 분석결과 폴더에는 이름 앞부분 뒤에 `_<판>`을 붙여 복사합니다.
 * 판 여러 개의 순서와 사용 여부는 AnalysisVariants가 정합니다.
 */
class AnalysisController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("main에서 관류 분석 프로그램 실행 정보로 만듭니다")

  /** "missing", "starting", "idle", "waiting", "busy", "done", "cancelled", "failed", "skipped" 중 하나입니다. */
  Q_PROPERTY(QString state READ state NOTIFY changed)
  Q_PROPERTY(QString title READ title NOTIFY changed)
  Q_PROPERTY(QString detail READ detail NOTIFY changed)
  Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY changed)
  Q_PROPERTY(int step READ step NOTIFY changed)
  Q_PROPERTY(int total READ total NOTIFY changed)
  /** 분석 중일 때 지금 단계의 이름(한국어)입니다. 단계 수가 5가 아닌 판이면 그 판이 찍은 문구를 그대로 씁니다. */
  Q_PROPERTY(QString stageText READ stageText NOTIFY changed)
  /** 분석 중일 때 전체 진행 비율(0~1)입니다. 단계별 실측 시간 비율과 하위 단계로 나눕니다. */
  Q_PROPERTY(double progress READ progress NOTIFY changed)
  /** 분석 중일 때 남은 시간 예상 문장("약 20초 남음")입니다. 진행 비율이 낮아 예상할 수 없으면 비어 있습니다. */
  Q_PROPERTY(QString remainingText READ remainingText NOTIFY changed)
  Q_PROPERTY(int queued READ queued NOTIFY changed)
  Q_PROPERTY(bool canRetry READ canRetry NOTIFY changed)
  Q_PROPERTY(bool canCancel READ canCancel NOTIFY changed)
  Q_PROPERTY(QString summaryPath READ summaryPath NOTIFY changed)
  /** 마지막으로 끝난 분석의 측정 품질 상태(요약 `qc.status`)입니다. "ok", "check", "retake"이고 qc가 없으면 빈 문자열입니다. */
  Q_PROPERTY(QString qualityStatus READ qualityStatus NOTIFY changed)
  /** 측정 품질 문장입니다. 예: "재측정 필요: 양말을 신고 5~10분 기다려 발을 따뜻하게 한 뒤 재촬영하세요." */
  Q_PROPERTY(QString qualityText READ qualityText NOTIFY changed)
  /** 분석결과 폴더 복사 상태입니다. "", "copying", "done", "failed" 중 하나입니다. */
  Q_PROPERTY(QString collectState READ collectState NOTIFY collectChanged)
  /** 복사 결과 문장입니다. 복사한 파일 수, 재분석 번호, 실패 이유를 담습니다. */
  Q_PROPERTY(QString collectText READ collectText NOTIFY collectChanged)
  /** 분석결과 폴더 경로(OS 구분자)입니다. 복사를 시작하기 전에는 비어 있습니다. */
  Q_PROPERTY(QString collectFolder READ collectFolder NOTIFY collectChanged)
  /** 판 이름(AnalyzerVariant::id)입니다. */
  Q_PROPERTY(QString variantId READ variantId CONSTANT)
  /** 화면에 보이는 판 이름입니다. */
  Q_PROPERTY(QString variantLabel READ variantLabel CONSTANT)
  /** 판 한 줄 설명입니다. */
  Q_PROPERTY(QString variantSummary READ variantSummary CONSTANT)
  /** 판의 소스 커밋(앞 7자리)과 날짜입니다. 없으면 빈 문자열입니다. 판 고르기 줄에 보입니다. */
  Q_PROPERTY(QString variantSource READ variantSource CONSTANT)
  /** 이 판으로 분석할지입니다. false이면 새 촬영을 넣지 않고 녹화 중 미리 분석도 보내지 않습니다. */
  Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)

 public:
  /**
   * @param launch 이 판을 올리는 관류 분석 프로그램 실행 정보(platform::AnalyzerVariantLaunch의 결과)
   * @param variant 판 정보. id가 비면 판을 나누지 않은 결과 폴더(`perfusion`)를 씁니다
   * @param timing 관류 분석 프로그램 시간 값. 시험에서만 바꿉니다
   */
  explicit AnalysisController(hwlab::platform::ProcessLaunch launch, hwlab::platform::AnalyzerVariant variant = {},
                              hwlab::platform::AnalyzerTiming timing = {}, QObject* parent = nullptr);
  ~AnalysisController() override;

  /**
   * 관류 분석 프로그램 파일을 확인하고 관류 분석 프로그램을 띄운 뒤 0.2초마다 상태를 읽기 시작합니다.
   *
   * @note 파일이 없으면 띄우지 않고 "관류 분석 프로그램 없음"을 보입니다. 다시 분석을 누르면 파일을 다시 확인합니다.
   */
  void Start();

  /**
   * 녹화 중 미리 분석을 요청할지 정합니다. 다음 녹화부터 적용합니다.
   *
   * @note 관류 분석 프로그램을 `--presegment-threads`, `--prestream`과 함께 띄웠을 때만 켭니다(AnalysisVariants).
   */
  void SetPrestream(bool on) { prestream_ = on; }

  /** 이 판으로 분석할지 정합니다. 이미 대기열에 넣은 촬영과 분석 중인 촬영은 그대로 끝냅니다. */
  void SetEnabled(bool on);

  const hwlab::platform::AnalyzerVariant& Variant() const { return variant_; }
  QString variantId() const { return QString::fromStdString(variant_.id); }
  QString variantLabel() const { return QString::fromStdString(variant_.label); }
  QString variantSummary() const { return QString::fromStdString(variant_.summary); }
  QString variantSource() const;
  bool enabled() const { return enabled_; }
  /** 분석 중이거나 대기열에 촬영이 있으면 true입니다. 다음 판의 분석을 미루는 데 씁니다(AnalysisVariants). */
  bool Active() const;

  QString state() const { return state_; }
  QString title() const { return title_; }
  QString detail() const { return detail_; }
  QString elapsedText() const { return elapsed_text_; }
  int step() const { return step_; }
  int total() const { return total_; }
  QString stageText() const { return stage_text_; }
  double progress() const { return progress_; }
  QString remainingText() const { return remaining_text_; }
  int queued() const { return queued_; }
  bool canRetry() const { return can_retry_; }
  bool canCancel() const { return can_cancel_; }
  QString summaryPath() const { return summary_path_; }
  QString qualityStatus() const { return event_ && event_->state == QStringLiteral("done") ? event_->quality_status : QString(); }
  QString qualityText() const { return event_ && event_->state == QStringLiteral("done") ? event_->quality_text : QString(); }
  QString collectState() const { return collect_state_; }
  QString collectText() const { return collect_text_; }
  QString collectFolder() const { return collect_folder_; }

  /**
   * 마지막 촬영(또는 마지막으로 끝난 분석)의 폴더를 다시 대기열에 넣습니다.
   *
   * @note 관류 분석 프로그램이 없으면 파일을 다시 확인하고, 관류 분석 프로그램이 실패 상태이면 다시 띄웁니다.
   *       분석 입력 확인에서 막힌 촬영도 사용자가 고르면 넣습니다. 관류 분석 프로그램이 이유를 다시 알립니다.
   */
  Q_INVOKABLE void retry();

  /** 분석 중인 요청을 취소합니다. 관류 분석 프로그램은 다시 띄우고 대기열의 다음 요청을 이어서 분석합니다. */
  Q_INVOKABLE void cancel();

  /** 분석결과 폴더를 파일 탐색기로 엽니다. 경로가 아직 없으면 아무것도 하지 않습니다. */
  Q_INVOKABLE void openCollectFolder();

  /**
   * 앱을 닫을 때 부릅니다. 상태 읽기를 멈추고, 진행 중인 분석과 대기열을 취소하며 관류 분석 프로그램 프로세스를 끝냅니다.
   *
   * @note 관류 분석 프로그램 종료(최대 AnalyzerTiming::stop_wait_ms 뒤 강제 종료)는 화면 스레드 밖에서 하며, 진행 중인 복사가
   *       끝난 뒤에 합니다. 소멸자가 둘 다 끝나기를 기다립니다. 다시 시작할 수 없습니다.
   */
  void Shutdown();

 public slots:
  /**
   * 촬영 폴더가 저장되었습니다.
   *
   * @param folder 촬영 폴더(전체 경로)
   * @param ready 분석 입력 확인을 통과했으면(ready, warning) true. false이면 분석하지 않고 이유를 보입니다
   */
  void onShotSaved(const QString& folder, bool ready);
  /**
   * 좌우 발 영상이 저장되어 분석을 시작할 수 있습니다(CaptureController::rgbSaved). ready면 바로 대기열에 넣고,
   * 같은 촬영의 onShotSaved에서는 다시 넣지 않습니다.
   */
  void onRgbSaved(const QString& folder, bool ready);
  /** 이 촬영 폴더의 분석이 끝났는지입니다. 결과 창을 "분석 중"으로 열지 정할 때 씁니다. */
  bool Finished(const QString& folder) const { return !folder.isEmpty() && finished_folders_.contains(folder); }

  /** 안정화와 녹화 중이거나 앞 판이 분석 중이면 true. 관류 분석 프로그램에 새 요청을 보내지 않습니다(AnalysisVariants). */
  void setHold(bool hold);
  /**
   * 보류 중일 때 보일 이유입니다. 비우면 녹화 때문에 보류한 것으로 보고 "녹화가 끝나면"으로 적습니다.
   *
   * @param waiting_for 앞 판의 화면 이름. 이 판이 그 판의 분석이 끝나기를 기다리면 줍니다
   */
  void SetHoldReason(const QString& waiting_for);

  /**
   * 녹화 중인 발 영상의 첫 장을 미리 분할하게 합니다. 녹화 중 미리 분석 스위치(prestream)가 켜져 있을 때만 보냅니다.
   *
   * @note 보류(setHold)의 예외입니다. 관류 분석 프로그램이 모델을 올리기 전이면 보내지 않고, 그 촬영은 분석 때 분할합니다.
   */
  void onFirstFramesWritten(const QString& left_video, const QString& right_video);

 signals:
  void changed();
  /**
   * 분석 한 건이 끝났습니다.
   *
   * @param folder 촬영 폴더
   * @param ok 성공이면 true
   * @param summary_path 요약 JSON 경로
   */
  void finished(const QString& folder, bool ok, const QString& summary_path);
  /**
   * 끝난 분석의 그림과 관류 지도 GIF를 다 그렸습니다. finished 뒤에 옵니다.
   *
   * @param folder 촬영 폴더
   * @note 관류 분석 프로그램이 그림을 분석 완료 뒤로 미룹니다(PerfusionRequest::defer_figures). 결과 창은 이때 GIF를 다시 읽습니다.
   */
  void figuresReady(const QString& folder);
  void enabledChanged();
  void collectChanged();
  /**
   * 분석결과 폴더 복사가 끝났습니다.
   *
   * @param folder 촬영 폴더
   * @param ok 복사했거나 같은 내용이 이미 있으면 true
   * @param text 화면 문장(collectText와 같음)
   * @param collect_folder 분석결과 폴더(OS 구분자). 촬영 폴더 경로를 나누지 못했으면 비어 있습니다
   */
  void collected(const QString& folder, bool ok, const QString& text, const QString& collect_folder);

 private:
  // 대기 중일 때 보이는 마지막 일입니다. 새 일이 생기면 바뀝니다.
  struct LastEvent {
    QString state;  // "done", "cancelled", "failed", "skipped"
    QString title;
    QString detail;
    QString elapsed;
    QString summary_path;
    QString quality_status;  // 요약 qc.status. 분석 완료이고 qc가 있을 때만 있습니다
    QString quality_text;    // qc.status_ko와 qc.action_ko
  };

  void Poll();
  void Collect(const QString& folder);
  void SetCollect(const QString& state, const QString& text, const QString& folder);
  void Enqueue(const QString& folder);
  void SetEvent(LastEvent event);
  void Show(const QString& state, const QString& title, const QString& detail, const QString& elapsed, int step,
            int total, int queued, bool can_retry, bool can_cancel, const QString& summary_path,
            const QString& stage_text = {}, double progress = 0, const QString& remaining = {});

  const hwlab::platform::ProcessLaunch launch_;
  const hwlab::platform::AnalyzerTiming timing_;
  std::unique_ptr<hwlab::platform::Analyzer> analyzer_;
  QString missing_;  // 관류 분석 프로그램 파일 문제. 비어 있으면 관류 분석 프로그램이 있습니다
  QString last_folder_;
  int64_t seen_finished_ = 0;
  int64_t seen_figures_ = 0;
  // 분석은 끝났고 그림을 기다리는 촬영 폴더입니다. 그림까지 끝나야 분석결과 폴더로 복사합니다.
  QString figures_folder_;
  QElapsedTimer figures_wait_;
  bool hold_ = false;
  QString waiting_for_;  // 보류 이유가 앞 판이면 그 판의 화면 이름입니다
  QString early_folder_;          // onRgbSaved로 먼저 넣은 촬영. 같은 촬영의 onShotSaved가 다시 넣지 않게 합니다
  QStringList finished_folders_;  // 최근에 분석이 끝난 촬영 폴더(Finished). 최근 몇 개만 둡니다
  const hwlab::platform::AnalyzerVariant variant_;
  bool enabled_ = true;
  bool prestream_ = false;
  std::optional<LastEvent> event_;
  bool shut_down_ = false;
  QTimer poll_timer_;
  QThreadPool background_;  // 분석결과 복사와 종료 때 관류 분석 프로그램 정리. 한 번에 하나씩 순서대로 실행합니다

  QString state_;
  QString title_;
  QString detail_;
  QString elapsed_text_;
  int step_ = 0;
  int total_ = 0;
  QString stage_text_;
  double progress_ = 0;
  QString remaining_text_;
  int queued_ = 0;
  bool can_retry_ = false;
  bool can_cancel_ = false;
  QString summary_path_;
  QString collect_state_;
  QString collect_text_;
  QString collect_folder_;
};

#endif  // HWLAB_APP_ANALYSIS_CONTROLLER_H_
