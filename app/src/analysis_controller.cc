#include "analysis_controller.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QRunnable>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <utility>

#include "hwlab/platform/frame_timing.h"
#include "hwlab/platform/perfusion_analysis.h"
#include "hwlab/platform/perfusion_summary.h"
#include "hwlab/record/shot.h"

namespace {

constexpr int kPollMs = 200;
// 관류 분석 프로그램의 예외 기록은 수십 줄이므로 화면에는 앞부분만 보이고 전체는 analyzer_log.txt에 남깁니다.
constexpr int kDetailLines = 3;
constexpr int kDetailChars = 240;

QString Utf8(const std::string& text) { return QString::fromStdString(text); }

QString Shorten(const QString& text) {
  QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  const bool cut_lines = lines.size() > kDetailLines;
  if (cut_lines) lines = lines.mid(0, kDetailLines);
  QString out = lines.join(QLatin1Char('\n'));
  if (out.size() > kDetailChars) return out.left(kDetailChars) + QStringLiteral("…");
  return cut_lines ? out + QStringLiteral("\n…") : out;
}

QString Seconds(int64_t ms) { return QStringLiteral("%1초").arg((ms + 500) / 1000); }

// 요약의 측정 품질 안내(qc)입니다. 촬영 화면은 결과 창을 열지 않아도 이 상태와 조치 문장을 보입니다.
std::optional<hwlab::platform::MeasurementQuality> ReadQuality(const QString& summary_path) {
  QFile file(summary_path);
  if (summary_path.isEmpty() || !file.open(QIODevice::ReadOnly)) return std::nullopt;
  const auto summary = hwlab::platform::ReadPerfusionSummary(file.readAll().toStdString());
  return summary ? summary->quality : std::nullopt;
}

// 관류 분석 프로그램의 5단계 이름입니다. 단계 번호는 출력 줄의 [n/5]입니다.
const char* const kStageNames[] = {"분석 모델 준비", "영상 읽기와 발 찾기", "맥박 신호 추출", "관류 계산", "커프 단계 추정",
                                   "결과 파일 저장"};
// 각 단계가 시작하는 시점의 전체 대비 비율입니다. 2026-10-26 실험 노트북 실제 발 촬영 4건(이진 판정 끔, 1단계부터 26.5초)의
// 1단계 시작 기준 단계 시각 평균 0, 16.5, 22.5, 23, 24.5초에서 정했습니다. 0단계(모델 올리기)는 앱이 미리 하므로 길이를 0으로 둡니다.
// 남은 시간 예상에만 쓰므로 다른 PC에서 달라도 분석에는 영향이 없습니다.
constexpr double kStageStart[] = {0.0, 0.0, 0.62, 0.85, 0.87, 0.92, 1.0};
// 분석 완료 뒤 그림(figures 줄)을 기다리는 최대 시간입니다. 노트북에서 그림은 약 2.5초 걸립니다(2026-10-26).
constexpr int64_t kFiguresWaitMs = 30'000;
// 진행 비율이 이보다 작으면 남은 시간을 보이지 않습니다. 첫 단계는 영상 읽기라 경과 시간으로 전체를 예상하기 어렵습니다.
constexpr double kMinProgressForEstimate = 0.1;

// 진행 단계와 하위 단계로 전체 진행 비율을 계산합니다. 5단계가 아니면 단계 수로 균등하게 나눕니다.
double Progress(const hwlab::platform::AnalyzerStatus& s) {
  if (s.total <= 0) return 0;
  const double sub = s.sub_total > 0 ? static_cast<double>(s.sub_step) / s.sub_total : 0.0;
  if (s.total == 5 && s.step >= 0 && s.step <= 5) {
    const double start = kStageStart[s.step];
    const double end = kStageStart[s.step + 1];
    return start + (end - start) * sub;
  }
  return std::min(1.0, (s.step + sub) / s.total);
}

QString StageText(const hwlab::platform::AnalyzerStatus& s) {
  if (s.total == 5 && s.step >= 0 && s.step <= 5) return QString::fromUtf8(kStageNames[s.step]);
  return QString::fromStdString(s.message);
}

// 남은 시간 예상입니다. 5초 단위로 올려 숫자가 매번 흔들리지 않게 합니다.
QString Remaining(int64_t elapsed_ms, double progress) {
  if (progress < kMinProgressForEstimate || progress >= 1.0) return {};
  const double left_s = elapsed_ms / 1000.0 * (1.0 - progress) / progress;
  const int rounded = std::max(5, static_cast<int>(std::ceil(left_s / 5.0)) * 5);
  return QStringLiteral("약 %1초 남음").arg(rounded);
}

QString FolderName(const QString& folder) { return QDir::fromNativeSeparators(folder).section(QLatin1Char('/'), -1, -1, QString::SectionSkipEmpty); }

struct CollectOutcome {
  bool ok = false;
  QString text;
  QString folder;  // 분석결과 폴더(OS 구분자)
};

// 분석 결과를 저장 위치의 분석결과 폴더로 복사합니다. 파일을 비교하고 복사하므로 작업 스레드에서 부릅니다.
CollectOutcome CollectShot(const QString& shot_folder, const std::string& variant) {
  CollectOutcome out;
  const QString failed = QStringLiteral("results 폴더로 복사하지 못함: ");
  const std::string path = shot_folder.toStdString();
  const auto split = hwlab::record::SplitShotFolderPath(path);
  if (!split) {
    out.text = failed + QStringLiteral("촬영 폴더 경로가 저장 위치/이름_생년월일/기준/날짜_시각(이전 구조는 저장 위치/날짜/이름_생년월일/기준/시각) 형식이 아닙니다");
    return out;
  }
  out.folder = QDir::toNativeSeparators(
      QDir(Utf8(split->root)).filePath(QString::fromUtf8(hwlab::record::kCollectedResultFolder)));
  std::string error;
  const std::string output_dir = hwlab::platform::PerfusionOutputDir(path, variant);
  // 분석 구간마다 프레임 간격(fps 저하)을 CSV로 남겨 함께 복사합니다. 못 써도 결과 복사는 합니다.
  std::string timing_error;
  if (!hwlab::platform::WriteFrameTiming(path, output_dir, split->layout.stem, &timing_error)) {
    qWarning("프레임 간격 CSV를 쓰지 못함: %s", timing_error.c_str());
  }
  const auto result = hwlab::platform::CollectPerfusionResults(output_dir, split->layout.stem, split->root, split->layout,
                                                               variant, &error);
  if (!result) {
    out.text = failed + (error.empty() ? QStringLiteral("알 수 없는 이유") : Utf8(error));
    return out;
  }
  out.folder = QDir::toNativeSeparators(Utf8(result->folder));
  if (result->files.empty()) {
    out.text = failed + QStringLiteral("분석 결과 폴더에 복사할 결과 파일이 없습니다");
    return out;
  }
  out.ok = true;
  out.text = result->copied > 0
                 ? QStringLiteral("results 폴더로 %1개 복사").arg(result->copied)
                 : QStringLiteral("results 폴더에 같은 결과 %1개가 있어 복사하지 않음").arg(result->files.size());
  if (result->reanalysis > 1) out.text += QStringLiteral(", 재분석 %1").arg(result->reanalysis);
  return out;
}

}  // namespace

