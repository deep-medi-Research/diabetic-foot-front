#ifndef HWLAB_APP_RESULT_VIEW_H_
#define HWLAB_APP_RESULT_VIEW_H_

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQuickImageProvider>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/pixel/convert.h"
#include "hwlab/platform/perfusion_detail.h"
#include "hwlab/platform/perfusion_summary.h"
#include "hwlab/platform/shot_review.h"
#include "hwlab/platform/video_reader.h"
#include "hwlab/timing/frame_gap.h"

class QDir;
class QQmlEngine;
class QQuickWindow;

/**
 * 관류 분석 결과 창의 ViewModel입니다.
 *
 * 촬영 폴더의 요약 JSON, 매니페스트, 사이드카를 읽어 판정과 관류지수를 문장으로 만들고, 되감기 막대 위치(왼발 RGB 프레임
 * 번호) 하나로 RGB 좌우, 열화상, 관류 지도 GIF의 장을 정합니다. 화면(QML)은 "image://result/<번호>/<스트림>/<위치>"로
 * 영상을 받아 갑니다.
 *
 * @note 스트림 사이의 장은 사이드카의 공통 시각으로 맞춥니다(timing::NearestFrames). GIF의 장은 왼발 RGB 첫 프레임에서
 *       지난 시간으로 분석 창을 고릅니다(timing::WindowIndexAt).
 */
