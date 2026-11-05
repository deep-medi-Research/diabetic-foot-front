#include "result_view.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "hwlab/frame.h"
#include "hwlab/platform/capture.h"
#include "hwlab/platform/frame_timing.h"
#include "hwlab/platform/perfusion_analysis.h"
#include "hwlab/platform/npy_archive.h"
#include "hwlab/platform/perfusion_detail.h"
#include "hwlab/platform/ppg_sensor.h"
#include "hwlab/platform/template_map.h"
#include "hwlab/record/shot.h"
#include "hwlab/timing/clock_map.h"
#include "hwlab/timing/window_index.h"
#include "analysis_variants.h"

namespace {

using hwlab::platform::PadVerdict;
using hwlab::record::FootSide;

// RGB는 가로세로 2화소마다 하나를 그립니다. 결과 창의 영상 칸은 원본의 절반보다 작습니다.
constexpr int32_t kRgbStep = 2;
// 표시 범위가 기록되지 않은 열화상은 첫 프레임에서 아래위 0.1%를 뺀 범위를 씁니다(미리보기와 같은 값).
constexpr int32_t kLowPermille = 1;
constexpr int32_t kHighPermille = 999;

std::optional<QByteArray> ReadFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
  return file.readAll();
}

std::string Utf8(const QString& text) { return text.toUtf8().toStdString(); }

QString FootName(FootSide foot) { return foot == FootSide::kLeft ? QStringLiteral("왼발") : QStringLiteral("오른발"); }

QString Number(double value, int digits) {
  return std::isfinite(value) ? QString::number(value, 'f', digits) : QStringLiteral("-");
}

// 커프 단계 기준값을 맞춘 고정 노출입니다. 지그 조명에서 -5(2026-10-27까지), 외부 배터리 조명 최대 밝기에서 -9입니다.
// 관류 분석 프로그램 v9가 인정하는 고정 노출과 같습니다.
bool IsFixedExposure(double step) { return step == -5 || step == -9; }

// 커프 상태 문자와 발목 혈압 단계 문자를 결과 창의 네 칸으로 바꿉니다. 발목 혈압 단계 M과 S는
// 폐색(O) 한 칸입니다. 옛 요약의 M, S도 같은 칸입니다.
int StateIndex(const std::string& code) {
  if (code == "0") return 0;
  if (code == "V") return 1;
  if (code == "D") return 2;
  if (code == "O" || code == "M" || code == "S") return 3;
  return -1;
}

QString StateName(int index) {
  static const char* const kNames[] = {"정상", "정상 (경계)", "경증", "폐색"};
  return index >= 0 && index < 4 ? QString::fromUtf8(kNames[index]) : QString();
}

// 촬영 폴더 이름 + "_shot.json"이 매니페스트입니다. 없으면 폴더 안 하나뿐인 "*_shot.json"을 씁니다.
// 파형을 -1~1로 맞춥니다. 관류 분석 프로그램의 맥박 파형은 상대 단위라 크기보다 모양과 박자를 봅니다.
QVariantList Normalized(const std::vector<double>& values) {
  double peak = 0;
  for (const double v : values) {
    if (std::isfinite(v)) peak = std::max(peak, std::abs(v));
  }
  QVariantList out;
  out.reserve(static_cast<qsizetype>(values.size()));
  for (const double v : values) out.append(std::isfinite(v) && peak > 0 ? QVariant(v / peak) : QVariant());
  return out;
}

QVariantList ToList(const std::vector<double>& values) {
  QVariantList out;
  for (const double v : values) out.append(std::isfinite(v) ? QVariant(v) : QVariant());
  return out;
}

// 맥박 파형의 자기상관이 가장 큰 주기로 심박수를 구합니다. 40~180회/분 범위만 봅니다.
std::optional<double> PulseRate(const std::vector<double>& a, const std::vector<double>& b, double fps) {
  const size_t n = std::min(a.size(), b.size());
  if (n < 4 || !std::isfinite(fps) || fps <= 0) return std::nullopt;
  std::vector<double> x(n);
  double mean = 0;
  for (size_t i = 0; i < n; ++i) mean += (x[i] = a[i] + b[i]);
  mean /= static_cast<double>(n);
  for (double& v : x) v -= mean;
  const auto lo = static_cast<size_t>(fps * 60.0 / 180.0);
  const auto hi = std::min(n / 2, static_cast<size_t>(fps * 60.0 / 40.0));
  double best = 0;
  size_t best_lag = 0;
  for (size_t lag = std::max<size_t>(1, lo); lag <= hi; ++lag) {
    double c = 0;
    for (size_t i = 0; i + lag < n; ++i) c += x[i] * x[i + lag];
    if (c > best) {
      best = c;
      best_lag = lag;
    }
  }
  if (best_lag == 0) return std::nullopt;
  return 60.0 * fps / static_cast<double>(best_lag);
}

// 그래프 세로축 위끝입니다. 최댓값보다 크고 읽기 쉬운 값(1, 2, 5 단위)을 고릅니다.
double NiceCeil(double value) {
  if (!std::isfinite(value) || value <= 0) return 0.1;
  const double base = std::pow(10.0, std::floor(std::log10(value)));
  for (const double m : {1.0, 2.0, 2.5, 5.0, 10.0}) {
    if (m * base >= value) return m * base;
  }
  return 10 * base;
}

QString FindManifest(const QDir& dir) {
  const QString named = dir.filePath(dir.dirName() + QStringLiteral("_shot.json"));
  if (QFileInfo::exists(named)) return named;
  const QStringList found = dir.entryList({QStringLiteral("*_shot.json")}, QDir::Files);
  return found.size() == 1 ? dir.filePath(found.first()) : QString();
}

// 측정 오류 구간은 PPG 줄에 칠하고, 값마다의 양은 PPG 사이드카의 rgb_window와 같은 구간(RGB 첫 장부터 마지막 장까지)에서 셉니다.
void AddPpgErrors(const hwlab::platform::PpgCsv& csv, const std::vector<hwlab::Timestamp>& rgb_time, double duration,
                  QVariantMap* chart) {
  std::vector<hwlab::Timestamp> times;
  std::vector<uint8_t> errors;
  times.reserve(csv.common_time_us.size());
  errors.reserve(csv.measure_error.size());
  for (size_t i = 0; i < csv.common_time_us.size() && i < csv.measure_error.size(); ++i) {
    times.push_back({csv.common_time_us[i]});
    errors.push_back(static_cast<uint8_t>(csv.measure_error[i]));
  }
  const int64_t origin = rgb_time.front().us;
  QVariantList spans;
  for (const hwlab::record::PpgErrorSpan& span : hwlab::record::FindPpgErrorSpans(times, errors)) {
    const double start = static_cast<double>(span.begin.us - origin) / 1e6;
    const double end = static_cast<double>(span.end.us - origin) / 1e6 + 1.0 / hwlab::platform::kPpgSampleHz;
    if (end < 0 || start > duration) continue;
    spans.append(QVariantMap{
        {QStringLiteral("code"), QString(QChar(span.code))},
        {QStringLiteral("name"), QString::fromStdString(hwlab::platform::PpgMeasureErrorText(span.code))},
        {QStringLiteral("start"), start},
        {QStringLiteral("end"), end}});
  }
  const hwlab::record::PpgErrorWindow window =
      hwlab::record::SummarizePpgErrors(times, errors, rgb_time.front(), rgb_time.back());
  QVariantList shares;
  for (const hwlab::record::PpgErrorShare& share : window.codes) {
    shares.append(QVariantMap{
        {QStringLiteral("code"), QString(QChar(share.code))},
        {QStringLiteral("text"), QString::fromStdString(hwlab::platform::PpgErrorShareText(share, window.samples))}});
  }
  (*chart)[QStringLiteral("ppgErrors")] = spans;
  (*chart)[QStringLiteral("ppgErrorShares")] = shares;
}

}  // namespace

ResultView::ResultView(QQmlEngine* engine, QObject* parent) : QObject(parent), engine_(engine) {
  engine_->addImageProvider(QStringLiteral("result"), new ResultImageProvider(this));
}

ResultView::~ResultView() {
  if (window_) delete window_.data();
  // 엔진이 이 객체보다 오래 있을 수 있어 이 객체를 가리키는 제공자를 지웁니다.
  engine_->removeImageProvider(QStringLiteral("result"));
}

bool ResultView::Open(const QString& shot_folder, QString* error, bool awaiting) {
  awaiting_ = awaiting;
  Load(shot_folder, false);
  if (!window_) {
    QQmlComponent component(engine_, QStringLiteral("HwLab"), QStringLiteral("ResultWindow"));
    QObject* object = component.createWithInitialProperties({{QStringLiteral("result"), QVariant::fromValue(this)}});
    window_ = qobject_cast<QQuickWindow*>(object);
    if (!window_) {
      delete object;
      if (error != nullptr) *error = component.errorString();
      return false;
    }
  }
  // 촬영 창처럼 최대화로 엽니다. 최대화를 풀면 ResultWindow.qml의 크기(1440x860)가 됩니다.
  window_->showMaximized();
  window_->raise();
  window_->requestActivate();
  if (error != nullptr) *error = error_;
  return summary_.has_value();
}