AnalysisController::AnalysisController(hwlab::platform::ProcessLaunch launch, hwlab::platform::AnalyzerVariant variant,
                                       hwlab::platform::AnalyzerTiming timing, QObject* parent)
    : QObject(parent), launch_(std::move(launch)), timing_(timing), variant_(std::move(variant)) {
  background_.setMaxThreadCount(1);
  connect(&poll_timer_, &QTimer::timeout, this, &AnalysisController::Poll);
}

AnalysisController::~AnalysisController() {
  poll_timer_.stop();
  // 복사와 관류 분석 프로그램 정리가 이 객체로 결과를 보내므로 먼저 끝나기를 기다립니다.
  background_.waitForDone();
  analyzer_.reset();
}

void AnalysisController::Shutdown() {
  if (shut_down_) return;
  shut_down_ = true;
  poll_timer_.stop();
  bool active = false;
  if (analyzer_) {
    const hwlab::platform::AnalyzerStatus s = analyzer_->Status();
    active = s.state == hwlab::platform::AnalyzerState::kBusy || s.queued > 0;
    // Stop은 관류 분석 프로그램이 끝나기를 기다리므로(최대 stop_wait_ms 뒤 강제 종료) 화면 스레드에서 하지 않습니다.
    std::shared_ptr<hwlab::platform::Analyzer> analyzer(std::move(analyzer_));
    background_.start(QRunnable::create([analyzer] { analyzer->Stop(); }));
  }
  Show(QStringLiteral("cancelled"), QStringLiteral("분석 종료"),
       active ? QStringLiteral("앱을 닫아 진행 중인 분석을 취소했습니다") : QString(), {}, 0, 0, 0, false, false, {});
}