class ResultView : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("main이 만듭니다")

  Q_PROPERTY(bool loaded READ loaded NOTIFY resultChanged)
  /** 녹화 직후 분석이 끝나기 전에 연 창이면 true입니다. 영상과 열화상은 바로 보이고 분석 값은 끝난 뒤 채웁니다. */
  Q_PROPERTY(bool awaiting READ awaiting NOTIFY resultChanged)
  /** 분석 진행 막대에 쓰는 판 묶음(AnalysisVariants)입니다. main이 SetAnalysis로 넘깁니다. 결과 창만 열면 nullptr입니다. */
  Q_PROPERTY(QObject* analysis READ analysis CONSTANT)
  /**
   * 이 촬영에서 고를 수 있는 분석 판 탭입니다. 원소는 `{id, label, done}`이고 판 순서입니다.
   *
   * @note 요약이 있는 판과, 녹화 직후 분석을 기다리는 켜 둔 판을 넣습니다. 판을 나누기 전의 결과(`perfusion` 바로 아래)는
   *       id가 빈 "이전 분석" 탭입니다.
   */
  Q_PROPERTY(QVariantList variantTabs READ variantTabs NOTIFY resultChanged)
  /** 보이는 판 탭의 id입니다. 바꾸면 그 판의 결과를 읽습니다. 되감기 위치는 유지합니다. */
  Q_PROPERTY(QString currentVariant READ currentVariant WRITE setCurrentVariant NOTIFY resultChanged)
  Q_PROPERTY(QString shotName READ shotName NOTIFY resultChanged)
  Q_PROPERTY(QString error READ error NOTIFY resultChanged)
  Q_PROPERTY(QString resultFolder READ resultFolder NOTIFY resultChanged)
  Q_PROPERTY(QString collectFolder READ collectFolder NOTIFY resultChanged)
  Q_PROPERTY(QString collectText READ collectText NOTIFY collectChanged)
  Q_PROPERTY(bool hasCuffLevel READ hasCuffLevel NOTIFY resultChanged)
  Q_PROPERTY(QString cuffScoreText READ cuffScoreText NOTIFY resultChanged)
  Q_PROPERTY(QString cuffCutsText READ cuffCutsText NOTIFY resultChanged)
  Q_PROPERTY(QString cuffSettingText READ cuffSettingText NOTIFY resultChanged)
  Q_PROPERTY(bool cuffApplicable READ cuffApplicable NOTIFY resultChanged)
  /** 커프 단계 기준값을 맞춘 카메라 설정(수동, 게인 1, 노출 -5 또는 -9)으로 찍은 촬영이면 true입니다. */
  Q_PROPERTY(bool cuffFixedSetting READ cuffFixedSetting NOTIFY resultChanged)
  /** 경계별 규칙 판정이나 커프 상태 판정의 결과이면 true, 규칙이 없는 옛 확률 합 판정이면 false입니다. */
  Q_PROPERTY(bool cuffRuleBased READ cuffRuleBased NOTIFY resultChanged)
  /** 커프 상태 0, V, D, O의 결과이면 true입니다. */
  Q_PROPERTY(bool cuffStates READ cuffStates NOTIFY resultChanged)
  /**
   * 판정의 줄입니다. 원소는 `[이름, 값, 기준, 걸림]`입니다.
   *
   * 옛 형식은 120%, 80%, 40% 규칙 순서이고 기준에 든 규칙이 걸림입니다. 커프 상태 형식은 정상, 정상 (경계), 경증, 폐색 네 상태의 확률이고,
   * 기준은 비었고 고른 상태(품질 검사를 무시한 상태 포함)가 걸림입니다. 읽는 법은 cuffModelText입니다.
   */
  Q_PROPERTY(QVariantList cuffRuleRows READ cuffRuleRows NOTIFY resultChanged)
  /** 커프 상태 판정의 읽는 법입니다(네 상태 확률 중 가장 큰 상태를 고름). 옛 형식이면 빈 문자열입니다. */
  Q_PROPERTY(QString cuffModelText READ cuffModelText NOTIFY resultChanged)
  /** 판정 계기 둘레의 네 칸 이름입니다. 커프 상태 형식은 0, V, D, O이고 옛 형식은 0, 40, 80, 120입니다. */
  Q_PROPERTY(QStringList cuffGaugeLabels READ cuffGaugeLabels NOTIFY resultChanged)
  /** 판정 계기 가운데의 단계입니다. 커프 상태 형식은 요약의 `level_ko`(예: "경증")이고 옛 형식은 "40%"처럼 씁니다. 없으면 "-"입니다. */
  Q_PROPERTY(QString cuffGaugeValue READ cuffGaugeValue NOTIFY resultChanged)
  /** 판정 계기 아래의 설명입니다. 예: "커프 상태 D, PAD 확률 0.76", "수축기 혈압 대비 커프 압력". */
  Q_PROPERTY(QString cuffGaugeCaption READ cuffGaugeCaption NOTIFY resultChanged)
  /** 커프 단계가 무엇을 추정한 값인지 적은 문장입니다. 판정 계기가 없을 때(단계를 내지 않았을 때) 보입니다. */
  Q_PROPERTY(QString cuffSchemeText READ cuffSchemeText NOTIFY resultChanged)
  /** 규칙 판정의 맥박 품질 문장입니다. 예: "SNR 12.4 dB, 기준 14.0 dB 이상". */
  Q_PROPERTY(QString cuffQualityText READ cuffQualityText NOTIFY resultChanged)
  /** 규칙 판정이 맥박 품질 부족으로 단계를 내지 않았으면 true입니다. */
  Q_PROPERTY(bool cuffQualityFailed READ cuffQualityFailed NOTIFY resultChanged)
  /** 규칙 판정이 단계를 내지 않은 이유 문장입니다. 품질 검사를 뺐을 때의 단계를 함께 적습니다. */
  Q_PROPERTY(QString cuffQualityFailText READ cuffQualityFailText NOTIFY resultChanged)
  Q_PROPERTY(bool hasPadVerdict READ hasPadVerdict NOTIFY resultChanged)
  Q_PROPERTY(int verdict READ verdict NOTIFY resultChanged)
  Q_PROPERTY(QString verdictText READ verdictText NOTIFY resultChanged)
  Q_PROPERTY(QString padVerdictText READ padVerdictText NOTIFY resultChanged)
  Q_PROPERTY(QString scoreText READ scoreText NOTIFY resultChanged)
  Q_PROPERTY(QString thresholdText READ thresholdText NOTIFY resultChanged)
  Q_PROPERTY(QString modelText READ modelText NOTIFY resultChanged)
  Q_PROPERTY(QString affectedFootText READ affectedFootText NOTIFY resultChanged)
  Q_PROPERTY(QString windowVotesText READ windowVotesText NOTIFY resultChanged)
  Q_PROPERTY(QString piLeftText READ piLeftText NOTIFY resultChanged)
  Q_PROPERTY(QString piRightText READ piRightText NOTIFY resultChanged)
  Q_PROPERTY(QString piRatioText READ piRatioText NOTIFY resultChanged)
  Q_PROPERTY(QString lowerFootText READ lowerFootText NOTIFY resultChanged)
  Q_PROPERTY(bool thermalExcluded READ thermalExcluded NOTIFY resultChanged)
  Q_PROPERTY(QString thermalText READ thermalText NOTIFY resultChanged)
  Q_PROPERTY(QString disclaimer READ disclaimer CONSTANT)
  Q_PROPERTY(QString analyzerText READ analyzerText NOTIFY resultChanged)
  Q_PROPERTY(QString firstFoot READ firstFoot NOTIFY resultChanged)
  Q_PROPERTY(QString secondFoot READ secondFoot NOTIFY resultChanged)
  Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY resultChanged)
  Q_PROPERTY(QString videoNote READ videoNote NOTIFY resultChanged)
  Q_PROPERTY(bool hasThermal READ hasThermal NOTIFY resultChanged)
  Q_PROPERTY(QString thermalNote READ thermalNote NOTIFY resultChanged)
  Q_PROPERTY(QString thermalRangeText READ thermalRangeText NOTIFY resultChanged)
  Q_PROPERTY(QString thermalCaption READ thermalCaption NOTIFY resultChanged)
  Q_PROPERTY(bool hasGif READ hasGif NOTIFY resultChanged)
  Q_PROPERTY(QUrl gifUrl READ gifUrl NOTIFY resultChanged)
  Q_PROPERTY(int frameCount READ frameCount NOTIFY resultChanged)
  /**
   * 결과를 읽을 때마다 하나씩 늘리는 번호입니다. 화면은 영상 주소에 넣습니다.
   *
   * @note Qt Image는 주소가 같으면 영상을 다시 요청하지 않습니다. 새 촬영을 읽어도 되감기 위치가 같으면 주소가 같아
   *       앞 촬영의 장이 남으므로, 읽을 때마다 바뀌는 이 값을 주소에 넣습니다.
   */
  Q_PROPERTY(int loadRevision READ loadRevision NOTIFY resultChanged)
  Q_PROPERTY(int position READ position NOTIFY positionChanged)
  Q_PROPERTY(int gifFrame READ gifFrame NOTIFY positionChanged)
  Q_PROPERTY(QString timeText READ timeText NOTIFY positionChanged)
  Q_PROPERTY(QString frameText READ frameText NOTIFY positionChanged)
  /** 부위별 관류지수 표의 행 [부위, 왼발, 오른발]입니다. */
  Q_PROPERTY(QVariantList regionRows READ regionRows NOTIFY resultChanged)
  /**
   * 분석 품질 표의 행 [항목, 값, 설명, 주의, 계산 안 함]입니다. 요약에 측정 품질 안내(qc)가 있으면 그 항목이고, 없으면
   * 결과 창이 요약에서 고른 값입니다.
   */
  Q_PROPERTY(QVariantList qualityRows READ qualityRows NOTIFY resultChanged)
  /** 측정 품질 상태(요약 `qc.status`): "ok", "check", "retake". qc가 없는 결과는 빈 문자열입니다. */
  Q_PROPERTY(QString qualityStatus READ qualityStatus NOTIFY resultChanged)
  /** 측정 품질 상태 문장(`qc.status_ko`). 예: "재측정 필요". */
  Q_PROPERTY(QString qualityStatusText READ qualityStatusText NOTIFY resultChanged)
  /** 측정자가 할 일(`qc.action_ko`). 할 일이 없으면 빈 문자열입니다. */
  Q_PROPERTY(QString qualityAction READ qualityAction NOTIFY resultChanged)
  /** 시간축 그래프 값입니다(TimeChart.qml). 파형, 구간별 관류지수, 구간별 판정 점수와 각 데이터가 없는 이유를 담습니다. */
  Q_PROPERTY(QVariantMap chart READ chart NOTIFY resultChanged)
  /** 되감기 위치의 분석 시각(초)입니다. 관류 분석 프로그램은 장 번호 / fps를 시각으로 쓰므로 그래프도 같은 시각을 씁니다. */
  Q_PROPERTY(double positionSeconds READ positionSeconds NOTIFY positionChanged)
  /**
   * 녹화 전체를 요약한 관류 분석 프로그램 그림입니다. 원소는 `{title, detail, url}`이고 발 분할, 양발 정합, 표준 발 PI 지도
   * 순서입니다. 파일이 있는 그림만 넣습니다. 결과 창 오른쪽 열에 보입니다.
   */
  Q_PROPERTY(QVariantList summaryFigures READ summaryFigures NOTIFY resultChanged)
  /** 시간 창별 표준 발 틀 PI 지도를 그릴 수 있으면 true입니다. 영상은 "image://result/<번호>/tmap/<창>"입니다. */
  Q_PROPERTY(bool hasTemplateMap READ hasTemplateMap NOTIFY resultChanged)
  /** 표준 발 틀 PI 지도의 색표 범위 문장입니다. 예: "0 ~ 0.228%". */
  Q_PROPERTY(QString templateMapRangeText READ templateMapRangeText NOTIFY resultChanged)
  /**
   * 커프 단계의 순번입니다. 판정 계기가 씁니다. 단계가 없으면 -1입니다.
   *
   * 커프 상태 형식은 0, V, D가 0~2이고 O(폐색)가 3입니다. 옛 요약의 M과 S도 3입니다. 옛 퍼센트 형식은 0, 40, 80, 120%가 0~3입니다.
   */
  Q_PROPERTY(int cuffLevelIndex READ cuffLevelIndex NOTIFY resultChanged)
  /** 촬영 때 입력한 값을 적는 줄의 이름입니다. 커프 상태 형식은 "고른 발목 혈압 단계", 옛 형식은 "촬영 때 입력한 기준"입니다. */
  Q_PROPERTY(QString criterionLabel READ criterionLabel NOTIFY resultChanged)
  /**
   * 촬영 때 입력한 값입니다. 예: "M", "40%". 견줄 값이 매니페스트에 없으면 빈 문자열입니다.
   *
   * @note 커프 상태 형식의 결과는 발목 혈압 단계와 견줍니다. M과 S를 골랐으면 폐색(O)과 같은 칸입니다. 검사 기준 %는 상태와 뜻이 달라 견주지 않습니다.
   */
  Q_PROPERTY(QString criterionText READ criterionText NOTIFY resultChanged)
  /** 입력한 기준과 추정 단계를 견준 문장입니다. 예: "추정과 같음". 둘 중 하나가 없으면 빈 문자열입니다. */
  Q_PROPERTY(QString criterionNote READ criterionNote NOTIFY resultChanged)
  /** 입력한 기준과 추정 단계가 같으면 true입니다. */
  Q_PROPERTY(bool criterionMatches READ criterionMatches NOTIFY resultChanged)
  /** 부위별 관류지수 막대입니다. 원소는 `{name, left, right}`(PI %, 없으면 NaN)이고 발 전체, 발가락, 앞발, 발 중간, 뒤꿈치 순서입니다. */
  Q_PROPERTY(QVariantList piBars READ piBars NOTIFY resultChanged)
  /** 부위별 관류지수 막대의 가로 끝 값(PI %)입니다. 모든 막대 중 가장 큰 값입니다. */
  Q_PROPERTY(double piBarMax READ piBarMax NOTIFY resultChanged)
  /** 30초 전체의 좌우 맥박 상관 문장입니다. 예: "0.47". */
  Q_PROPERTY(QString templateCorrText READ templateCorrText NOTIFY resultChanged)
  /** RGB 카메라 설정 표의 행 [항목, 값, 기준, 기준과 다름]입니다. 사이드카와 매니페스트에서 읽습니다. */
  Q_PROPERTY(QVariantList cameraRows READ cameraRows NOTIFY resultChanged)
  /** 카메라 설정 표 제목 옆 문장입니다. 두 발 카메라 설정이 같으면 "두 발 같음"입니다. */
  Q_PROPERTY(QString cameraNote READ cameraNote NOTIFY resultChanged)

 public:
  /**
   * @param engine 결과 창을 만들 QML 엔진. 이미지 제공자 "result"를 이 엔진에 등록합니다
   * @param parent Qt 부모
   */
  explicit ResultView(QQmlEngine* engine, QObject* parent = nullptr);
  ~ResultView() override;
  ResultView(const ResultView&) = delete;
  ResultView& operator=(const ResultView&) = delete;

  /**
   * 촬영 폴더의 분석 결과를 읽고 결과 창을 띄웁니다. 창이 이미 있으면 내용을 바꾸고 앞으로 가져옵니다.
   *
   * @param shot_folder 촬영 폴더. 요약은 판마다 `<shot_folder>/perfusion/<판>/<폴더 이름>_summary.json`에서 읽습니다
   * @param error 요약을 읽지 못하면 이유를 씁니다. nullptr이면 쓰지 않습니다
   * @return 요약을 읽었으면 true. 영상이나 열화상을 열지 못해도 요약을 읽었으면 true이고 그 칸에 이유를 보입니다
   * @note 창은 요약을 읽지 못해도 띄우고 이유를 보입니다.
   */
  bool Open(const QString& shot_folder, QString* error = nullptr, bool awaiting = false);

  /**
   * 결과 창이 이 촬영을 보이고 있으면 분석 결과를 다시 읽습니다. 창을 앞으로 가져오지 않고 되감기 위치를 유지합니다.
   *
   * @param shot_folder 분석이 끝난 촬영 폴더
   * @return 창이 이 촬영을 보이고 있어 다시 읽었으면 true
   */
  bool Refresh(const QString& shot_folder);

  /** 분석 진행 막대와 판 목록에 쓸 판 묶음(AnalysisVariants)을 정합니다. 창을 만들기 전에 한 번 부릅니다. */
  void SetAnalysis(QObject* analysis) { analysis_ = analysis; }

  /** 결과 창입니다. 띄우기 전에는 nullptr입니다. */
  QQuickWindow* Window() const;

  /** 결과 창을 닫습니다. 띄우지 않았으면 아무것도 하지 않습니다. 메인 창이 닫힐 때 부릅니다. */
  void Close();

  /**
   * 분석결과 폴더 복사 결과를 결과 창에 보입니다.
   *
   * @param shot_folder 복사한 촬영 폴더. 지금 결과 창의 촬영과 다르면 무시합니다
   * @param text 복사 결과 문장(AnalysisController::collectText)
   */
  void SetCollectText(const QString& shot_folder, const QString& text);

  /**
   * 되감기 막대 위치를 정합니다.
   *
   * @param position 왼발 RGB 프레임 번호. 0 ~ frameCount - 1로 제한합니다
   */
  Q_INVOKABLE void setPosition(int position);

  /** 분석 결과 폴더(`<촬영 폴더>/perfusion`)를 파일 탐색기로 엽니다. */
  Q_INVOKABLE void openFolder();

  /** 저장 위치의 분석결과 폴더를 파일 탐색기로 엽니다. 촬영 폴더 경로를 나누지 못했으면 아무것도 하지 않습니다. */
  Q_INVOKABLE void openCollectFolder();

  /**
   * 이미지 제공자가 부르는 함수입니다.
   *
   * @param id "<번호>/left/<위치>", "<번호>/right/<위치>", "<번호>/thermal/<위치>", "<번호>/tmap/<창>". 번호는
   *           loadRevision이고 주소를 바꾸는 데만 씁니다. 위치는 왼발 RGB 프레임 번호, 창은 시간 창 번호(gifFrame)입니다
   * @return 미리보기 영상. 읽지 못하면 빈 영상
   */
  QImage Frame(const QString& id);

  bool loaded() const { return summary_.has_value(); }
  bool awaiting() const { return awaiting_ && !summary_.has_value(); }
  QObject* analysis() const { return analysis_; }
  QString shotName() const { return shot_name_; }
  QString error() const { return error_; }
  QString resultFolder() const { return result_folder_; }
  QString collectFolder() const { return collect_folder_; }
  QString collectText() const { return collect_text_; }
  bool hasCuffLevel() const { return summary_ && summary_->cuff_level.has_value(); }
  QString cuffScoreText() const;
  QString cuffCutsText() const;
  QString cuffSettingText() const;
  bool cuffApplicable() const { return hasCuffLevel() && summary_->cuff_level->applicable; }
  bool cuffFixedSetting() const;
  bool cuffRuleBased() const { return hasCuffLevel() && !summary_->cuff_level->rule.empty(); }
  bool cuffStates() const { return hasCuffLevel() && summary_->cuff_level->states; }
  QVariantList cuffRuleRows() const;
  QStringList cuffGaugeLabels() const;
  QString cuffGaugeValue() const;
  QString cuffGaugeCaption() const;
  QString cuffModelText() const;
  QString cuffSchemeText() const;
  QString cuffQualityText() const;
  bool cuffQualityFailed() const { return cuffRuleBased() && !summary_->cuff_level->quality_ok; }
  QString cuffQualityFailText() const;
  QVariantList variantTabs() const { return variant_tabs_; }
  QString currentVariant() const { return variant_; }
  void setCurrentVariant(const QString& id);
  /** 이진 PAD 판정이 있는지. 2026-10-26 이전 결과이거나 판정을 켜고 분석한 결과입니다. */
  bool hasPadVerdict() const { return summary_ && summary_->classified; }
  int verdict() const;
  /** 결과 창 제목 줄입니다. 커프 단계가 있으면 커프 단계, 없으면 이진 판정입니다. */
  QString verdictText() const;
  QString padVerdictText() const;
  QString scoreText() const;
  QString thresholdText() const;
  QString modelText() const;
  QString affectedFootText() const;
  QString windowVotesText() const;
  QString piLeftText() const;
  QString piRightText() const;
  QString piRatioText() const;
  QString lowerFootText() const;
  bool thermalExcluded() const;
  QString thermalText() const;
  QString disclaimer() const;
  QString analyzerText() const;
  QString firstFoot() const;
  QString secondFoot() const;
  bool hasVideo() const { return frame_count_ > 0; }
  QString videoNote() const { return video_note_; }
  bool hasThermal() const { return !thermal_map_.empty(); }
  QString thermalNote() const { return thermal_note_; }
  QString thermalRangeText() const;
  /** 열화상 칸 제목입니다. 열화상은 한 화면에 두 발이 있으므로 화면 왼쪽이 어느 발인지 적습니다. 결과 창은 늘 왼발을 왼쪽에 그립니다. */
  QString thermalCaption() const;
  bool hasGif() const { return !gif_url_.isEmpty(); }
  QUrl gifUrl() const { return gif_url_; }
  int frameCount() const { return static_cast<int>(frame_count_); }
  int loadRevision() const { return load_revision_; }
  int position() const { return position_; }
  int gifFrame() const;
  QString timeText() const;
  QString frameText() const;
  QVariantList regionRows() const;
  QVariantList qualityRows() const;
  QVariantMap chart() const { return chart_; }
  double positionSeconds() const;
  QVariantList summaryFigures() const { return summary_figures_; }
  QString qualityStatus() const { return summary_ && summary_->quality ? QString::fromStdString(summary_->quality->status) : QString(); }
  QString qualityStatusText() const {
    return summary_ && summary_->quality ? QString::fromStdString(summary_->quality->status_ko) : QString();
  }
  QString qualityAction() const {
    return summary_ && summary_->quality ? QString::fromStdString(summary_->quality->action_ko) : QString();
  }
  bool hasTemplateMap() const { return template_top_.has_value(); }
  QString templateMapRangeText() const;
  int cuffLevelIndex() const;
  QString criterionLabel() const;
  QString criterionText() const;
  QString criterionNote() const;
  bool criterionMatches() const;
  QVariantList piBars() const;
  double piBarMax() const;
  QString templateCorrText() const;
  QVariantList cameraRows() const;
  QString cameraNote() const;

  /**
   * 그래프의 시각(초)으로 되감기 위치를 옮깁니다.
   *
   * @param seconds 분석 시각(장 번호 / fps)
   */
  Q_INVOKABLE void seekSeconds(double seconds);

 signals:
  void resultChanged();
  void positionChanged();
  void collectChanged();

 private:
  void Load(const QString& shot_folder, bool keep_position);
  // 촬영 폴더에 결과가 있는 판과 분석을 기다리는 판으로 탭을 만들고, 보일 판을 고릅니다.
  void ChooseVariant(const QDir& dir);
  void LoadChart(const std::string& out_dir);
  void LoadPpg(double duration);
  void LoadFrameTiming(double fps);
  void LoadStreams(const QString& folder, const hwlab::platform::ShotManifest& manifest);
  QImage RgbImage(hwlab::platform::VideoReader* reader, int64_t index);
  QImage ThermalImage(int64_t index);
  QImage TemplateMapImage(int32_t window);
  void LoadSummaryFigures(const std::string& out_dir);

  QQmlEngine* engine_;
  QObject* analysis_ = nullptr;
  bool awaiting_ = false;
  QPointer<QQuickWindow> window_;

  QString shot_name_;
  QString variant_;  // 보이는 판 탭. 빈 문자열은 판을 나누기 전의 결과입니다
  QVariantList variant_tabs_;
  QString error_;
  QString result_folder_;
  QString shot_folder_;     // 절대 경로, '/' 구분자
  QString collect_folder_;  // 저장 위치/분석결과(OS 구분자). 경로를 나누지 못하면 비어 있습니다
  QString collect_text_;
  QString video_note_;
  QString thermal_note_;
  QUrl gif_url_;
  QVariantMap chart_;
  QVariantList summary_figures_;
  std::optional<hwlab::platform::PerfusionSummary> summary_;
  std::optional<hwlab::record::FootSide> pixel_left_foot_;
  std::optional<int32_t> criterion_;  // 매니페스트 criterion
  QString ankle_level_;               // 매니페스트 blood_pressure.ankle_level.name. schema 6 전이면 비었습니다
  std::string light_source_;          // 매니페스트 lighting.source
  std::string light_product_;         // 매니페스트 lighting.product
  std::optional<hwlab::platform::RecordedCameraSettings> left_camera_;   // 왼발 RGB 사이드카 settings
  std::optional<hwlab::platform::RecordedCameraSettings> right_camera_;  // 오른발 RGB 사이드카 settings
  int32_t rgb_width_ = 0;                                                // 왼발 RGB 사이드카 format
  int32_t rgb_height_ = 0;
  std::optional<int32_t> rgb_fps_;
  std::optional<double> template_top_;  // 표준 발 틀 PI 지도 색표 위끝. 지도를 그릴 수 없으면 nullopt

  int load_revision_ = 0;
  int position_ = 0;
  int64_t frame_count_ = 0;                  // 되감기 막대의 장 수(왼발 RGB 영상과 사이드카 중 작은 값)
  std::vector<hwlab::Timestamp> left_time_;  // 왼발 RGB 프레임의 공통 시각
  std::vector<size_t> right_map_;            // 왼발 프레임 번호마다 오른발 프레임 번호
  std::vector<size_t> thermal_map_;          // 왼발 프레임 번호마다 열화상 프레임 번호
  std::vector<int32_t> gif_map_;             // 왼발 프레임 번호마다 GIF 장 번호
  hwlab::timing::FrameRateCheck frame_check_;  // 왼발 RGB 프레임 시각의 느린 간격(LoadFrameTiming)
  int64_t frame_missing_ = 0;                  // 왼발 RGB 프레임 시각의 카메라 단 누락 장 수

  std::mutex mutex_;  // 아래 읽기 객체. 이미지 제공자가 다른 스레드에서 부를 수 있습니다
  std::unique_ptr<hwlab::platform::VideoReader> left_reader_;
  std::unique_ptr<hwlab::platform::VideoReader> right_reader_;
  std::string thermal_path_;
  hwlab::platform::SidecarFrames thermal_frames_;
  hwlab::pixel::Y16Range thermal_range_;
  std::vector<uint8_t> uyvy_;
  std::vector<uint16_t> y16_;
  std::optional<hwlab::platform::PerfusionDetail> detail_;  // 표준 발 틀 PI 지도를 그릴 결과 배열
};

/** QML Image가 "image://result/<번호>/<스트림>/<위치>"로 결과 창 영상을 받아 가는 곳입니다. */
class ResultImageProvider : public QQuickImageProvider {
 public:
  explicit ResultImageProvider(ResultView* view) : QQuickImageProvider(QQuickImageProvider::Image), view_(view) {}
  QImage requestImage(const QString& id, QSize* size, const QSize&) override {
    QImage image = view_->Frame(id);
    if (size != nullptr) *size = image.size();
    return image;
  }

 private:
  ResultView* view_;
};

#endif  // HWLAB_APP_RESULT_VIEW_H_