bool ResultView::Refresh(const QString& shot_folder) {
  if (!window_ || !window_->isVisible()) return false;
  if (QDir::cleanPath(QDir(shot_folder).absolutePath()) != shot_folder_) return false;
  // 판 하나가 끝나도 켜 둔 다른 판이 남아 있으면 그 탭을 "분석 중"으로 둡니다.
  const auto* variants = qobject_cast<const AnalysisVariants*>(analysis_);
  awaiting_ = awaiting_ && variants != nullptr && !variants->Finished(shot_folder);
  Load(shot_folder, true);
  return true;
}

QQuickWindow* ResultView::Window() const { return window_.data(); }

void ResultView::Close() {
  if (window_) window_->close();
}

void ResultView::SetCollectText(const QString& shot_folder, const QString& text) {
  if (QDir::cleanPath(QDir(shot_folder).absolutePath()) != shot_folder_ || text == collect_text_) return;
  collect_text_ = text;
  emit collectChanged();
}

void ResultView::setCurrentVariant(const QString& id) {
  if (id == variant_ || shot_folder_.isEmpty()) return;
  variant_ = id;
  Load(shot_folder_, true);
}

void ResultView::ChooseVariant(const QDir& dir) {
  const auto* variants = qobject_cast<const AnalysisVariants*>(analysis_);
  const QString name = dir.dirName();
  const QDir perfusion(dir.filePath(QString::fromLatin1(hwlab::platform::kPerfusionFolderName)));
  const auto has_summary = [&](const QString& id) {
    const std::string out = hwlab::platform::PerfusionOutputDir(Utf8(dir.path()), Utf8(id));
    return QFileInfo::exists(QString::fromStdString(hwlab::platform::PerfusionSummaryPath(out, Utf8(name))));
  };
  // 앱이 아는 판을 판 순서로 먼저 두고, 앱이 모르는 판 폴더(다른 판의 앱으로 분석한 결과)는 이름 순으로 뒤에 둡니다.
  QStringList ids = variants ? variants->Ids() : QStringList();
  for (const QString& sub : perfusion.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
    if (!ids.contains(sub) && has_summary(sub)) ids.append(sub);
  }
  variant_tabs_.clear();
  QStringList shown;
  for (const QString& id : ids) {
    const bool done = has_summary(id);
    const auto* lane = variants ? qobject_cast<const AnalysisController*>(variants->lane(id)) : nullptr;
    if (!done && !(awaiting_ && lane && lane->enabled())) continue;
    variant_tabs_.append(QVariantMap{{QStringLiteral("id"), id},
                                     {QStringLiteral("label"), variants ? variants->LabelOf(id) : id},
                                     {QStringLiteral("done"), done}});
    shown.append(id);
  }
  if (has_summary(QString())) {
    variant_tabs_.append(QVariantMap{{QStringLiteral("id"), QString()},
                                     {QStringLiteral("label"), QStringLiteral("이전 분석")},
                                     {QStringLiteral("done"), true}});
    shown.append(QString());
  }
  if (shown.contains(variant_)) return;
  // 보던 판이 없으면 결과가 있는 첫 판, 그것도 없으면 첫 탭(분석을 기다리는 판)을 보입니다.
  for (const QVariant& tab : variant_tabs_) {
    if (tab.toMap().value(QStringLiteral("done")).toBool()) {
      variant_ = tab.toMap().value(QStringLiteral("id")).toString();
      return;
    }
  }
  variant_ = shown.isEmpty() ? (ids.isEmpty() ? QString() : ids.front()) : shown.front();
}

void ResultView::Load(const QString& shot_folder, bool keep_position) {
  const int kept = position_;
  // 신호를 보내면 화면이 곧바로 영상을 요청해 Frame이 같은 잠금을 잡으므로, 신호는 잠금을 푼 뒤에 보냅니다.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const QDir dir(QDir(shot_folder).absolutePath());
    shot_name_ = dir.dirName();
    shot_folder_ = QDir::cleanPath(dir.path());
    ChooseVariant(dir);
    result_folder_ = QDir::toNativeSeparators(
        QString::fromStdString(hwlab::platform::PerfusionOutputDir(Utf8(dir.path()), Utf8(variant_))));
    collect_folder_.clear();
    if (const auto split = hwlab::record::SplitShotFolderPath(Utf8(shot_folder_))) {
      collect_folder_ = QDir::toNativeSeparators(QDir(QString::fromStdString(split->root))
                                                     .filePath(QString::fromUtf8(hwlab::record::kCollectedResultFolder)));
    }
    error_.clear();
    video_note_.clear();
    thermal_note_.clear();
    gif_url_.clear();
    summary_.reset();
    pixel_left_foot_.reset();
    criterion_.reset();
    ankle_level_.clear();
    light_source_.clear();
    light_product_.clear();
    left_camera_.reset();
    right_camera_.reset();
    rgb_width_ = 0;
    rgb_height_ = 0;
    rgb_fps_.reset();
    detail_.reset();
    template_top_.reset();
    summary_figures_.clear();
    ++load_revision_;
    position_ = 0;
    frame_count_ = 0;
    left_time_.clear();
    right_map_.clear();
    thermal_map_.clear();
    gif_map_.clear();
    left_reader_.reset();
    right_reader_.reset();
    thermal_path_.clear();

    const std::string out_dir = Utf8(QDir::fromNativeSeparators(result_folder_));
    const QString summary_path = QString::fromStdString(hwlab::platform::PerfusionSummaryPath(out_dir, Utf8(shot_name_)));
    if (const auto text = ReadFile(summary_path)) {
      std::string why;
      summary_ = hwlab::platform::ReadPerfusionSummary(text->toStdString(), &why);
      if (!summary_) error_ = QString::fromStdString(why);
    } else if (awaiting_) {
      error_ = QStringLiteral("분석 중입니다. 끝나면 이 창에 결과를 채웁니다");
    } else {
      error_ = QStringLiteral("요약 파일이 없습니다: ") + QDir::toNativeSeparators(summary_path);
    }
    const QString gif = QString::fromStdString(hwlab::platform::PerfusionGifPath(out_dir, Utf8(shot_name_)));
    if (QFileInfo::exists(gif)) gif_url_ = QUrl::fromLocalFile(gif);

    const QString manifest_path = FindManifest(dir);
    const auto manifest_text = manifest_path.isEmpty() ? std::nullopt : ReadFile(manifest_path);
    std::string why;
    const auto manifest =
        manifest_text ? hwlab::platform::ReadShotManifest(manifest_text->toStdString(), &why) : std::nullopt;
    if (!manifest) {
      video_note_ = manifest_text ? QString::fromStdString(why) : QStringLiteral("촬영 폴더에 매니페스트가 없습니다");
      thermal_note_ = video_note_;
    } else {
      pixel_left_foot_ = manifest->pixel_left_foot;
      criterion_ = manifest->criterion;
      ankle_level_ = QString::fromStdString(manifest->ankle_level);
      light_source_ = manifest->light_source;
      light_product_ = manifest->light_product;
      LoadStreams(dir.path(), *manifest);
    }

    LoadChart(out_dir);
    LoadSummaryFigures(out_dir);

    // GIF의 장은 관류 분석 프로그램이 장 번호(녹화 시작부터 fps로 센 시간)로 나눈 창이므로, 왼발 첫 프레임에서 지난 시간으로 고릅니다.
    if (summary_ && !left_time_.empty()) {
      gif_map_.resize(static_cast<size_t>(frame_count_), 0);
      for (int64_t i = 0; i < frame_count_; ++i) {
        const int64_t t_us = left_time_[static_cast<size_t>(i)].us - left_time_.front().us;
        gif_map_[static_cast<size_t>(i)] = hwlab::timing::WindowIndexAt(t_us, summary_->window_count, summary_->window_us,
                                                                         summary_->window_stride_us)
                                               .value_or(0);
      }
    }
    if (keep_position) position_ = std::clamp(kept, 0, std::max(0, static_cast<int>(frame_count_) - 1));
  }
  // 복사 결과는 이 촬영의 복사가 끝난 뒤 SetCollectText로 받습니다. 같은 촬영을 다시 읽을 때는 받은 문장을 둡니다.
  if (!keep_position && !collect_text_.isEmpty()) {
    collect_text_.clear();
    emit collectChanged();
  }
  emit resultChanged();
  emit positionChanged();
}