void AnalysisController::Start() {
  if (shut_down_) return;
  missing_.clear();
  if (const auto problem = hwlab::platform::AnalyzerFilesProblem(launch_)) {
    missing_ = Utf8(*problem);
  } else if (!analyzer_) {
    std::string error;
    analyzer_ = hwlab::platform::Analyzer::Start(launch_, timing_, &error);
    if (analyzer_) {
      analyzer_->SetHold(hold_);
    } else {
      SetEvent({QStringLiteral("failed"), QStringLiteral("분석 실패"), Utf8(error), {}, {}});
    }
  }
  if (!poll_timer_.isActive()) poll_timer_.start(kPollMs);
  Poll();
}

void AnalysisController::SetHoldReason(const QString& waiting_for) {
  if (waiting_for == waiting_for_) return;
  waiting_for_ = waiting_for;
  Poll();
}

void AnalysisController::setHold(bool hold) {
  hold_ = hold;
  if (analyzer_) analyzer_->SetHold(hold);
}

void AnalysisController::SetEnabled(bool on) {
  if (on == enabled_) return;
  enabled_ = on;
  emit enabledChanged();
}

QString AnalysisController::variantSource() const {
  const auto& v = variant_;
  const auto asset = [](const hwlab::platform::AnalyzerVariant::Asset& a) {
    if (a.name.empty()) return QString();
    return QString::fromStdString(a.uploaded) + QStringLiteral(" (") + QString::fromStdString(a.sha256.substr(0, 8)) +
           QStringLiteral(")");
  };
  QStringList parts;
  if (!v.upstream_commit.empty()) {
    parts.append(QStringLiteral("연구 ") + QString::fromStdString(v.upstream_commit.substr(0, 7)) + QStringLiteral(" ") +
                 QString::fromStdString(v.upstream_date));
  }
  QStringList assets;
  for (const auto& [role, a] : {std::pair{QStringLiteral("판정 "), &v.classifier},
                                std::pair{QStringLiteral("템플릿 "), &v.pulse_template}}) {
    if (const QString text = asset(*a); !text.isEmpty()) assets.append(role + text);
  }
  if (!assets.isEmpty()) parts.append(QString::fromStdString(v.release_tag) + QStringLiteral(": ") + assets.join(QStringLiteral(", ")));
  return parts.join(QStringLiteral("\n"));
}

bool AnalysisController::Active() const {
  if (!analyzer_) return false;
  const hwlab::platform::AnalyzerStatus s = analyzer_->Status();
  return s.state == hwlab::platform::AnalyzerState::kBusy || s.queued > 0;
}

void AnalysisController::onFirstFramesWritten(const QString& left_video, const QString& right_video) {
  if (!enabled_ || !prestream_ || shut_down_ || !analyzer_) return;
  std::string error;
  if (!analyzer_->Presegment(left_video.toStdString(), right_video.toStdString(), &error)) {
    qInfo("녹화 중 첫 장 미리 분할을 보내지 않음: %s", error.c_str());
  }
}

void AnalysisController::onRgbSaved(const QString& folder, bool ready) {
  if (shut_down_ || !ready || !enabled_) return;
  last_folder_ = folder;
  early_folder_ = folder;
  Enqueue(folder);
}

void AnalysisController::onShotSaved(const QString& folder, bool ready) {
  if (shut_down_ || !enabled_) return;
  if (folder == early_folder_) {
    // 녹화가 끝나 RGB 확인을 통과했을 때 이미 넣었습니다. 막는 문제는 RGB에서만 나오므로 여기서 결과가 달라지지 않습니다.
    early_folder_.clear();
    if (ready) return;
  }
  last_folder_ = folder;
  if (!ready) {
    // 관류 분석 프로그램 결과보다 입력 확인 결과를 먼저 보입니다. 이유 문장은 촬영 결과 줄(analysisText)에 있습니다.
    SetEvent({QStringLiteral("skipped"), QStringLiteral("분석하지 않음"),
              FolderName(folder) + QStringLiteral(": 분석 입력 확인에서 막혔습니다"), {}, {}});
    return;
  }
  Enqueue(folder);
}

void AnalysisController::Enqueue(const QString& folder) {
  if (!analyzer_) {
    if (missing_.isEmpty()) {
      SetEvent({QStringLiteral("failed"), QStringLiteral("분석 실패"), QStringLiteral("관류 분석 프로그램을 띄우지 못했습니다"), {}, {}});
    }
    Poll();
    return;
  }
  std::string error;
  auto request = hwlab::platform::PerfusionRequestForShot(folder.toStdString(), &error);
  if (request) request->output_dir = hwlab::platform::PerfusionOutputDir(request->recording_dir, variant_.id);
  if (!request || !analyzer_->Submit(*request, &error)) {
    SetEvent({QStringLiteral("failed"), QStringLiteral("분석 실패"), Utf8(error), {}, {}});
    return;
  }
  Poll();
}

void AnalysisController::retry() {
  if (shut_down_) return;
  if (!analyzer_) {
    Start();
    if (!analyzer_) return;
  }
  if (analyzer_->Status().state == hwlab::platform::AnalyzerState::kFailed) analyzer_->Restart();
  if (!last_folder_.isEmpty()) Enqueue(last_folder_);
}

void AnalysisController::cancel() {
  if (analyzer_) analyzer_->Cancel();
  Poll();
}

void AnalysisController::openCollectFolder() {
  if (!collect_folder_.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(collect_folder_));
}

void AnalysisController::Collect(const QString& folder) {
  SetCollect(QStringLiteral("copying"), QStringLiteral("results 폴더로 복사 중"), {});
  background_.start(QRunnable::create([this, folder, variant = variant_.id] {
    const CollectOutcome outcome = CollectShot(folder, variant);
    // 소멸자가 작업 스레드를 기다린 뒤 객체가 사라지므로, 큐에 넣은 호출은 이 객체가 있을 때만 실행됩니다.
    QMetaObject::invokeMethod(this, [this, folder, outcome] {
      SetCollect(outcome.ok ? QStringLiteral("done") : QStringLiteral("failed"), outcome.text, outcome.folder);
      if (!outcome.ok) qWarning("%s", qPrintable(outcome.text));
      emit collected(folder, outcome.ok, outcome.text, outcome.folder);
    }, Qt::QueuedConnection);
  }));
}

void AnalysisController::SetCollect(const QString& state, const QString& text, const QString& folder) {
  if (state == collect_state_ && text == collect_text_ && folder == collect_folder_) return;
  collect_state_ = state;
  collect_text_ = text;
  collect_folder_ = folder;
  emit collectChanged();
}

void AnalysisController::SetEvent(LastEvent event) {
  event_ = std::move(event);
  Poll();
}