void ResultView::LoadChart(const std::string& out_dir) {
  chart_.clear();
  double fps = summary_ && std::isfinite(summary_->fps) && summary_->fps > 0 ? summary_->fps : 30.0;
  double duration = summary_ && summary_->frames > 0 ? static_cast<double>(summary_->frames) / fps : 30.0;
  std::string why;
  const auto detail = hwlab::platform::ReadPerfusionDetail(
      hwlab::platform::PerfusionDetailPath(out_dir, Utf8(shot_name_)), &why);
  if (detail) {
    if (std::isfinite(detail->fps) && detail->fps > 0) fps = detail->fps;
    if (!detail->template_left.empty()) duration = static_cast<double>(detail->template_left.size()) / fps;
    chart_[QStringLiteral("hasWave")] = !detail->template_left.empty();
    chart_[QStringLiteral("waveLeft")] = Normalized(detail->template_left);
    chart_[QStringLiteral("waveRight")] = Normalized(detail->template_right);
    QString note = QStringLiteral("영상에서 뽑은 맥박, %1장 전체, 상대값").arg(detail->template_left.size());
    if (const auto bpm = PulseRate(detail->template_left, detail->template_right, fps)) {
      note += QStringLiteral(".  맥박 약 %1회/분").arg(std::lround(*bpm));
    }
    chart_[QStringLiteral("waveNote")] = note;
    chart_[QStringLiteral("waveScaled")] = false;
    // 관류 분석 프로그램이 30초 맥박을 AC(DC 대비 %)로 저장했으면(<name>_waveforms.npz) 그 값으로 그립니다. 없으면 위 상대값입니다.
    const std::string wave_path = out_dir + "/" + Utf8(shot_name_) + "_waveforms.npz";
    if (const auto npz = hwlab::platform::ReadNpz(wave_path)) {
      const auto get = [&](const char* key) -> const std::vector<double>* {
        const auto it = npz->arrays.find(key);
        return it == npz->arrays.end() ? nullptr : &it->second.values;
      };
      const auto* pl = get("pulse_left");
      const auto* pr = get("pulse_right");
      const auto* al = get("ac_left");
      const auto* ar = get("ac_right");
      if (pl && pr && al && ar && pl->size() == pr->size() && !pl->empty()) {
        double top = 0;
        for (const auto* v : {pl, pr}) {
          for (const double x : *v) {
            if (std::isfinite(x)) top = std::max(top, std::abs(x));
          }
        }
        chart_[QStringLiteral("waveScaled")] = true;
        chart_[QStringLiteral("waveLeft")] = ToList(*pl);
        chart_[QStringLiteral("waveRight")] = ToList(*pr);
        chart_[QStringLiteral("waveAcLeft")] = ToList(*al);
        chart_[QStringLiteral("waveAcRight")] = ToList(*ar);
        // 세로축은 심박 성분 기준입니다. 가는 선(밝기 변화 전체)은 잡음이 커서 축을 넘으면 잘라 그립니다.
        chart_[QStringLiteral("waveMax")] = NiceCeil(top * 1.3);
        const auto pi_of = [&](const char* key) {
          const auto* v = get(key);
          return v && !v->empty() ? (*v)[0] : std::numeric_limits<double>::quiet_NaN();
        };
        QString scaled = QStringLiteral("AC(DC 대비 %), 굵은 선은 심박 성분, 가는 선은 조명 보정한 밝기 변화. 30초 PI 왼 %1% 오 %2%")
                             .arg(Number(pi_of("pi_left"), 3), Number(pi_of("pi_right"), 3));
        if (const auto bpm = PulseRate(detail->template_left, detail->template_right, fps)) {
          scaled += QStringLiteral(".  맥박 약 %1회/분").arg(std::lround(*bpm));
        }
        chart_[QStringLiteral("waveNote")] = scaled;
      }
    }
    const auto l = detail->left_series.find("pi");
    const auto r = detail->right_series.find("pi");
    if (!detail->window_centers_s.empty() && l != detail->left_series.end() && r != detail->right_series.end()) {
      double top = 0;
      for (const auto* series : {&l->second, &r->second}) {
        for (const double v : *series) {
          if (std::isfinite(v)) top = std::max(top, v);
        }
      }
      chart_[QStringLiteral("hasPi")] = true;
      chart_[QStringLiteral("piTimes")] = ToList(detail->window_centers_s);
      chart_[QStringLiteral("piLeft")] = ToList(l->second);
      chart_[QStringLiteral("piRight")] = ToList(r->second);
      chart_[QStringLiteral("piMax")] = NiceCeil(top * 1.1);
      chart_[QStringLiteral("piNote")] = QStringLiteral("10초 구간마다 계산, 점은 구간 가운데 시각");
      // 부위별 관류지수(발가락, 앞발, 발 중간, 뒤꿈치)입니다. 네 부위가 모두 있을 때만 그립니다. 세로축은 네 칸이 같습니다.
      QVariantList regions;
      double region_top = 0;
      for (const auto& [key, name] : {std::pair{"pi_toes", "발가락"}, std::pair{"pi_fore", "앞발"},
                                      std::pair{"pi_mid", "발 중간"}, std::pair{"pi_heel", "뒤꿈치"}}) {
        const auto rl = detail->left_series.find(key);
        const auto rr = detail->right_series.find(key);
        if (rl == detail->left_series.end() || rr == detail->right_series.end()) break;
        for (const auto* series : {&rl->second, &rr->second}) {
          for (const double v : *series) {
            if (std::isfinite(v)) region_top = std::max(region_top, v);
          }
        }
        regions.append(QVariantMap{{QStringLiteral("name"), QString::fromUtf8(name)},
                                   {QStringLiteral("left"), ToList(rl->second)},
                                   {QStringLiteral("right"), ToList(rr->second)}});
      }
      if (regions.size() == 4) {
        chart_[QStringLiteral("hasRegions")] = true;
        chart_[QStringLiteral("regions")] = regions;
        chart_[QStringLiteral("regionMax")] = NiceCeil(region_top * 1.05);
      }
    }
    // 시간 창별 좌우 맥박 상관입니다. 낮은 구간은 두 발에서 같은 심박을 찾지 못한 구간입니다.
    if (detail->template_corr.size() == detail->window_centers_s.size() && !detail->template_corr.empty()) {
      chart_[QStringLiteral("hasCorr")] = true;
      chart_[QStringLiteral("corrTimes")] = ToList(detail->window_centers_s);
      chart_[QStringLiteral("corr")] = ToList(detail->template_corr);
    }
    // 표준 발 틀 PI 지도(v8)는 이미지 제공자가 시간 창마다 그리므로 결과 배열을 남겨 둡니다.
    if (detail->HasTemplateMap()) {
      template_top_ = hwlab::platform::TemplateMapTop(*detail);
      if (template_top_) detail_ = detail;
    }
  } else {
    chart_[QStringLiteral("waveMissing")] = awaiting_ ? QStringLiteral("분석이 끝나면 그립니다")
                                                     : QStringLiteral("파형 데이터를 읽지 못했습니다: ") + QString::fromStdString(why);
  }
  if (summary_ && summary_->classified && !summary_->window_probs.empty()) {
    std::vector<double> times = summary_->window_centres_s;
    // 구간 시각이 요약에 없으면 관류 분석 프로그램 기본값(10초 구간, 5초 간격, 가운데 5초부터)으로 둡니다.
    if (times.size() != summary_->window_probs.size()) {
      times.clear();
      for (size_t i = 0; i < summary_->window_probs.size(); ++i) times.push_back(5.0 + 5.0 * static_cast<double>(i));
    }
    chart_[QStringLiteral("hasVotes")] = true;
    chart_[QStringLiteral("voteTimes")] = ToList(times);
    chart_[QStringLiteral("votes")] = ToList(summary_->window_probs);
    chart_[QStringLiteral("threshold")] = summary_->threshold;
    chart_[QStringLiteral("voteWindow")] = summary_->window_us > 0 ? static_cast<double>(summary_->window_us) / 1e6 : 10.0;
  }
  // 커프 단계 모델의 10초 구간별 E입니다. 2026-10-26 이전 요약에는 없어 그리지 않습니다.
  if (summary_ && summary_->cuff_level && !summary_->cuff_level->window_e.empty() &&
      summary_->cuff_level->window_e.size() == summary_->cuff_level->window_centres_s.size()) {
    const auto& c = *summary_->cuff_level;
    chart_[QStringLiteral("hasLevels")] = true;
    chart_[QStringLiteral("levelTimes")] = ToList(c.window_centres_s);
    chart_[QStringLiteral("levels")] = ToList(c.window_e);
    chart_[QStringLiteral("levelCuts")] = ToList(c.cuts);
  }
  chart_[QStringLiteral("fps")] = fps;
  chart_[QStringLiteral("duration")] = duration;
  LoadFrameTiming(fps);
  LoadPpg(duration);
}

void ResultView::LoadFrameTiming(double fps) {
  // 왼발 RGB 프레임 시각으로 프레임 간격을 그립니다. 좌우 RGB는 한 카메라의 같은 프레임을 잘라 써서 시각이 같습니다.
  // 가로축은 다른 그래프와 같은 "프레임 번호 / fps"(관류 분석 프로그램이 보는 시각)입니다. fps가 떨어진 구간이 분석 구간과
  // 겹치는지 보이려는 것이라, 실제 시각이 아니라 분석이 그 프레임을 놓은 자리에 그립니다.
  frame_check_ = {};
  frame_missing_ = 0;
  if (left_time_.size() < 2 || !(fps > 0)) return;
  const int64_t period = static_cast<int64_t>(std::llround(1e6 / fps));
  frame_check_ = hwlab::timing::CheckFrameRate(left_time_, period);
  frame_missing_ = hwlab::timing::CountMissingFrames(hwlab::timing::FindFrameGaps(left_time_, period));
  std::vector<double> times, intervals;
  QVariantList spans;
  double top = 0;
  double span_start = -1, span_end = -1;
  for (size_t i = 1; i < left_time_.size(); ++i) {
    const double t = static_cast<double>(i) / fps;
    const double ms = static_cast<double>(left_time_[i].us - left_time_[i - 1].us) / 1000.0;
    times.push_back(t);
    intervals.push_back(ms);
    top = std::max(top, ms);
    // 주기의 1.2배를 넘은 간격(느린 간격과 누락)이 이어진 자리를 띠로 모읍니다. 1초 안에 다시 나오면 같은 띠입니다.
    if (ms * 1000.0 * 5 > static_cast<double>(period) * 6) {
      const double begin = static_cast<double>(i - 1) / fps;
      if (span_start >= 0 && begin - span_end <= 1.0) {
        span_end = t;
      } else {
        if (span_start >= 0) spans.append(QVariant(QVariantList{span_start, span_end}));
        span_start = begin;
        span_end = t;
      }
    }
  }
  if (span_start >= 0) spans.append(QVariant(QVariantList{span_start, span_end}));
  chart_[QStringLiteral("hasFrames")] = true;
  chart_[QStringLiteral("frameTimes")] = ToList(times);
  chart_[QStringLiteral("frameIntervals")] = ToList(intervals);
  chart_[QStringLiteral("framePeriod")] = static_cast<double>(period) / 1000.0;
  // 세로축은 50ms(1.5배)가 보이게 55ms부터, 큰 누락이 있으면 100ms까지 늘립니다. 그보다 긴 간격은 위 끝에 붙여 그립니다.
  chart_[QStringLiteral("frameMax")] = std::clamp(std::ceil((top + 5) / 10) * 10, 55.0, 100.0);
  chart_[QStringLiteral("slowSpans")] = spans;

  // 분석 구간마다 그 안의 느린 간격 수와 최저 fps입니다. 그래프 읽음 값에 같은 구간의 값으로 보입니다.
  const auto per_window = [&](const char* times_key, double length_s, const char* slow_key, const char* fps_key) {
    const QVariantList centres = chart_.value(QString::fromLatin1(times_key)).toList();
    if (centres.isEmpty()) return;
    std::vector<hwlab::platform::AnalysisWindow> windows;
    for (const QVariant& c : centres) windows.push_back({"", c.toDouble(), length_s});
    QVariantList slow, low;
    for (const auto& r : hwlab::platform::SummarizeFrameWindows(left_time_, fps, windows)) {
      slow.append(static_cast<double>(r.slow_intervals + r.missing_frames));
      low.append(r.min_fps);
    }
    chart_[QString::fromLatin1(slow_key)] = slow;
    chart_[QString::fromLatin1(fps_key)] = low;
  };
  const double pi_window = summary_ && summary_->window_us > 0 ? static_cast<double>(summary_->window_us) / 1e6 : 10.0;
  per_window("piTimes", pi_window, "piSlow", "piMinFps");
  per_window("levelTimes", 10.0, "levelSlow", "levelMinFps");
  per_window("voteTimes", pi_window, "voteSlow", "voteMinFps");
}

void ResultView::LoadPpg(double duration) {
  // PPG는 촬영 폴더의 <촬영 이름>_ppg.csv입니다. 공통 시각을 왼발 RGB 첫 장 기준 초로 바꿔 녹화 구간만 씁니다.
  const std::string path = Utf8(shot_folder_) + "/" + Utf8(shot_name_) + hwlab::platform::kPpgFileSuffix;
  if (!QFileInfo::exists(QString::fromStdString(path))) {
    chart_[QStringLiteral("ppgMissing")] = QStringLiteral("이 촬영에는 PPG 기록이 없습니다");
    return;
  }
  std::string why;
  const auto csv = hwlab::platform::ReadPpgCsv(path, &why);
  if (!csv || left_time_.empty()) {
    chart_[QStringLiteral("ppgMissing")] = csv ? QStringLiteral("RGB 영상 시각이 없어 PPG를 맞추지 못했습니다")
                                                : QString::fromStdString(why);
    return;
  }
  // 500Hz를 5개씩 평균해 100Hz로 그립니다. 화면 폭보다 촘촘한 점은 보이지 않고 그리기만 느려집니다.
  constexpr size_t kGroup = 5;
  const int64_t origin = left_time_.front().us;
  QVariantList times, values;
  double lo = 1e18, hi = -1e18;
  int64_t hr_sum = 0, hr_count = 0, spo2_sum = 0;
  for (size_t i = 0; i + kGroup <= csv->value.size(); i += kGroup) {
    double t = 0, v = 0;
    for (size_t k = 0; k < kGroup; ++k) {
      t += static_cast<double>(csv->common_time_us[i + k] - origin) / 1e6;
      v += csv->value[i + k];
    }
    t /= kGroup;
    v /= kGroup;
    if (t < 0 || t > duration) continue;
    times.append(t);
    values.append(v);
    lo = std::min(lo, v);
    hi = std::max(hi, v);
    if (csv->heart_rate[i] > 0) {
      hr_sum += csv->heart_rate[i];
      spo2_sum += csv->spo2[i];
      ++hr_count;
    }
  }
  if (times.isEmpty()) {
    chart_[QStringLiteral("ppgMissing")] = QStringLiteral("PPG 기록이 RGB 녹화 구간과 겹치지 않습니다");
    return;
  }
  const double pad = std::max(1.0, (hi - lo) * 0.08);
  chart_[QStringLiteral("hasPpg")] = true;
  chart_[QStringLiteral("ppgTimes")] = times;
  chart_[QStringLiteral("ppgValues")] = values;
  chart_[QStringLiteral("ppgMin")] = lo - pad;
  chart_[QStringLiteral("ppgMax")] = hi + pad;
  QString note = QStringLiteral("손가락 센서 원시값, 500Hz를 100Hz로 평균");
  if (hr_count > 0) {
    note += QStringLiteral(".  장치 맥박 평균 %1회/분, SpO2 %2%").arg(std::lround(static_cast<double>(hr_sum) / hr_count))
                .arg(std::lround(static_cast<double>(spo2_sum) / hr_count));
  }
  chart_[QStringLiteral("ppgNote")] = note;
  AddPpgErrors(*csv, left_time_, duration, &chart_);
}

void ResultView::LoadStreams(const QString& folder, const hwlab::platform::ShotManifest& manifest) {
  const QDir dir(folder);
  const auto open_side = [&](const std::string& video, const std::string& sidecar,
                             std::unique_ptr<hwlab::platform::VideoReader>* reader, std::vector<hwlab::Timestamp>* times,
                             std::optional<hwlab::platform::RecordedCameraSettings>* camera) -> QString {
    const auto text = ReadFile(dir.filePath(QString::fromStdString(sidecar)));
    if (sidecar.empty() || !text) return QStringLiteral("사이드카가 없습니다: ") + QString::fromStdString(sidecar);
    std::string why;
    const auto frames = hwlab::platform::ReadSidecarFrames(text->toStdString(), &why);
    if (!frames) return QString::fromStdString(why);
    *camera = frames->camera;
    if (camera == &left_camera_) {
      rgb_width_ = frames->width;
      rgb_height_ = frames->height;
      rgb_fps_ = frames->frame_rate;
    }
    auto opened = hwlab::platform::MakeVideoReader(&why);
    if (!opened || !opened->Open(Utf8(dir.filePath(QString::fromStdString(video))), &why)) {
      return QString::fromStdString(why);
    }
    // 영상 장 수와 사이드카 기록 수가 다르면 앞에서부터 둘 다 있는 장만 씁니다.
    const auto count = std::min<int64_t>(opened->FrameCount(), static_cast<int64_t>(frames->common_time.size()));
    times->assign(frames->common_time.begin(), frames->common_time.begin() + count);
    *reader = std::move(opened);
    return {};
  };

  QString left_problem = open_side(manifest.left_video, manifest.left_sidecar, &left_reader_, &left_time_, &left_camera_);
  if (!left_problem.isEmpty()) {
    video_note_ = QStringLiteral("왼발 영상: ") + left_problem;
    left_time_.clear();
    thermal_note_ = QStringLiteral("왼발 영상 시각이 없어 열화상을 맞추지 못했습니다");
    return;
  }
  frame_count_ = static_cast<int64_t>(left_time_.size());
  std::vector<hwlab::Timestamp> right_time;
  const QString right_problem = open_side(manifest.right_video, manifest.right_sidecar, &right_reader_, &right_time, &right_camera_);
  if (right_problem.isEmpty()) {
    right_map_ = hwlab::timing::NearestFrames(left_time_, right_time);
  } else {
    video_note_ = QStringLiteral("오른발 영상: ") + right_problem;
    right_reader_.reset();
  }

  if (manifest.thermal_raw.empty()) {
    thermal_note_ = QStringLiteral("열화상 없이 찍은 촬영입니다");
    return;
  }
  const auto text = ReadFile(dir.filePath(QString::fromStdString(manifest.thermal_sidecar)));
  std::string why;
  const auto frames = text ? hwlab::platform::ReadSidecarFrames(text->toStdString(), &why) : std::nullopt;
  if (!frames) {
    thermal_note_ = text ? QString::fromStdString(why) : QStringLiteral("열화상 사이드카가 없습니다");
    return;
  }
  thermal_path_ = Utf8(dir.filePath(QString::fromStdString(manifest.thermal_raw)));
  const qint64 frame_bytes = static_cast<qint64>(frames->width) * frames->height * 2;
  const qint64 file_frames = QFileInfo(QString::fromStdString(thermal_path_)).size() / frame_bytes;
  if (file_frames <= 0) {
    thermal_note_ = QStringLiteral("열화상 원시값 파일이 없거나 비었습니다");
    thermal_path_.clear();
    return;
  }
  thermal_frames_ = *frames;
  if (static_cast<qint64>(thermal_frames_.common_time.size()) > file_frames) {
    thermal_frames_.common_time.resize(static_cast<size_t>(file_frames));
  }
  if (thermal_frames_.display_range) {
    thermal_range_ = *thermal_frames_.display_range;
  } else if (hwlab::platform::ReadRaw16Frame(thermal_path_, frames->width, frames->height, 0, &y16_)) {
    hwlab::FrameView view;
    view.format = hwlab::PixelFormat::kY16;
    view.width = frames->width;
    view.height = frames->height;
    view.plane[0] = reinterpret_cast<const uint8_t*>(y16_.data());
    view.row_stride[0] = frames->width * 2;
    thermal_range_ = hwlab::pixel::FindPercentileRange(view, {0, 0, frames->width, frames->height}, kLowPermille,
                                                       kHighPermille);
  }
  thermal_map_ = hwlab::timing::NearestFrames(left_time_, thermal_frames_.common_time);
}