void AnalysisController::Poll() {
  if (shut_down_) return;
  if (!missing_.isEmpty()) {
    Show(QStringLiteral("missing"), QStringLiteral("관류 분석 프로그램 없음"),
         missing_ + QStringLiteral("\n촬영은 할 수 있고 분석만 하지 않습니다"), {}, 0, 0, 0, true, false, {});
    return;
  }
  if (!analyzer_) {
    if (event_) {
      Show(event_->state, event_->title, event_->detail, {}, 0, 0, 0, true, false, {});
    }
    return;
  }
  const hwlab::platform::AnalyzerStatus s = analyzer_->Status();
  if (s.finished_count != seen_finished_) {
    seen_finished_ = s.finished_count;
    const auto& d = s.last_done;
    const QString name = Utf8(d.name);
    const QString path = Utf8(d.summary_path);
    last_folder_ = Utf8(d.recording_dir);
    if (d.ok) {
      event_ = LastEvent{QStringLiteral("done"), QStringLiteral("분석 완료"), name, QStringLiteral("걸린 시간 ") + Seconds(d.elapsed_ms), path};
      if (const auto quality = ReadQuality(path)) {
        event_->quality_status = Utf8(quality->status);
        event_->quality_text = Utf8(quality->status_ko) +
                               (quality->action_ko.empty() ? QString() : QStringLiteral(": ") + Utf8(quality->action_ko));
        emit changed();  // Show는 품질 문장을 견주지 않으므로 따로 알립니다
      }
    } else if (d.cancelled) {
      event_ = LastEvent{QStringLiteral("cancelled"), QStringLiteral("분석 취소"), name, {}, {}};
    } else {
      event_ = LastEvent{QStringLiteral("failed"), QStringLiteral("분석 실패"),
                         name + QStringLiteral(": ") + Shorten(Utf8(d.error)), {}, {}};
    }
    finished_folders_.removeAll(last_folder_);
    finished_folders_.append(last_folder_);
    while (finished_folders_.size() > 8) finished_folders_.removeFirst();
    emit finished(last_folder_, d.ok, path);
    // 그림은 분석 완료 뒤에 그립니다. 그림까지 끝나면(figures 줄) 결과 창을 다시 읽고 분석결과 폴더로 복사합니다.
    if (d.ok) {
      // 앞 촬영의 그림 줄을 받지 못한 채 다음 분석이 끝났으면 앞 촬영부터 복사합니다.
      if (!figures_folder_.isEmpty()) Collect(figures_folder_);
      figures_folder_ = last_folder_;
      figures_wait_.start();
    }
  }
  if (s.figures_count != seen_figures_) {
    seen_figures_ = s.figures_count;
    if (!figures_folder_.isEmpty()) {
      const QString folder = std::exchange(figures_folder_, QString());
      emit figuresReady(folder);
      Collect(folder);
    }
  }
  // 그림 줄이 오지 않으면(그림을 미루지 않는 관류 분석 프로그램, 그리다 멈춤) 기다리지 않고 복사합니다.
  if (!figures_folder_.isEmpty() && figures_wait_.elapsed() > kFiguresWaitMs) {
    Collect(std::exchange(figures_folder_, QString()));
  }
  const int queued = s.queued;
  using hwlab::platform::AnalyzerState;
  switch (s.state) {
    case AnalyzerState::kBusy: {
      const double progress = Progress(s);
      const QString stage = StageText(s);
      const QString title = stage.isEmpty() ? QStringLiteral("분석 중") : QStringLiteral("분석 중: ") + stage;
      // 하위 단계 문구는 관류 분석 프로그램의 영어라 화면에는 번호만 보이고 원문은 analyzer_log.txt에 남깁니다.
      QString detail = Utf8(s.current_name);
      if (s.sub_total > 0) detail += QStringLiteral("  (세부 %1/%2)").arg(s.sub_step).arg(s.sub_total);
      Show(QStringLiteral("busy"), title, detail, QStringLiteral("경과 ") + Seconds(s.elapsed_ms), s.step, s.total,
           queued, false, true, {}, stage, progress, Remaining(s.elapsed_ms, progress));
      return;
    }
    case AnalyzerState::kStarting:
      Show(QStringLiteral("starting"), QStringLiteral("분석 준비 중"),
           !s.held ? Utf8(s.message)
           : waiting_for_.isEmpty() ? QStringLiteral("녹화가 끝나면 분석 모델을 올립니다")
                                    : waiting_for_ + QStringLiteral(" 분석이 끝나면 분석 모델을 올립니다"),
           {}, 0, 0, queued, false,
           false, {});
      return;
    case AnalyzerState::kFailed:
      Show(QStringLiteral("failed"), QStringLiteral("분석 실패"), Shorten(Utf8(s.message)), {}, 0, 0, queued, true,
           false, {});
      return;
    case AnalyzerState::kStopped:
      Show(QStringLiteral("failed"), QStringLiteral("분석 멈춤"), {}, {}, 0, 0, 0, false, false, {});
      return;
    case AnalyzerState::kIdle:
      break;
  }
  if (queued > 0) {
    Show(QStringLiteral("waiting"), QStringLiteral("분석 대기"),
         !s.held ? QString()
         : waiting_for_.isEmpty() ? QStringLiteral("녹화가 끝나면 시작합니다")
                                  : waiting_for_ + QStringLiteral(" 분석이 끝나면 시작합니다"),
         {}, 0, 0, queued, false, false, {});
    return;
  }
  if (event_) {
    Show(event_->state, event_->title, event_->detail, event_->elapsed, 0, 0, 0, !last_folder_.isEmpty(), false,
         event_->summary_path);
    return;
  }
  Show(QStringLiteral("idle"), QStringLiteral("분석 대기"), QStringLiteral("촬영이 끝나면 자동으로 분석합니다"), {}, 0, 0, 0,
       false, false, {});
}

void AnalysisController::Show(const QString& state, const QString& title, const QString& detail,
                              const QString& elapsed, int step, int total, int queued, bool can_retry,
                              bool can_cancel, const QString& summary_path, const QString& stage_text,
                              double progress, const QString& remaining) {
  if (state == state_ && title == title_ && detail == detail_ && elapsed == elapsed_text_ && step == step_ &&
      total == total_ && queued == queued_ && can_retry == can_retry_ && can_cancel == can_cancel_ &&
      summary_path == summary_path_ && stage_text == stage_text_ && progress == progress_ &&
      remaining == remaining_text_) {
    return;
  }
  stage_text_ = stage_text;
  progress_ = progress;
  remaining_text_ = remaining;
  state_ = state;
  title_ = title;
  detail_ = detail;
  elapsed_text_ = elapsed;
  step_ = step;
  total_ = total;
  queued_ = queued;
  can_retry_ = can_retry;
  can_cancel_ = can_cancel;
  summary_path_ = summary_path;
  emit changed();
}