void ResultView::setPosition(int position) {
  const int clamped = std::clamp(position, 0, std::max(0, frameCount() - 1));
  if (clamped == position_) return;
  position_ = clamped;
  emit positionChanged();
}

void ResultView::openFolder() { QDesktopServices::openUrl(QUrl::fromLocalFile(result_folder_)); }

void ResultView::openCollectFolder() {
  if (!collect_folder_.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(collect_folder_));
}

QImage ResultView::Frame(const QString& id) {
  const QStringList parts = id.split(QLatin1Char('/'));
  if (parts.size() < 3) return {};
  std::lock_guard<std::mutex> lock(mutex_);
  bool ok = false;
  // 표준 발 틀 PI 지도는 프레임 번호가 아니라 시간 창 번호로 받습니다. 영상이 없어도 그립니다.
  if (parts[1] == QStringLiteral("tmap")) {
    const int window = parts[2].toInt(&ok);
    return ok ? TemplateMapImage(window) : QImage();
  }
  const int64_t position = parts[2].toLongLong(&ok);
  if (!ok || position < 0 || position >= frame_count_) return {};
  const auto at = static_cast<size_t>(position);
  if (parts[1] == QStringLiteral("left")) return RgbImage(left_reader_.get(), position);
  if (parts[1] == QStringLiteral("right") && at < right_map_.size()) {
    return RgbImage(right_reader_.get(), static_cast<int64_t>(right_map_[at]));
  }
  if (parts[1] == QStringLiteral("thermal") && at < thermal_map_.size()) {
    return ThermalImage(static_cast<int64_t>(thermal_map_[at]));
  }
  return {};
}

QImage ResultView::RgbImage(hwlab::platform::VideoReader* reader, int64_t index) {
  if (reader == nullptr || !reader->ReadFrame(index, &uyvy_)) return {};
  hwlab::FrameView view;
  view.format = hwlab::PixelFormat::kUyvy;
  view.width = reader->Width();
  view.height = reader->Height();
  view.plane[0] = uyvy_.data();
  view.row_stride[0] = view.width * 2;
  const int32_t width = view.width / kRgbStep;
  const int32_t height = view.height / kRgbStep;
  std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
  if (!hwlab::pixel::UyvyToRgbPreview(view, kRgbStep, rgb)) return {};
  QImage image(width, height, QImage::Format_RGB888);
  for (int32_t y = 0; y < height; ++y) {
    std::memcpy(image.scanLine(y), rgb.data() + static_cast<size_t>(y) * width * 3, static_cast<size_t>(width) * 3);
  }
  return image;
}

QImage ResultView::ThermalImage(int64_t index) {
  const int32_t width = thermal_frames_.width;
  const int32_t height = thermal_frames_.height;
  if (thermal_path_.empty() || !hwlab::platform::ReadRaw16Frame(thermal_path_, width, height, index, &y16_)) return {};
  hwlab::FrameView view;
  view.format = hwlab::PixelFormat::kY16;
  view.width = width;
  view.height = height;
  // 원시값 파일과 이 앱이 도는 PC(x86-64, Apple Silicon)가 모두 리틀 엔디언이라 uint16 배열을 바이트로 그대로 넘깁니다.
  view.plane[0] = reinterpret_cast<const uint8_t*>(y16_.data());
  view.row_stride[0] = width * 2;
  std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
  if (!hwlab::pixel::Y16ToRgbPreview(view, thermal_range_, hwlab::pixel::Palette::kIron, 1, rgb)) return {};
  QImage image(width, height, QImage::Format_RGB888);
  for (int32_t y = 0; y < height; ++y) {
    std::memcpy(image.scanLine(y), rgb.data() + static_cast<size_t>(y) * width * 3, static_cast<size_t>(width) * 3);
  }
  return image;
}

int ResultView::verdict() const {
  if (!summary_) return 0;
  switch (hwlab::platform::PadVerdictOf(*summary_)) {
    case PadVerdict::kNone:
      return 0;
    case PadVerdict::kNormal:
      return 1;
    case PadVerdict::kPadLike:
      return 2;
  }
  return 0;
}

QString ResultView::cuffScoreText() const {
  return hasCuffLevel() ? Number(summary_->cuff_level->e, 2) + QStringLiteral(" (0~3)") : QStringLiteral("-");
}

QString ResultView::cuffCutsText() const {
  if (!hasCuffLevel() || summary_->cuff_level->cuts.empty()) return QStringLiteral("-");
  QStringList cuts;
  for (const double cut : summary_->cuff_level->cuts) cuts.append(Number(cut, 2));
  return cuts.join(QStringLiteral(", "));
}

QString ResultView::cuffSettingText() const {
  if (!hasCuffLevel()) return QStringLiteral("-");
  const auto& c = *summary_->cuff_level;
  const QString gain = std::isfinite(c.gain) ? QStringLiteral(", 게인 ") + Number(c.gain, 0) : QString();
  if (c.setting_mode == "manual") {
    return QStringLiteral("수동, 노출 ") + (std::isfinite(c.exposure_step) ? Number(c.exposure_step, 0) : QStringLiteral("기록 없음")) +
           gain;
  }
  if (c.setting_mode == "auto") return QStringLiteral("자동") + gain;
  return QStringLiteral("기록 없음");
}

bool ResultView::cuffFixedSetting() const {
  if (!hasCuffLevel()) return false;
  const auto& c = *summary_->cuff_level;
  return c.setting_mode == "manual" && IsFixedExposure(c.exposure_step) && c.gain == 1;
}

QVariantList ResultView::cuffRuleRows() const {
  if (!cuffRuleBased()) return {};
  const auto& c = *summary_->cuff_level;
  const auto row = [](const QString& name, const QString& value, const QString& rule, bool hit) {
    return QVariant(QVariantList{name, value, rule, hit});
  };
  // v9 이후 요약은 기준값이 있는 규칙이 아니라 네 상태 확률을 냅니다. 고른 상태(품질을 무시한 상태 포함)의 줄을 밝게 보입니다.
  if (c.states) {
    const int chosen = StateIndex(c.level_code_if_quality_ignored);
    const double p[] = {c.p_state_0, c.p_state_v, c.p_state_d, c.p_state_o};
    QVariantList rows;
    for (int k = 0; k < 4; ++k) {
      rows.append(row(StateName(k), Number(p[k], 2), QString(), k == chosen));
    }
    return rows;
  }
  return {
      row(QStringLiteral("120%: 두 발 맥박 동조"), Number(c.h1re_30, 3),
          Number(c.h1re_30_below, 3) + QStringLiteral(" 미만"), c.h1re_30 < c.h1re_30_below),
      row(QStringLiteral("80%: 약한 발 맥박 지연"), Number(c.delay2_30_ms, 1) + QStringLiteral(" ms"),
          Number(c.delay2_30_above_ms, 1) + QStringLiteral(" ms 초과"), c.delay2_30_ms > c.delay2_30_above_ms),
      row(QStringLiteral("40%: 두 발 PI 비"), Number(c.pi_ratio, 3), Number(c.pi_ratio_below, 2) + QStringLiteral(" 미만"),
          c.pi_ratio < c.pi_ratio_below),
  };
}

QString ResultView::cuffQualityText() const {
  if (!cuffRuleBased()) return {};
  const auto& c = *summary_->cuff_level;
  return QStringLiteral("SNR ") + Number(c.snr_db, 1) + QStringLiteral(" dB, 기준 ") + Number(c.quality_snr_db, 1) +
         QStringLiteral(" dB 이상");
}

QString ResultView::cuffQualityFailText() const {
  if (!cuffQualityFailed()) return {};
  const auto& c = *summary_->cuff_level;
  QString without;
  if (const int index = StateIndex(c.level_code_if_quality_ignored); c.states && index >= 0) {
    without = QStringLiteral(" 품질 검사를 빼면 %1입니다.").arg(StateName(index));
  } else if (!c.states && c.level_if_quality_ignored >= 0) {
    without = QStringLiteral(" 품질 검사를 빼면 %1%입니다.").arg(c.level_if_quality_ignored);
  }
  QString text = QStringLiteral("강한 발의 맥박 신호가 약해 단계를 내지 않았습니다.") + without;
  // 측정 품질 안내가 있으면 이유에 맞는 조치(발이 차가움, 움직임 등)를 함께 적습니다.
  if (summary_->quality && !summary_->quality->action_ko.empty()) {
    text += QStringLiteral(" ") + QString::fromStdString(summary_->quality->action_ko);
  }
  return text;
}

QString ResultView::verdictText() const {
  if (summary_ && summary_->cuff_level && !summary_->cuff_level->has_level) return QStringLiteral("측정 품질 부족");
  if (cuffStates()) return StateName(StateIndex(summary_->cuff_level->level_code));
  if (summary_ && summary_->cuff_level) return QStringLiteral("커프 단계 %1%").arg(summary_->cuff_level->level_pct);
  return padVerdictText();
}

QStringList ResultView::cuffGaugeLabels() const {
  if (cuffStates()) return {QStringLiteral("0"), QStringLiteral("V"), QStringLiteral("D"), QStringLiteral("O")};
  return {QStringLiteral("0"), QStringLiteral("40"), QStringLiteral("80"), QStringLiteral("120")};
}

QString ResultView::cuffGaugeValue() const {
  const int index = cuffLevelIndex();
  if (index < 0) return QStringLiteral("-");
  if (cuffStates()) {
    const auto& c = *summary_->cuff_level;
    // 옛 요약의 M, S는 level_ko가 중등도, 중증이라 칸 이름(폐색)을 씁니다.
    if (c.level_ko.empty() || c.level_code == "M" || c.level_code == "S") return StateName(index);
    return QString::fromStdString(c.level_ko);
  }
  return QStringLiteral("%1%").arg(summary_->cuff_level->level_pct);
}

QString ResultView::cuffGaugeCaption() const {
  if (!cuffStates()) return QStringLiteral("수축기 혈압 대비 커프 압력");
  const auto& c = *summary_->cuff_level;
  return QStringLiteral("커프 상태 ") + QString::fromStdString(c.level_code) +
         (std::isfinite(c.pad_probability) ? QStringLiteral(", PAD 확률 ") + Number(c.pad_probability, 2) : QString());
}

QString ResultView::cuffModelText() const {
  if (!cuffStates()) return {};
  return QStringLiteral("신경망 하나로 두 발 맥박과 느린 밝기 변화(0.05~0.5 Hz)에서 네 상태 확률을 계산하고, 확률이 가장 큰 상태를 "
                        "고릅니다. PAD 확률은 경증과 폐색 확률의 합입니다.");
}

QString ResultView::cuffSchemeText() const {
  if (cuffStates()) {
    return QStringLiteral("커프 상태 0(정상), V(정상 경계, 정맥만 막힘), D(경증, 동맥 부분 압박), O(폐색) 중 하나로 추정한 "
                          "값입니다. 촬영 때 고르는 발목 혈압 단계 M과 S는 폐색입니다.");
  }
  return QStringLiteral("커프 압력이 수축기 혈압의 몇 %인지 0, 40, 80, 120% 네 단계 중 하나로 추정한 값입니다.");
}

QString ResultView::padVerdictText() const {
  if (!summary_) return QStringLiteral("결과 없음");
  switch (hwlab::platform::PadVerdictOf(*summary_)) {
    case PadVerdict::kPadLike:
      return summary_->affected_foot ? QStringLiteral("PAD 의심, ") + FootName(*summary_->affected_foot)
                                     : QStringLiteral("PAD 의심");
    case PadVerdict::kNormal:
      return QStringLiteral("PAD 의심 소견 없음");
    case PadVerdict::kNone:
      break;
  }
  if (summary_->classified) return QStringLiteral("판정 낱말을 알 수 없음: ") + QString::fromStdString(summary_->decision);
  return QStringLiteral("판정 없음");
}

QString ResultView::scoreText() const { return summary_ && summary_->classified ? Number(summary_->p_pad, 3) : "-"; }

QString ResultView::thresholdText() const {
  return summary_ && summary_->classified ? Number(summary_->threshold, 3) : "-";
}

QString ResultView::modelText() const {
  return summary_ && !summary_->model.empty() ? QString::fromStdString(summary_->model) : QStringLiteral("-");
}

QString ResultView::affectedFootText() const {
  if (!summary_ || hwlab::platform::PadVerdictOf(*summary_) != PadVerdict::kPadLike) return QStringLiteral("-");
  return summary_->affected_foot ? FootName(*summary_->affected_foot) : QStringLiteral("기록 없음");
}

QString ResultView::windowVotesText() const {
  if (!summary_ || !summary_->classified || summary_->window_probs.empty()) return {};
  const auto above = std::count_if(summary_->window_probs.begin(), summary_->window_probs.end(),
                                   [&](double p) { return std::isfinite(p) && p >= summary_->threshold; });
  return QStringLiteral("판정 구간 %1개 중 %2개가 기준 이상").arg(summary_->window_probs.size()).arg(above);
}

QString ResultView::piLeftText() const { return summary_ ? Number(summary_->pi_left, 3) + "%" : "-"; }

QString ResultView::piRightText() const { return summary_ ? Number(summary_->pi_right, 3) + "%" : "-"; }

QString ResultView::piRatioText() const { return summary_ ? Number(summary_->pi_ratio, 2) : "-"; }

QString ResultView::lowerFootText() const {
  if (!summary_) return QStringLiteral("-");
  if (summary_->lower_foot) return FootName(*summary_->lower_foot);
  return std::isfinite(summary_->pi_left) && std::isfinite(summary_->pi_right) ? QStringLiteral("같음")
                                                                                : QStringLiteral("-");
}

bool ResultView::thermalExcluded() const { return summary_ && summary_->classified && !summary_->thermal_in_verdict; }

QString ResultView::thermalText() const {
  if (summary_ && !summary_->classified && summary_->cuff_level) {
    return QStringLiteral("열화상은 분석에 쓰지 않습니다. 커프 단계는 RGB 영상만으로 추정합니다.");
  }
  if (!summary_ || !summary_->classified) return QStringLiteral("판정이 없습니다");
  if (summary_->thermal_in_verdict) return QStringLiteral("RGB 영상과 열화상을 함께 쓴 판정입니다");
  const std::string reason = hwlab::platform::DescribeThermalSkip(summary_->thermal_skipped);
  return QStringLiteral("열화상을 빼고 RGB 영상만으로 판정했습니다. ") +
         (reason.empty() ? QStringLiteral("요약에 이유 기록이 없습니다.") : QString::fromStdString(reason));
}

QString ResultView::disclaimer() const { return QString::fromUtf8(hwlab::platform::kPerfusionDisclaimer); }

QString ResultView::analyzerText() const {
  if (!summary_ || summary_->upstream_commit.empty()) return {};
  const QString commit = QStringLiteral("분석 코드 ") + QString::fromStdString(summary_->upstream_commit.substr(0, 7));
  return summary_->variant.empty() ? commit : QString::fromStdString(summary_->variant) + QStringLiteral(", ") + commit;
}

QString ResultView::firstFoot() const {
  return pixel_left_foot_ == FootSide::kRight ? QStringLiteral("right") : QStringLiteral("left");
}

QString ResultView::secondFoot() const {
  return pixel_left_foot_ == FootSide::kRight ? QStringLiteral("left") : QStringLiteral("right");
}

QString ResultView::thermalCaption() const {
  if (!pixel_left_foot_) return QStringLiteral("열화상(좌우 기록 없음)");
  // 결과 창은 영상 왼쪽이 오른발이면 좌우를 뒤집어 그리므로 화면 왼쪽은 늘 왼발입니다.
  return QStringLiteral("열화상(왼쪽이 왼발)");
}

QString ResultView::thermalRangeText() const {
  if (!hasThermal()) return {};
  return QStringLiteral("원시값 %1 ~ %2").arg(thermal_range_.low).arg(thermal_range_.high);
}

int ResultView::gifFrame() const {
  const auto at = static_cast<size_t>(position_);
  return at < gif_map_.size() ? gif_map_[at] : 0;
}

QString ResultView::timeText() const {
  if (left_time_.empty()) return {};
  const double now = static_cast<double>(left_time_[static_cast<size_t>(position_)].us - left_time_.front().us) / 1e6;
  const double total = static_cast<double>(left_time_.back().us - left_time_.front().us) / 1e6;
  return QStringLiteral("%1초 / %2초").arg(now, 0, 'f', 2).arg(total, 0, 'f', 2);
}

QVariantList ResultView::regionRows() const {
  QVariantList rows;
  if (!summary_) return rows;
  const auto row = [&](const QString& name, double l, double r) {
    rows.append(QVariant(QVariantList{name, Number(l, 3) + "%", Number(r, 3) + "%"}));
  };
  row(QStringLiteral("발 전체"), summary_->pi_left, summary_->pi_right);
  row(QStringLiteral("발가락"), summary_->left.pi_toes, summary_->right.pi_toes);
  row(QStringLiteral("앞발"), summary_->left.pi_fore, summary_->right.pi_fore);
  row(QStringLiteral("발 중간"), summary_->left.pi_mid, summary_->right.pi_mid);
  row(QStringLiteral("뒤꿈치"), summary_->left.pi_heel, summary_->right.pi_heel);
  return rows;
}

QVariantList ResultView::qualityRows() const {
  QVariantList rows;
  if (!summary_) return rows;
  // 측정 품질 안내(qc)가 있으면 그 항목을 그대로 보입니다. 설명은 경고 문장, 없으면 기준입니다. 계산하지 않은 항목(na)은 흐리게
  // 보입니다. 녹화 프레임 간격은 qc에 없어 느린 간격이 있을 때만 덧붙입니다.
  if (summary_->quality) {
    for (const auto& item : summary_->quality->items) {
      QString value = QString::fromStdString(item.value);
      // 맥박 타이밍 기준(v9)은 관류 분석 프로그램이 고른 템플릿 이름을 영어로 냅니다(footcells.pulse_reference의 source).
      if (item.code == "pulse_timing") {
        if (item.value == "contact PPG") value = QStringLiteral("손가락 PPG");
        if (item.value == "v106w-foot left") value = QStringLiteral("왼발 영상");
        if (item.value == "v106w-foot right") value = QStringLiteral("오른발 영상");
      }
      if (value.isEmpty()) {
        value = QStringLiteral("-");
      } else if (bool ok = false; value.toDouble(&ok), ok) {
        value = QString::number(value.toDouble(), 'g', 3);
      }
      // 발 온도의 단위 "counts"는 열화상 원시값이라 기준 문장에 적혀 있어 값 뒤에 붙이지 않습니다.
      if (!item.unit.empty() && item.unit != "counts" && value != QStringLiteral("-")) {
        value += QStringLiteral(" ") + QString::fromStdString(item.unit);
      }
      const QString note = QString::fromStdString(item.message_ko.empty() ? item.threshold : item.message_ko);
      rows.append(QVariant(QVariantList{QString::fromStdString(item.name_ko), value, note,
                                        item.severity == "warn" || item.severity == "fail", item.severity == "na"}));
    }
    if (left_time_.size() >= 2 && (frame_check_.slow_intervals > 0 || frame_missing_ > 0)) {
      rows.append(QVariant(QVariantList{QStringLiteral("프레임 간격"), QStringLiteral("최저 %1fps").arg(Number(frame_check_.min_fps, 1)),
                                        QString::fromStdString(hwlab::platform::DescribeSlowFrames(frame_check_)), true, false}));
    }
    return rows;
  }
  // 네 번째 값은 관류 분석 프로그램 규칙에 걸렸는지입니다. 검증된 합격 기준이 없으므로 관류 분석 프로그램이 실제로 동작을 바꾸는 경우만 표시합니다.
  const auto row = [&](const QString& name, const QString& value, const QString& note, bool flagged) {
    rows.append(QVariant(QVariantList{name, value, note, flagged}));
  };
  const auto pair = [](double l, double r, int digits) { return Number(l, digits) + QStringLiteral(" / ") + Number(r, digits); };
  const auto& l = summary_->left;
  const auto& r = summary_->right;
  const bool colour = l.mask_method == "colour" || r.mask_method == "colour";
  // v8은 발 셀로 PI를 계산하고(foot_cells), 발 분할 검출 점수와 정확도가 같은 값입니다.
  const bool cells = std::isfinite(summary_->cells_used);
  if (cells) {
    row(QStringLiteral("발 분할 점수"), pair(l.detection_score, r.detection_score, 2),
        colour ? QStringLiteral("분할 모델이 발을 찾지 못한 발이 있어 색 기준으로 발 영역을 잡았습니다")
               : QStringLiteral("왼발 / 오른발, 분할 모델이 발바닥으로 본 화소의 평균 확률, 0~1"),
        colour);
    const int noisy = std::isfinite(summary_->cells_common)
                          ? static_cast<int>(std::lround(summary_->cells_common - summary_->cells_used))
                          : 0;
    row(QStringLiteral("PI 계산 셀"), Number(summary_->cells_used, 0),
        noisy > 0 ? QStringLiteral("두 발 모두에서 보인 셀 100개 중, 잡음이 커 뺀 셀 %1개").arg(noisy)
                  : QStringLiteral("두 발 모두에서 보이고 잡음이 적은 셀 수, 100개 중"),
        false);
  } else {
    row(QStringLiteral("발 검출 점수"), pair(l.detection_score, r.detection_score, 2),
        colour ? QStringLiteral("분할 모델이 발을 찾지 못한 발이 있어 색 기준으로 발 영역을 잡았습니다")
               : QStringLiteral("왼발 / 오른발, 0~1. 0.2 이하면 관류 분석 프로그램이 색 기준으로 바꿉니다"),
        colour);
    row(QStringLiteral("발 영역 정확도"), pair(l.mask_iou, r.mask_iou, 2), QStringLiteral("분할 모델의 예상값, 0~1"), false);
    row(QStringLiteral("관류 계산 블록"), pair(l.blocks, r.blocks, 0), QStringLiteral("발 영역 안의 블록 수"), false);
  }
  if (std::isfinite(l.thermal_mask_dice) || std::isfinite(r.thermal_mask_dice)) {
    // 관류 분석 프로그램은 0.9 미만이면 다른 정렬 방법도 해 보고 겹침이 큰 쪽을 씁니다(multimodal_infer.py).
    const bool low = (std::isfinite(l.thermal_mask_dice) && l.thermal_mask_dice < 0.9) ||
                     (std::isfinite(r.thermal_mask_dice) && r.thermal_mask_dice < 0.9);
    row(QStringLiteral("열화상 발 영역 일치"), pair(l.thermal_mask_dice, r.thermal_mask_dice, 2),
        low ? QStringLiteral("0.9 미만인 발이 있어 관류 분석 프로그램이 다른 정렬 방법도 비교했습니다")
            : QStringLiteral("RGB에서 옮긴 영역과 열화상 분할의 겹침, 0~1"),
        low);
  }
  row(QStringLiteral("좌우 맥박 상관"), Number(summary_->template_corr, 2),
      QStringLiteral("두 발에서 같은 심박을 찾았는지, 1에 가까울수록 같음"), false);
  const bool no_ref = std::isfinite(summary_->light_ref_min) && summary_->light_ref_min <= 0;
  row(QStringLiteral("조명 기준 블록"), Number(summary_->light_ref_min, 0),
      no_ref ? (cells ? QStringLiteral("배경 기준이 없는 구간이 있어 그 구간은 조명 잡음을 빼지 않고 계산했습니다")
                      : QStringLiteral("배경 기준이 없는 구간이 있어 관류 분석 프로그램이 다른 계산 방식(POS)을 썼습니다"))
             : QStringLiteral("조명 잡음 제거에 쓴 배경 블록 수의 최솟값"),
      no_ref);
  // 녹화 프레임 간격입니다. 관류 분석 프로그램은 모든 프레임을 1/fps 간격으로 보므로 fps가 떨어진 구간은 시간축이 어긋납니다.
  if (left_time_.size() >= 2) {
    const bool slow = frame_check_.slow_intervals > 0 || frame_missing_ > 0;
    const QString described = QString::fromStdString(hwlab::platform::DescribeSlowFrames(frame_check_));
    row(QStringLiteral("프레임 간격"),
        slow ? QStringLiteral("최저 %1fps").arg(Number(frame_check_.min_fps, 1)) : QStringLiteral("고름"),
        slow ? (described.isEmpty() ? QStringLiteral("카메라 단 누락 %1장").arg(frame_missing_)
                                    : described + (frame_missing_ > 0 ? QStringLiteral(", 누락 %1장").arg(frame_missing_)
                                                                      : QString())) +
                   QStringLiteral(". 그래프의 주황 띠가 분석 구간과 겹치는지 봅니다")
             : QStringLiteral("주기의 1.2배를 넘은 간격이 없음. 1초 창 최저 %1fps").arg(Number(frame_check_.min_fps, 1)),
        slow);
  }
  // v8 결과 창은 발 분할 점수와 PI 계산 셀을 그림 제목 줄에, 좌우 맥박 상관과 프레임 간격을 그래프로 보이므로
  // 이 표에는 관류 분석 프로그램이 동작을 바꾼 줄만 남깁니다.
  if (cells) {
    QVariantList flagged;
    for (const QVariant& r : rows) {
      if (r.toList().value(3).toBool()) flagged.append(r);
    }
    return flagged;
  }
  return rows;
}

double ResultView::positionSeconds() const {
  const double fps = summary_ && std::isfinite(summary_->fps) && summary_->fps > 0 ? summary_->fps : 30.0;
  return position_ / fps;
}

void ResultView::seekSeconds(double seconds) {
  const double fps = summary_ && std::isfinite(summary_->fps) && summary_->fps > 0 ? summary_->fps : 30.0;
  setPosition(static_cast<int>(std::lround(seconds * fps)));
}

QString ResultView::frameText() const {
  if (frame_count_ <= 0) return {};
  const auto at = static_cast<size_t>(position_);
  QString text = QStringLiteral("RGB %1 / %2").arg(position_ + 1).arg(frame_count_);
  if (at < thermal_map_.size()) {
    text += QStringLiteral("    열화상 %1 / %2").arg(thermal_map_[at] + 1).arg(thermal_frames_.common_time.size());
  }
  if (summary_ && summary_->window_count > 0 && at < gif_map_.size()) {
    const double centre_s = (static_cast<double>(gif_map_[at]) * static_cast<double>(summary_->window_stride_us) +
                             static_cast<double>(summary_->window_us) / 2.0) / 1e6;
    text += QStringLiteral("    관류 지도 %1 / %2 (창 가운데 %3초)")
                .arg(gif_map_[at] + 1)
                .arg(summary_->window_count)
                .arg(centre_s, 0, 'f', 1);
  }
  return text;
}

void ResultView::LoadSummaryFigures(const std::string& out_dir) {
  // 관류 분석 프로그램(v8)이 녹화 전체로 그린 그림입니다. 그림 속 글자는 결과 창 크기에서 읽기 어려워 핵심 값을 제목 줄에 따로 씁니다.
  const auto add = [&](const char* suffix, const QString& title, const QString& detail) {
    const QString path = QString::fromStdString(out_dir + "/" + Utf8(shot_name_) + suffix);
    if (!QFileInfo::exists(path)) return;
    summary_figures_.append(QVariantMap{{QStringLiteral("title"), title},
                                        {QStringLiteral("detail"), detail},
                                        {QStringLiteral("url"), QUrl::fromLocalFile(path)}});
  };
  QString segmentation, cells, windows;
  if (summary_) {
    const auto& l = summary_->left;
    const auto& r = summary_->right;
    segmentation = QStringLiteral("점수 ") + Number(l.detection_score, 2) + QStringLiteral(" / ") + Number(r.detection_score, 2);
    if (std::isfinite(summary_->cells_used)) {
      const double noisy = std::isfinite(summary_->cells_common) ? summary_->cells_common - summary_->cells_used : 0.0;
      cells = QStringLiteral("PI 계산 셀 %1개, 잡음으로 뺀 셀 %2개").arg(Number(summary_->cells_used, 0), Number(noisy, 0));
    }
    if (summary_->window_count > 0) windows = QStringLiteral("10초 구간 %1개 평균").arg(summary_->window_count);
  }
  add("_segmentation.png", QStringLiteral("발 분할"), segmentation);
  add("_template.png", QStringLiteral("양발 정합"), cells);
  add("_pi_map.png", QStringLiteral("표준 발 PI 지도"), windows);
}

QImage ResultView::TemplateMapImage(int32_t window) {
  if (!detail_ || !template_top_) return {};
  // 결과 창의 지도 칸 높이(약 200화소)보다 조금 크게 그려 화면 배율이 1.25여도 흐려지지 않게 합니다.
  constexpr int32_t kFootHeight = 280;
  const auto image = hwlab::platform::DrawTemplateMap(*detail_, window, kFootHeight, *template_top_);
  if (!image) return {};
  QImage out(image->width, image->height, QImage::Format_RGBA8888);
  for (int32_t y = 0; y < image->height; ++y) {
    std::memcpy(out.scanLine(y), image->rgba.data() + static_cast<size_t>(y) * image->width * 4,
                static_cast<size_t>(image->width) * 4);
  }
  return out;
}

QString ResultView::templateMapRangeText() const {
  return template_top_ ? QStringLiteral("0 ~ ") + Number(*template_top_, 3) + QStringLiteral("%") : QString();
}

int ResultView::cuffLevelIndex() const {
  if (!hasCuffLevel() || !summary_->cuff_level->has_level) return -1;
  if (cuffStates()) return StateIndex(summary_->cuff_level->level_code);
  switch (summary_->cuff_level->level_pct) {
    case 0:
      return 0;
    case 40:
      return 1;
    case 80:
      return 2;
    case 120:
      return 3;
    default:
      return -1;
  }
}

QString ResultView::criterionLabel() const {
  return cuffStates() ? QStringLiteral("고른 발목 혈압 단계") : QStringLiteral("촬영 때 입력한 기준");
}

QString ResultView::criterionText() const {
  if (cuffStates()) return StateIndex(ankle_level_.toStdString()) >= 0 ? ankle_level_ : QString();
  return criterion_ ? QStringLiteral("%1%").arg(*criterion_) : QString();
}

bool ResultView::criterionMatches() const {
  if (!hasCuffLevel() || !summary_->cuff_level->has_level) return false;
  if (cuffStates()) {
    const int chosen = StateIndex(ankle_level_.toStdString());
    return chosen >= 0 && chosen == StateIndex(summary_->cuff_level->level_code);
  }
  return criterion_ && summary_->cuff_level->level_pct == *criterion_;
}

QString ResultView::criterionNote() const {
  if (criterionText().isEmpty() || !hasCuffLevel() || !summary_->cuff_level->has_level) return {};
  if (criterionMatches()) return QStringLiteral("추정과 같음");
  if (cuffStates()) return QStringLiteral("추정은 ") + StateName(StateIndex(summary_->cuff_level->level_code));
  return QStringLiteral("추정 %1%와 다름").arg(summary_->cuff_level->level_pct);
}

QVariantList ResultView::piBars() const {
  QVariantList bars;
  if (!summary_) return bars;
  const auto bar = [&](const QString& name, double l, double r) {
    bars.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("left"), l}, {QStringLiteral("right"), r}});
  };
  bar(QStringLiteral("발 전체"), summary_->pi_left, summary_->pi_right);
  bar(QStringLiteral("발가락"), summary_->left.pi_toes, summary_->right.pi_toes);
  bar(QStringLiteral("앞발"), summary_->left.pi_fore, summary_->right.pi_fore);
  bar(QStringLiteral("발 중간"), summary_->left.pi_mid, summary_->right.pi_mid);
  bar(QStringLiteral("뒤꿈치"), summary_->left.pi_heel, summary_->right.pi_heel);
  return bars;
}

double ResultView::piBarMax() const {
  double top = 0;
  for (const QVariant& b : piBars()) {
    for (const char* key : {"left", "right"}) {
      const double v = b.toMap().value(QString::fromLatin1(key)).toDouble();
      if (std::isfinite(v)) top = std::max(top, v);
    }
  }
  return top > 0 ? top : 0.1;
}

QString ResultView::templateCorrText() const { return summary_ ? Number(summary_->template_corr, 2) : QStringLiteral("-"); }

namespace {

// 카메라 설정 한 줄의 값입니다. 두 발이 다르면 "왼발 값 / 오른발 값"으로 씁니다.
QString PairText(const QString& left, const std::optional<QString>& right) {
  return !right || *right == left ? left : left + QStringLiteral(" / ") + *right;
}

QString ExposureText(const hwlab::platform::RecordedCameraSettings& c) {
  if (c.mode == "auto") return QStringLiteral("자동");
  if (!c.exposure_step) return QStringLiteral("기록 없음");
  QString text = QString::number(*c.exposure_step);
  if (c.exposure_ms) text += QStringLiteral(" (%1ms)").arg(Number(*c.exposure_ms, 2));
  return text;
}

QString GainText(const hwlab::platform::RecordedCameraSettings& c) {
  return c.gain ? QString::number(*c.gain) : QStringLiteral("기록 없음");
}

QString WhiteBalanceText(const hwlab::platform::RecordedCameraSettings& c) {
  if (c.white_balance_auto == true) return QStringLiteral("자동");
  if (c.white_balance_k) return QStringLiteral("%1K 고정").arg(*c.white_balance_k);
  return QStringLiteral("기록 없음");
}

QString ModeText(const hwlab::platform::RecordedCameraSettings& c) {
  const QString mode = c.mode == "manual" ? QStringLiteral("수동") : c.mode == "auto" ? QStringLiteral("자동") : QStringLiteral("기록 없음");
  return c.locked == true ? mode + QStringLiteral(", 잠금") : mode;
}

}  // namespace

QVariantList ResultView::cameraRows() const {
  QVariantList rows;
  if (!left_camera_) return rows;
  const auto& l = *left_camera_;
  const auto right = [&](QString (*text)(const hwlab::platform::RecordedCameraSettings&)) -> std::optional<QString> {
    return right_camera_ ? std::optional<QString>(text(*right_camera_)) : std::nullopt;
  };
  // 커프 단계 기준값은 수동, 게인 1, 고정 노출(-5 또는 -9)의 촬영으로 맞췄습니다. 커프 단계가 있는 결과에만 기준과 견줍니다.
  const bool cuff = hasCuffLevel();
  const auto differs = [&](const std::optional<int32_t>& v, int32_t want) { return cuff && (!v || *v != want); };
  const auto exposure_off_of = [&](const hwlab::platform::RecordedCameraSettings& c) {
    return cuff && (c.mode != "manual" || !c.exposure_step || !IsFixedExposure(*c.exposure_step));
  };
  const bool exposure_off = exposure_off_of(l) || (right_camera_ && exposure_off_of(*right_camera_));
  const bool gain_off = differs(l.gain, 1) || (right_camera_ && differs(right_camera_->gain, 1));
  rows.append(QVariant(QVariantList{QStringLiteral("노출"), PairText(ExposureText(l), right(ExposureText)),
                                    cuff ? QStringLiteral("기준 -5, -9") : QString(), exposure_off}));
  rows.append(QVariant(QVariantList{QStringLiteral("게인"), PairText(GainText(l), right(GainText)),
                                    cuff ? QStringLiteral("기준 1") : QString(), gain_off}));
  rows.append(QVariant(QVariantList{QStringLiteral("화이트밸런스"), PairText(WhiteBalanceText(l), right(WhiteBalanceText)),
                                    QString(), false}));
  rows.append(QVariant(QVariantList{QStringLiteral("모드"), PairText(ModeText(l), right(ModeText)), QString(), false}));
  if (rgb_width_ > 0) {
    const QString fps = rgb_fps_ ? QStringLiteral("%1fps, ").arg(*rgb_fps_) : QString();
    rows.append(QVariant(QVariantList{QStringLiteral("영상"), fps + QStringLiteral("%1×%2").arg(rgb_width_).arg(rgb_height_),
                                      QString(), false}));
  }
  if (!light_product_.empty() || !light_source_.empty()) {
    const QString source = light_source_ == "external" ? QStringLiteral("외부 조명")
                           : light_source_ == "app"    ? QStringLiteral("앱 조명")
                                                       : QString::fromStdString(light_source_);
    rows.append(QVariant(QVariantList{QStringLiteral("조명"),
                                      light_product_.empty() ? source : QString::fromStdString(light_product_), QString(), false}));
  }
  return rows;
}

QString ResultView::cameraNote() const {
  if (!left_camera_) return QStringLiteral("사이드카에 기록 없음");
  if (!right_camera_) return QStringLiteral("왼발 카메라");
  const auto& l = *left_camera_;
  const auto& r = *right_camera_;
  const bool same = l.mode == r.mode && l.exposure_step == r.exposure_step && l.gain == r.gain &&
                    l.white_balance_auto == r.white_balance_auto && l.white_balance_k == r.white_balance_k && l.locked == r.locked;
  return same ? QStringLiteral("두 발 같음") : QStringLiteral("왼발 / 오른발");
}
