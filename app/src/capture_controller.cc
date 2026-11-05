#include "capture_controller.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRunnable>
#include <QStandardPaths>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>

#include <vector>

#include "app_version.h"
#include "hwlab/pixel/convert.h"
#include "hwlab/pixel/side_regions.h"
#include "hwlab/platform/ppg_sensor.h"
#include "hwlab/record/shot_limits.h"
#include "remembered_values.h"
#include "shutdown_watch.h"

namespace {

constexpr int kPollMs = 200;
constexpr int kFreeSpaceCheckMs = 10000;  // 촬영하지 않는 동안 저장 위치의 남은 공간을 읽는 간격
constexpr int kPreviewEveryFrames = 3;  // 30fps에서 초당 10장
constexpr int kPreviewStep = 4;         // 1920x1200을 480x300으로 줄입니다
constexpr int32_t kMaxShotNumber = 99;  // 같은 초에 다시 찍을 때 폴더 이름 뒤에 넣는 번호의 최대값
constexpr int kThermalPreviewEveryFrames = 3;  // 30Hz에서 초당 10장
// 창을 닫은 뒤 카메라 정리(Stop)를 기다리는 최대 시간입니다. 정상 종료의 실측 시간이 쌓이면(shutdown_log.txt) 다시 정합니다.
constexpr int kShutdownLimitMs = 20000;
// 녹화 중에 창을 닫으면 녹화를 멈추고 촬영 마무리를 기다리는 최대 시간입니다. 실측 마무리는 약 4초입니다(2026-10-26).
constexpr int kSaveBeforeCloseLimitMs = 30000;
// RGB 카메라가 내는 화소 형식입니다. 좌우 영역의 색차 격자(x와 폭이 짝수)를 정합니다.
constexpr hwlab::PixelFormat kRgbFormat = hwlab::PixelFormat::kUyvy;
// 기본 RGB 발 영역 하나의 최대 폭입니다. 1920x1200에서 가운데 세로선 양쪽 640x1200 두 칸입니다.
constexpr int32_t kFootMaxWidth = 640;
constexpr char kFootKey[] = "regions/pixel_left_foot";
// 화면에서 적용한 카메라 설정입니다. remember가 true일 때만 다음 실행이 이 값으로 시작합니다.
constexpr char kRememberSettingsKey[] = "camera/remember";
constexpr char kModeKey[] = "camera/mode";
constexpr char kExposureKey[] = "camera/exposure_step";
constexpr char kGainKey[] = "camera/gain";
constexpr char kWhiteBalanceKey[] = "camera/white_balance_k";

// 남긴 설정을 읽습니다. 화면에서 고를 수 있는 범위를 벗어나거나 빠진 값이 있으면 쓰지 않습니다(손으로 고친 레지스트리 등).
// fps는 촬영 선언 파일의 값을 그대로 씁니다. 관류 분석 프로그램이 가정하는 값이라 화면에서 바꾸지 않기 때문입니다.
std::optional<hwlab::flow::SettingsSpec> ReadRememberedSettings(const RememberedValues& values,
                                                                const hwlab::flow::SettingsSpec& base) {
  const QString mode = values.Value(QLatin1String(kModeKey)).toString();
  bool exposure_ok = false, gain_ok = false, wb_ok = false;
  const int exposure = values.Value(QLatin1String(kExposureKey)).toInt(&exposure_ok);
  const int gain = values.Value(QLatin1String(kGainKey)).toInt(&gain_ok);
  const int wb = values.Value(QLatin1String(kWhiteBalanceKey)).toInt(&wb_ok);
  if (mode != QStringLiteral("auto") && mode != QStringLiteral("manual")) return std::nullopt;
  if (!exposure_ok || exposure < -13 || exposure > -1) return std::nullopt;
  if (!gain_ok || gain < 0 || gain > 255) return std::nullopt;
  if (!wb_ok || wb < 2800 || wb > 10000) return std::nullopt;
  hwlab::flow::SettingsSpec out = base;
  out.mode = mode == QStringLiteral("auto") ? hwlab::SettingMode::kAuto : hwlab::SettingMode::kManual;
  out.exposure_step = exposure;
  out.gain = gain;
  out.white_balance_k = wb;
  return out;
}

void SaveSettings(RememberedValues& values, const hwlab::flow::SettingsSpec& settings) {
  values.SetValue(QLatin1String(kModeKey), settings.mode == hwlab::SettingMode::kAuto ? "auto" : "manual");
  // 자동 맞춤에서는 노출 단계를 쓰지 않아 0일 수 있습니다. 직접 지정으로 바꿀 때 쓸 수 있게 범위 안의 값만 남깁니다.
  values.SetValue(QLatin1String(kExposureKey), std::clamp(settings.exposure_step, -13, -1));
  values.SetValue(QLatin1String(kGainKey), settings.gain);
  values.SetValue(QLatin1String(kWhiteBalanceKey), settings.white_balance_k);
}

using hwlab::record::FootSide;
using hwlab::record::ThermalAbsence;

// 이번 촬영에 열화상을 녹화할지 정한 결과입니다.
struct ThermalPlan {
  bool record = false;   // 열화상 파일을 녹화함
  bool confirm = false;  // 열화상 없이 찍기 전에 사용자에게 진행 여부를 물음
  ThermalAbsence absence = ThermalAbsence::kNone;  // record가 false일 때 이유
};

// 열화상 녹화 정책입니다. 정책을 바꿀 때는 이 함수만 고칩니다.
// 수신 중일 때만 녹화하고, 아니면 사용자가 진행을 고른 경우에만 RGB만 녹화합니다. 실행할 때 끈 경우는 묻지 않습니다.
ThermalPlan PlanThermal(bool has_camera, ThermalAbsence no_camera, const std::string& device_name,
                        hwlab::platform::ThermalConnection connection) {
  if (!has_camera) {
    const ThermalAbsence why = no_camera == ThermalAbsence::kNone ? ThermalAbsence::kNoDevice : no_camera;
    return {false, why != ThermalAbsence::kDisabled, why};
  }
  // 장치 이름은 연 뒤에 정해지므로, 이름이 없으면 매니페스트 장치 목록에 열화상을 넣을 수 없습니다.
  if (!device_name.empty() && connection == hwlab::platform::ThermalConnection::kReceiving) {
    return {true, false, ThermalAbsence::kNone};
  }
  if (connection == hwlab::platform::ThermalConnection::kWaiting) return {false, true, ThermalAbsence::kPreparing};
  return {false, true, ThermalAbsence::kNotReceiving};
}

QString AbsenceText(ThermalAbsence absence) {
  switch (absence) {
    case ThermalAbsence::kNone: return QStringLiteral("이유 모름");
    case ThermalAbsence::kDisabled: return QStringLiteral("열화상 끔");
    case ThermalAbsence::kNoDevice: return QStringLiteral("열화상 카메라 없음");
    case ThermalAbsence::kNotReceiving: return QStringLiteral("열화상 카메라 인식 안 됨");
    case ThermalAbsence::kPreparing: return QStringLiteral("열화상 카메라 준비 중");
  }
  return {};
}

// 화면 안내의 종류입니다. 장치가 없거나 끊긴 경우와 첫 프레임을 기다리는 경우의 문구가 다릅니다.
QString PromptKind(const ThermalPlan& plan) {
  if (!plan.confirm) return {};
  return plan.absence == ThermalAbsence::kPreparing ? QStringLiteral("preparing") : QStringLiteral("missing");
}

FootSide Opposite(FootSide foot) { return foot == FootSide::kLeft ? FootSide::kRight : FootSide::kLeft; }

QString FootText(FootSide foot) { return foot == FootSide::kLeft ? QStringLiteral("왼발") : QStringLiteral("오른발"); }

bool AnyVideoExists(const hwlab::platform::PreparedShot& shot) {
  for (const std::string* video : {&shot.video, &shot.left_video, &shot.right_video}) {
    if (!video->empty() && QFile::exists(QString::fromStdString(*video))) return true;
  }
  return false;
}

QString RoleText(hwlab::record::FileRole role) {
  using hwlab::record::FileRole;
  switch (role) {
    case FileRole::kLeftVideo: return QStringLiteral("왼발 영상");
    case FileRole::kLeftSidecar: return QStringLiteral("왼발 사이드카");
    case FileRole::kRightVideo: return QStringLiteral("오른발 영상");
    case FileRole::kRightSidecar: return QStringLiteral("오른발 사이드카");
    case FileRole::kThermalRaw: return QStringLiteral("열화상 원시값");
    case FileRole::kThermalSidecar: return QStringLiteral("열화상 사이드카");
    case FileRole::kThermalPreview: return QStringLiteral("열화상 확인용 영상");
    case FileRole::kManifest: return QStringLiteral("매니페스트");
    case FileRole::kFullVideo: return QStringLiteral("전체 화면 영상");
    case FileRole::kFullSidecar: return QStringLiteral("전체 화면 사이드카");
    case FileRole::kThermalLeftArray: return QStringLiteral("왼발 열화상 배열");
    case FileRole::kThermalRightArray: return QStringLiteral("오른발 열화상 배열");
  }
  return {};
}

// 분석 전 확인 결과를 결과 줄 문장과 상태("ready", "warning", "blocked")로 바꿉니다.
std::pair<QString, QString> AnalysisVerdict(const std::vector<hwlab::record::InputFinding>& findings) {
  using hwlab::record::InputProblem;
  QStringList blocking;
  QStringList warnings;
  for (const auto& f : findings) {
    QString text;
    switch (f.problem) {
      case InputProblem::kMissingFile: text = QStringLiteral("파일 없음: ") + RoleText(f.role); break;
      case InputProblem::kUnreadableSidecar: text = QStringLiteral("읽지 못함: ") + RoleText(f.role); break;
      case InputProblem::kFewThermalFrames:
        text = QStringLiteral("RGB 녹화 구간 열화상 %1장(최소 %2장)").arg(f.value).arg(f.limit);
        break;
      case InputProblem::kNotPortrait:
        text = QStringLiteral("%1 세로형 아님(폭 %2, 높이 %3)").arg(RoleText(f.role)).arg(f.value).arg(f.limit);
        break;
      case InputProblem::kThermalGap: text = QStringLiteral("열화상 누락 %1장").arg(f.value); break;
      case InputProblem::kThermalAbsent: text = QStringLiteral("열화상 파일 없음"); break;
    }
    if (f.blocking) {
      blocking << text;
    } else {
      warnings << text;
    }
  }
  const QString warning_line = warnings.isEmpty() ? QString() : QStringLiteral("\n경고: ") + warnings.join(QStringLiteral(", "));
  if (!blocking.isEmpty()) {
    return {QStringLiteral("분석 불가\n") + blocking.join(QStringLiteral("\n")) + warning_line, QStringLiteral("blocked")};
  }
  if (!warnings.isEmpty()) return {QStringLiteral("분석 가능") + warning_line, QStringLiteral("warning")};
  return {QStringLiteral("분석 가능"), QStringLiteral("ready")};
}

QString PhaseName(hwlab::flow::Phase phase) {
  switch (phase) {
    case hwlab::flow::Phase::kPreview: return QStringLiteral("미리보기");
    case hwlab::flow::Phase::kSettling: return QStringLiteral("안정화");
    case hwlab::flow::Phase::kRecording: return QStringLiteral("녹화");
    case hwlab::flow::Phase::kDisconnected: return QStringLiteral("연결 끊김");
  }
  return {};
}

QString EndName(hwlab::flow::EndReason reason) {
  switch (reason) {
    case hwlab::flow::EndReason::kCompleted: return QStringLiteral("완료");
    case hwlab::flow::EndReason::kStopped: return QStringLiteral("중지");
    case hwlab::flow::EndReason::kSettingChanged: return QStringLiteral("녹화 중 설정 변경으로 끝남");
    case hwlab::flow::EndReason::kDisconnected: return QStringLiteral("연결 끊김으로 끝남");
  }
  return {};
}

template <typename T>
QString Text(const std::optional<T>& value) {
  if (!value) return QStringLiteral("모름");
  if constexpr (std::is_same_v<T, bool>) return *value ? QStringLiteral("예") : QStringLiteral("아니오");
  return QString::number(*value);
}

// 열화상 프레임 크기를 아직 받지 못했을 때 디스크 추정에 쓰는 크기와, 기록 주기가 0일 때의 장치 최대 Hz입니다.
constexpr hwlab::record::VideoSize kThermalFallbackSize{640, 480};
constexpr int32_t kThermalMaxRateHz = 30;

// 촬영 한 번이 디스크에 쓸 양의 입력입니다. 촬영 버튼을 누를 때와 남은 공간 표시가 같은 값으로 셉니다.
hwlab::record::ShotSizePlan SizePlanOf(const hwlab::flow::CaptureSpec& spec, hwlab::platform::SavedVideos videos,
                                       const hwlab::pixel::SideRegions& regions, hwlab::record::VideoSize thermal_size,
                                       bool thermal) {
  using hwlab::platform::SavedVideos;
  hwlab::record::ShotSizePlan size;
  size.rgb_frames = spec.target_frames;
  size.rgb_frame_rate = spec.settings.sdk_frame_rate;
  if (videos != SavedVideos::kFeet) size.rgb_videos.push_back({spec.width, spec.height});
  if (videos != SavedVideos::kFull) {
    size.rgb_videos.push_back({regions.left.width, regions.left.height});
    size.rgb_videos.push_back({regions.right.width, regions.right.height});
  }
  size.thermal = thermal;
  size.thermal_size = thermal_size.width > 0 && thermal_size.height > 0 ? thermal_size : kThermalFallbackSize;
  size.thermal_record_period_us = spec.thermal_record_period_us;
  size.thermal_max_rate_hz = kThermalMaxRateHz;
  return size;
}

// 촬영 폴더를 만들기 전에 경로 길이와 디스크 여유를 봅니다. 통과하면 빈 문자열입니다.
std::string ShotLimitProblem(const std::string& root, const hwlab::record::ShotInfo& info,
                             const hwlab::flow::CaptureSpec& spec, hwlab::platform::SavedVideos videos,
                             const hwlab::pixel::SideRegions& regions, hwlab::record::VideoSize thermal_size,
                             const std::vector<hwlab::record::ShotDevice>& devices, std::optional<int64_t> free_override) {
  const auto layout = hwlab::platform::PlanShotLayout(info, spec.codec, videos, devices);
  if (!layout) return {};  // 촬영 정보 오류는 PrepareShot이 이유를 남깁니다
  bool thermal = false;
  for (const auto& device : devices) thermal = thermal || device.stream == hwlab::StreamKind::kThermal;
  const hwlab::record::ShotSizePlan size = SizePlanOf(spec, videos, regions, thermal_size, thermal);
  const std::optional<int64_t> free_bytes = free_override ? free_override : hwlab::platform::FreeBytesAt(root);
  // 같은 초 재촬영 번호가 들어간 가장 긴 이름으로 셉니다.
  return hwlab::platform::CheckShotLimits(root, hwlab::record::NumberLayout(*layout, kMaxShotNumber),
                                          hwlab::record::EstimateShotBytes(size), free_bytes,
                                          hwlab::platform::MaxPathLength());
}

QString FieldText(hwlab::flow::SettingField field) {
  using F = hwlab::flow::SettingField;
  switch (field) {
    case F::kExposureAuto: return QStringLiteral("노출 자동");
    case F::kExposureStep: return QStringLiteral("노출 단계");
    case F::kGain: return QStringLiteral("게인");
    case F::kWhiteBalanceAuto: return QStringLiteral("색온도 자동");
    case F::kWhiteBalanceK: return QStringLiteral("색온도");
    case F::kSdkFrameRate: return QStringLiteral("SDK fps");
    case F::kLocked: return QStringLiteral("잠금");
  }
  return {};
}

QString ReadbackText(const hwlab::CameraState& s, hwlab::flow::SettingField field) {
  using F = hwlab::flow::SettingField;
  switch (field) {
    case F::kExposureAuto: return Text(s.exposure_auto);
    case F::kExposureStep: return Text(s.exposure_step);
    case F::kGain: return Text(s.gain);
    case F::kWhiteBalanceAuto: return Text(s.white_balance_auto);
    case F::kWhiteBalanceK: return Text(s.white_balance_k);
    case F::kSdkFrameRate: return Text(s.sdk_frame_rate);
    case F::kLocked: return Text(s.locked);
  }
  return {};
}

// 복원 결과를 줄 단위 문장으로 바꿉니다. 첫 값은 화면에 알릴 줄(실패, 불일치), 둘째 값은 로그에만 남길 줄입니다.
std::pair<QStringList, QStringList> RestoreLines(const hwlab::platform::ReconcileResult& result) {
  QStringList shown;
  QStringList logged;
  if (!result.failed.empty()) {
    QStringList items;
    using K = hwlab::flow::CommandKind;
    for (const auto& c : result.failed) {
      // 값을 쓰는 명령만 값을 함께 적습니다. 잠금, 자동 전환 명령의 value는 쓰지 않는 0입니다.
      const bool has_value = c.kind == K::kSdkFrameRate || c.kind == K::kExposureManual || c.kind == K::kGain ||
                             c.kind == K::kWhiteBalanceManual;
      const QString name = QLatin1String(hwlab::flow::NameOf(c.kind));
      items << (has_value ? QStringLiteral("%1(%2)").arg(name).arg(c.value) : name);
    }
    shown << QStringLiteral("실패한 명령: ") + items.join(QStringLiteral(", "));
  }
  if (!result.mismatches.empty()) {
    QStringList items;
    for (const auto f : result.mismatches) {
      items << QStringLiteral("%1(지금 %2)").arg(FieldText(f), ReadbackText(result.readback, f));
    }
    shown << QStringLiteral("실행 전과 다른 항목: ") + items.join(QStringLiteral(", "));
  }
  if (!result.unknown_saved.empty()) {
    QStringList items;
    for (const auto f : result.unknown_saved) items << FieldText(f);
    logged << QStringLiteral("실행 전 값을 몰라 되돌리지 않은 항목: ") + items.join(QStringLiteral(", "));
  }
  return {shown, logged};
}

// 복원 결과를 경고 출력과 앱 데이터 폴더의 로그 파일에 남깁니다. 남길 줄이 없으면 아무것도 하지 않습니다.
void LogRestore(const QStringList& lines) {
  if (lines.isEmpty()) return;
  const QString stamp = QDateTime::currentDateTime().toString(Qt::ISODate);
  for (const QString& line : lines) qWarning("카메라 복원: %s", qUtf8Printable(line));
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (dir.isEmpty() || !QDir().mkpath(dir)) return;
  QFile file(QDir(dir).filePath(QStringLiteral("camera_restore_log.txt")));
  if (!file.open(QIODevice::Append | QIODevice::Text)) return;
  for (const QString& line : lines) file.write((stamp + QStringLiteral(" ") + line + QStringLiteral("\n")).toUtf8());
}

}  // namespace

CaptureController::CaptureController(std::unique_ptr<hwlab::platform::RgbCamera> camera,
                                     hwlab::flow::CaptureSpec spec, QString out_dir, RememberedValues* remembered_values,
                                     std::unique_ptr<hwlab::platform::ThermalCamera> thermal,
                                     QString thermal_absent_reason, hwlab::record::ThermalAbsence thermal_absence,
                                     hwlab::platform::SavedVideos videos, hwlab::record::LightingInfo lighting)
    : camera_(std::move(camera)),
      thermal_camera_(std::move(thermal)),
      spec_(std::move(spec)),
      settings_(spec_.settings),
      shot_(std::move(out_dir), remembered_values),
      thermal_view_(thermal_camera_ != nullptr, std::move(thermal_absent_reason), spec_.thermal_record_period_us),
      remembered_values_(remembered_values),
      thermal_absence_(thermal_absence),
      saved_videos_(videos),
      lighting_(std::move(lighting)),
      rgb_regions_(QStringLiteral("regions/rgb"), kRgbFormat, kFootMaxWidth, remembered_values),
      // 열화상은 RGB와 맞춘 보정값이 없어 RGB 영역을 옮겨 쓰지 않고 따로 정합니다. 기본값은 가운데 세로선 분할입니다.
      thermal_regions_(QStringLiteral("regions/thermal"), hwlab::PixelFormat::kY16, 0, remembered_values) {
  background_.setMaxThreadCount(1);
  connect(&poll_timer_, &QTimer::timeout, this, &CaptureController::Poll);
  connect(&shot_, &ShotForm::changed, this, &CaptureController::canShootChanged);
  // 이름을 칠 때마다 부르지 않도록 저장 위치가 바뀐 때만 남은 공간을 다시 읽습니다.
  connect(&shot_, &ShotForm::changed, this, [this] {
    if (shot_.root() != space_root_) RefreshFreeSpace();
  });
  connect(&space_timer_, &QTimer::timeout, this, [this] {
    if (canEdit() && !pending_ && !finishing_) RefreshFreeSpace();
  });
  connect(this, &CaptureController::phaseChanged, this, &CaptureController::canShootChanged);
  connect(this, &CaptureController::measuredFpsChanged, this, &CaptureController::canShootChanged);
  connect(&rgb_regions_, &FootRegions::changed, this, &CaptureController::canShootChanged);
  connect(&rgb_regions_, &FootRegions::changed, this, [this] {
    rgb_trace_.SetBoxes(rgb_regions_.Regions(), rgb_regions_.frameWidth(), rgb_regions_.frameHeight());
  });
  connect(&thermal_regions_, &FootRegions::changed, this, &CaptureController::canShootChanged);
  connect(&thermal_regions_, &FootRegions::changed, this, &CaptureController::UpdateWarmthRegions);
  connect(&thermal_view_, &ThermalView::previewChanged, this, [this] {
    thermal_regions_.SetFrameSize(thermal_view_.FrameWidth(), thermal_view_.FrameHeight());
    UpdateWarmthRegions();
  });
  connect(&thermal_view_, &ThermalView::footContrast, this, [this](int left, int right) {
    last_contrast_ = {left, right};
    foot_warmth_.Push(last_contrast_);
    emit footWarmthChanged();
  });
  // 열화상이 끊기면 앞 판정을 버립니다. 다시 잡히면 새로 쌓습니다.
  connect(this, &CaptureController::canShootChanged, this, [this] {
    if (thermal_connection_ == hwlab::platform::ThermalConnection::kReceiving || foot_warmth_.Lowest(0) == 0) return;
    foot_warmth_.Reset();
    emit footWarmthChanged();
  });
  const QString foot = remembered_values_->Value(QLatin1String(kFootKey)).toString();
  if (foot == QStringLiteral("left")) pixel_left_foot_ = FootSide::kLeft;
  if (foot == QStringLiteral("right")) pixel_left_foot_ = FootSide::kRight;
  remember_settings_ = remembered_values_->Value(QLatin1String(kRememberSettingsKey)).toBool();
  if (remember_settings_) {
    if (const auto remembered = ReadRememberedSettings(*remembered_values_, spec_.settings)) {
      settings_ = *remembered;
      // Capture는 Start에서 spec_으로 만들므로 시작 설정도 같이 바꿉니다.
      spec_.settings = settings_;
      started_with_remembered_ = true;
    }
  }
  UpdateFootNames();
  rgb_regions_.SetFrameSize(spec_.width, spec_.height);
}

bool CaptureController::canShoot() const {
  if (!canEdit() || pending_ || finishing_ || !shot_.ready() || lens_shading_busy_) return false;
  if (space_.verdict == hwlab::record::FreeSpace::kShort) return false;
  // RGB 프레임이 오지 않으면(카메라를 찾는 중, 끊김) 찍지 않습니다.
  if (measured_fps_ <= 0) return false;
  if (saved_videos_ != hwlab::platform::SavedVideos::kFull && !rgb_regions_.ready()) return false;
  const ThermalPlan plan = PlanThermal(thermal_camera_ != nullptr, thermal_absence_,
                                       thermal_camera_ ? thermal_camera_->DeviceName() : std::string(), thermal_connection_);
  return !plan.record || thermal_regions_.ready();
}

QString CaptureController::leftBoxFoot() const { return FootText(pixel_left_foot_); }

QString CaptureController::rightBoxFoot() const { return FootText(Opposite(pixel_left_foot_)); }

void CaptureController::SetPixelLeftFoot(FootSide foot) {
  if (foot == pixel_left_foot_) return;
  pixel_left_foot_ = foot;
  UpdateFootNames();
  emit footChanged();
}

void CaptureController::toggleFoot() {
  if (!canEdit() || pending_) return;
  SetPixelLeftFoot(Opposite(pixel_left_foot_));
  // 화면에서 바꾼 값만 남깁니다. 명령줄 시험 옵션으로 준 값이 다음 실행에 남지 않게 하기 위함입니다.
  remembered_values_->SetValue(QLatin1String(kFootKey), pixel_left_foot_ == FootSide::kLeft ? "left" : "right");
}

void CaptureController::UpdateFootNames() {
  rgb_regions_.SetSideNames(leftBoxFoot() + QStringLiteral(" 상자"), rightBoxFoot() + QStringLiteral(" 상자"));
  thermal_regions_.SetSideNames(QStringLiteral("열화상 ") + leftBoxFoot() + QStringLiteral(" 상자"),
                                QStringLiteral("열화상 ") + rightBoxFoot() + QStringLiteral(" 상자"));
}

QString CaptureController::savedVideosText() const {
  switch (saved_videos_) {
    case hwlab::platform::SavedVideos::kFull: return QStringLiteral("저장 영상: 전체 화면");
    case hwlab::platform::SavedVideos::kFeet: return QStringLiteral("저장 영상: 왼발, 오른발");
    case hwlab::platform::SavedVideos::kFullAndFeet: return QStringLiteral("저장 영상: 왼발, 오른발, 전체 화면");
  }
  return {};
}

QString CaptureController::lightingText() const {
  const QString product = lighting_.product.empty() ? QString()
                                                    : QStringLiteral(" (%1)").arg(QString::fromStdString(lighting_.product));
  switch (lighting_.source) {
    case hwlab::record::LightSource::kUnknown: return QStringLiteral("조명: 기록 없음") + product;
    case hwlab::record::LightSource::kExternal:
      return QStringLiteral("조명: 앱 밖에서 켬, 밝기 미확인") + product;
    case hwlab::record::LightSource::kApp:
      return lighting_.brightness_percent ? QStringLiteral("조명: 앱 제어 %1%").arg(*lighting_.brightness_percent) + product
                                          : QStringLiteral("조명: 앱 제어, 밝기 미확인") + product;
  }
  return {};
}

QString CaptureController::thermalPlanText() const {
  const ThermalPlan plan = PlanThermal(thermal_camera_ != nullptr, thermal_absence_,
                                       thermal_camera_ ? thermal_camera_->DeviceName() : std::string(), thermal_connection_);
  if (plan.record) return QString();
  return AbsenceText(plan.absence) + (plan.confirm ? QStringLiteral(": 촬영하면 열화상 없이 찍을지 묻습니다")
                                                   : QStringLiteral(": RGB만 녹화합니다"));
}

void CaptureController::SetThermalPrompt(const QString& kind) {
  if (kind == thermal_prompt_) return;
  thermal_prompt_ = kind;
  emit thermalPromptChanged();
}

void CaptureController::UpdateWarmthRegions() {
  const auto rects = thermal_regions_.ReadyPair();
  if (rects == warmth_rects_) return;
  warmth_rects_ = rects;
  thermal_view_.SetWarmthRegions(rects);
  foot_warmth_.Reset();
  emit footWarmthChanged();
}

QString CaptureController::footWarmthState() const {
  const ThermalPlan plan = PlanThermal(thermal_camera_ != nullptr, thermal_absence_,
                                       thermal_camera_ ? thermal_camera_->DeviceName() : std::string(), thermal_connection_);
  if (!plan.record || !warmth_rects_) return {};
  using hwlab::pixel::Warmth;
  const Warmth a = foot_warmth_.State(0), b = foot_warmth_.State(1);
  if (a == Warmth::kCold || b == Warmth::kCold) return QStringLiteral("cold");
  if (a == Warmth::kChecking || b == Warmth::kChecking) return QStringLiteral("checking");
  return QStringLiteral("warm");
}

QString CaptureController::footWarmthText() const {
  const QString state = footWarmthState();
  if (state == QStringLiteral("checking")) return QStringLiteral("발이 따뜻한지 열화상으로 확인하는 중입니다");
  if (state != QStringLiteral("cold")) return {};
  // 화소 왼쪽 상자(0)가 pixel_left_foot_입니다. 문장은 환자의 왼발, 오른발로 씁니다.
  using hwlab::pixel::Warmth;
  const bool cold0 = foot_warmth_.State(0) == Warmth::kCold, cold1 = foot_warmth_.State(1) == Warmth::kCold;
  const QString min = QString::number(hwlab::pixel::kWarmContrastMin);
  if (cold0 && cold1) {
    const bool left_first = pixel_left_foot_ == FootSide::kLeft;
    return QStringLiteral("두 발이 차갑습니다 (발과 바닥의 열화상 차이 왼발 %1, 오른발 %2, 기준 %3)")
        .arg(foot_warmth_.Lowest(left_first ? 0 : 1))
        .arg(foot_warmth_.Lowest(left_first ? 1 : 0))
        .arg(min);
  }
  const int side = cold0 ? 0 : 1;
  const QString foot = side == 0 ? leftBoxFoot() : rightBoxFoot();
  return QStringLiteral("%1이 차갑습니다 (발과 바닥의 열화상 차이 %2, 기준 %3)").arg(foot).arg(foot_warmth_.Lowest(side)).arg(min);
}

QString CaptureController::cameraSettingWarning() const {
  const bool fixed = settings_.mode == hwlab::SettingMode::kManual && settings_.gain == 1 &&
                     (settings_.exposure_step == -5 || settings_.exposure_step == -9);
  if (fixed) return {};
  const QString now = settings_.mode == hwlab::SettingMode::kAuto
                          ? QStringLiteral("자동")
                          : QStringLiteral("수동, 노출 %1, 게인 %2").arg(settings_.exposure_step).arg(settings_.gain);
  return QStringLiteral("카메라 설정(%1)이 커프 단계 기준(수동, 게인 1, 노출 -5 또는 -9)과 다릅니다").arg(now);
}

void CaptureController::SetColdFootPrompt(const QString& title) {
  if (title == cold_foot_prompt_) return;
  cold_foot_prompt_ = title;
  emit coldFootPromptChanged();
}

void CaptureController::startWithColdFoot() {
  if (cold_foot_prompt_.isEmpty()) return;
  SetColdFootPrompt({});
  Begin(false, true);
}

void CaptureController::cancelColdFootPrompt() { SetColdFootPrompt({}); }

int32_t CaptureController::EarlierShotsToday(const hwlab::record::ShotInfo& info) const {
  // 촬영 폴더는 <저장 위치>/<이름_생년월일>/<발목 혈압 단계>/<날짜_시각>입니다(record::LayoutOf). 같은 날짜로 시작하고
  // 매니페스트가 있는 폴더만 셉니다. 녹화 전에 지운 폴더(DiscardUnrecordedShot)는 남지 않습니다.
  const QDir level(QDir(QString::fromStdString(shot_.RootUtf8()))
                       .filePath(QString::fromStdString(info.patient_name + "_" + info.birth_date + "/" + info.ankle_level)));
  const QString day = QStringLiteral("%1%2%3_").arg(info.year, 4, 10, QLatin1Char('0')).arg(info.month, 2, 10, QLatin1Char('0'))
                          .arg(info.day, 2, 10, QLatin1Char('0'));
  int32_t count = 0;
  for (const QString& name : level.entryList({day + QStringLiteral("*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (!QDir(level.filePath(name)).entryList({QStringLiteral("*_shot.json")}, QDir::Files).isEmpty()) ++count;
  }
  return count;
}

void CaptureController::startWithoutThermal() {
  if (thermal_prompt_.isEmpty()) return;
  SetThermalPrompt({});
  Begin(true);
}

void CaptureController::cancelThermalPrompt() { SetThermalPrompt({}); }

void CaptureController::SetLensShadingPrompt(const QString& kind) {
  if (kind == lens_shading_prompt_) return;
  lens_shading_prompt_ = kind;
  emit lensShadingPromptChanged();
}

void CaptureController::askLensShading() {
  if (!canEdit() || pending_) return;
  SetLensShadingPrompt(QStringLiteral("confirm"));
}

void CaptureController::correctLensShading() {
  SetLensShadingPrompt({});
  if (capture_ == nullptr || !canEdit() || pending_) return;
  if (!capture_->CorrectThermalLensShading()) {
    SetAppError(QStringLiteral("열화상을 받는 미리보기 중에만 렌즈 보정을 할 수 있습니다"));
  }
}

void CaptureController::cancelLensShadingPrompt() { SetLensShadingPrompt({}); }

void CaptureController::SetAnalysis(const QString& text, const QString& state) {
  if (text == analysis_text_ && state == analysis_state_) return;
  analysis_text_ = text;
  analysis_state_ = state;
  emit analysisChanged();
}

CaptureController::~CaptureController() {
  poll_timer_.stop();
  background_.waitForDone();
  // shutdown 없이 끝난 경우(화면 저장 시험)에도 복원 결과를 로그에 남깁니다. 이미 멈췄으면 Stop은 빈 결과입니다.
  if (capture_) {
    const auto [shown, logged] = RestoreLines(capture_->Stop());
    LogRestore(shown + logged);
  }
}

void CaptureController::shutdown() {
  if (shutting_down_ || close_ready_) return;
  shutting_down_ = true;
  LogShutdownStep(QStringLiteral("종료 시작"));
  if (pending_ || finishing_) {
    // 촬영 마무리(PPG 파일, 열화상 배열 저장, 분석 입력 확인)는 Poll에서 이어지므로 끝난 뒤 장치를 닫습니다.
    // 장치를 먼저 닫으면 폴더에 영상만 남았습니다.
    saving_before_close_ = true;
    emit shutdownChanged();
    LogShutdownStep(QStringLiteral("촬영 마무리 기다림"));
    if (pending_) capture_->StopRecording();
    // 카메라가 프레임을 보내지 않으면 녹화가 끝나지 않으므로 정한 시간 뒤에는 마무리 없이 장치를 닫습니다.
    QTimer::singleShot(kSaveBeforeCloseLimitMs, this, [this] {
      if (!saving_before_close_) return;
      LogShutdownStep(QStringLiteral("촬영 마무리가 끝나지 않아 장치를 닫음"));
      saving_before_close_ = false;
      StopDevices();
    });
    return;
  }
  emit shutdownChanged();
  StopDevices();
}

void CaptureController::StopDevices() {
  poll_timer_.stop();
  emit shutdownChanged();
  // 장치 드라이버 호출이 돌아오지 않으면 Stop이 끝나지 않아 창이 닫히지 않고 장치를 잡은 채 남습니다(2026-10-27).
  // 단계를 남기고 제한 시간이 지나면 프로세스를 끝냅니다(shutdown_watch.h).
  // Stop은 녹화 파일을 닫고 카메라 명령을 실행해 수 초 걸릴 수 있으므로 화면 스레드에서 하지 않습니다.
  background_.start(QRunnable::create([this] {
    // 같은 작업 줄의 앞 작업(열화상 배열 저장)을 기다린 시간은 넣지 않도록 Stop을 시작할 때 제한 시간을 겁니다.
    ArmShutdownLimit(kShutdownLimitMs, QStringLiteral("카메라 정리 %1초").arg(kShutdownLimitMs / 1000));
    const hwlab::platform::ReconcileResult result =
        capture_ ? capture_->Stop([](const char* step) { LogShutdownStep(QString::fromUtf8(step)); })
                 : hwlab::platform::ReconcileResult{};
    LogShutdownStep(QStringLiteral("카메라 정리 끝"));
    QMetaObject::invokeMethod(this, [this, result] {
      // 복원 불일치 안내는 사람이 닫을 때까지 떠 있으므로 제한 시간을 풉니다. 창을 닫은 뒤의 정리는 main이 다시 겁니다.
      DisarmShutdownLimit();
      const auto [shown, logged] = RestoreLines(result);
      LogRestore(shown + logged);
      restore_report_ = shown.isEmpty() ? QString() : (shown + logged).join(QStringLiteral("\n"));
      shutting_down_ = false;
      close_ready_ = true;
      emit shutdownChanged();
      emit shutdownFinished();
    }, Qt::QueuedConnection);
  }));
}

bool CaptureController::Start(QString* error) {
  capture_ = std::make_unique<hwlab::platform::Capture>(*camera_, spec_, thermal_camera_.get(),
                                                       light_ != nullptr ? light_->Device() : nullptr);
  capture_->SetThermalPreviewSink(kThermalPreviewEveryFrames, thermal_view_.Sink());
  rgb_trace_.SetBoxes(rgb_regions_.Regions(), rgb_regions_.frameWidth(), rgb_regions_.frameHeight());
  capture_->SetPreviewSink(kPreviewEveryFrames, [this](const hwlab::FrameView& view) {
    // 수신 스레드에서 불립니다. 줄인 RGB로 바꿔 두기만 하고 그리기는 화면 스레드가 합니다.
    rgb_trace_.Measure(view);
    const int w = view.width / kPreviewStep;
    const int h = view.height / kPreviewStep;
    QImage image(w, h, QImage::Format_RGB888);
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    if (!hwlab::pixel::UyvyToRgbPreview(view, kPreviewStep, rgb)) return;
    for (int y = 0; y < h; ++y) std::memcpy(image.scanLine(y), rgb.data() + static_cast<size_t>(y) * w * 3, w * 3);
    {
      std::lock_guard<std::mutex> lock(preview_mutex_);
      preview_ = std::move(image);
    }
    QMetaObject::invokeMethod(this, [this] {
      ++preview_revision_;
      emit previewChanged();
    }, Qt::QueuedConnection);
  });
  std::string message;
  if (!capture_->Start(&message)) {
    *error = QString::fromStdString(message);
    return false;
  }
  if (light_ != nullptr) light_->Bind(capture_.get());
  last_poll_ms_ = QDateTime::currentMSecsSinceEpoch();
  poll_timer_.start(kPollMs);
  space_timer_.start(kFreeSpaceCheckMs);
  RefreshFreeSpace();
  Poll();
  return true;
}

QString CaptureController::freeSpaceLevel() const {
  switch (space_.verdict) {
    case hwlab::record::FreeSpace::kLow: return QStringLiteral("low");
    case hwlab::record::FreeSpace::kShort: return QStringLiteral("short");
    case hwlab::record::FreeSpace::kEnough:
    case hwlab::record::FreeSpace::kUnknown: break;
  }
  return {};
}

std::optional<int64_t> CaptureController::FreeBytesAt(const std::string& folder) const {
  return free_bytes_override_ ? free_bytes_override_ : hwlab::platform::FreeBytesAt(folder);
}

void CaptureController::SetFreeBytesOverride(int64_t bytes) {
  free_bytes_override_ = bytes;
  RefreshFreeSpace();
}

void CaptureController::RefreshFreeSpace() {
  const std::string root = shot_.RootUtf8();
  const ThermalPlan plan = PlanThermal(thermal_camera_ != nullptr, thermal_absence_,
                                       thermal_camera_ ? thermal_camera_->DeviceName() : std::string(), thermal_connection_);
  const hwlab::record::ShotSizePlan size =
      SizePlanOf(spec_, saved_videos_, rgb_regions_.Regions(), {thermal_view_.FrameWidth(), thermal_view_.FrameHeight()},
                 plan.record);
  const std::optional<int64_t> free_bytes = FreeBytesAt(root);
  hwlab::platform::FreeSpaceNotice next = hwlab::platform::DescribeFreeSpace(free_bytes, hwlab::record::EstimateShotBytes(size));
  space_root_ = shot_.root();
  if (next.verdict == space_.verdict && next.text == space_.text) return;
  space_ = std::move(next);
  emit freeSpaceChanged();
  emit canShootChanged();
}

void CaptureController::SetSpecSource(const QString& file, const QString& sha256) {
  spec_file_ = file.toStdString();
  spec_sha256_ = sha256.toStdString();
}

QImage CaptureController::LatestPreview() {
  std::lock_guard<std::mutex> lock(preview_mutex_);
  return preview_;
}

std::vector<ReadbackModel::Row> CaptureController::ReadbackRows(const hwlab::platform::CaptureStatus& status) const {
  // 목표와 다른 항목을 화면이 빨간색으로 표시합니다.
  const auto mismatched = [&](hwlab::flow::SettingField field) {
    for (const auto f : status.mismatches) {
      if (f == field) return true;
    }
    return false;
  };
  using F = hwlab::flow::SettingField;
  const hwlab::CameraState& s = status.readback;
  // 목표는 녹화 중이거나 안정화 중이면 녹화 목표, 아니면 미리보기 목표입니다.
  const hwlab::CameraState t = status.phase == hwlab::flow::Phase::kPreview
                                   ? hwlab::flow::PreviewTarget(settings_)
                                   : hwlab::flow::RecordingTarget(settings_);
  std::vector<ReadbackModel::Row> rows;
  const auto row = [&](const QString& name, const QString& value, bool bad, const QString& target) {
    rows.push_back({name, value, bad, target});
  };
  // 단계와 함께 명목 노출 시간(2ⁿ초)을 보입니다. 예: "-5 (31.25 ms)"
  const auto exposure = [](const std::optional<bool>& is_auto, const std::optional<int32_t>& step) {
    if (is_auto == true) return QStringLiteral("자동");
    if (!step) return Text(step);
    return QStringLiteral("%1 (%2 ms)").arg(*step).arg(hwlab::ExposureStepMs(*step), 0, 'f', 2);
  };
  const auto balance = [](const std::optional<bool>& is_auto, const std::optional<int32_t>& k) {
    return is_auto == true ? QStringLiteral("자동") : Text(k) + " K";
  };
  row(QStringLiteral("노출"), exposure(s.exposure_auto, s.exposure_step),
      mismatched(F::kExposureAuto) || mismatched(F::kExposureStep), exposure(t.exposure_auto, t.exposure_step));
  row(QStringLiteral("게인"), Text(s.gain), mismatched(F::kGain), Text(t.gain));
  row(QStringLiteral("색온도"), balance(s.white_balance_auto, s.white_balance_k),
      mismatched(F::kWhiteBalanceAuto) || mismatched(F::kWhiteBalanceK), balance(t.white_balance_auto, t.white_balance_k));
  row(QStringLiteral("SDK fps"), Text(s.sdk_frame_rate), mismatched(F::kSdkFrameRate), Text(t.sdk_frame_rate));
  row(QStringLiteral("잠금"), Text(s.locked), mismatched(F::kLocked), Text(t.locked));
  return rows;
}

QString CaptureController::phase() const { return PhaseName(phase_); }

void CaptureController::Poll() {
  const hwlab::platform::CaptureStatus status = capture_->Status();
  Assign(phase_, status.phase, &CaptureController::phaseChanged);
  Assign(settle_remaining_, static_cast<int>(status.frames_until_settled), &CaptureController::settleRemainingChanged);
  Assign(counts_, Counts{status.written, status.missing, status.dropped}, &CaptureController::countsChanged);
  // 쓰기 실패는 녹화가 끝까지 이어지므로 실패를 안 때부터 남은 공간을 다시 읽어 원인과 함께 보입니다.
  if (pending_ && (status.write_failed || (pending_->thermal && status.thermal.write_failed))) {
    ReportSaveFailure(pending_->prepared.folder,
                      QString::fromStdString(status.write_failed ? status.error : status.thermal.error));
  }
  Assign(error_, !save_failure_.isEmpty() ? save_failure_
                 : status.error.empty() ? app_error_ : QString::fromStdString(status.error),
         &CaptureController::errorChanged);
  readback_.Update(ReadbackRows(status));
  thermal_view_.Update(status.thermal);
  if (light_ != nullptr) light_->Update(status);
  Assign(thermal_connection_, status.thermal.connection, &CaptureController::canShootChanged);
  Assign(lens_shading_busy_, status.thermal.lens_shading_busy, &CaptureController::canShootChanged);
  if (!thermal_prompt_.isEmpty()) {
    // 안내가 열려 있는 동안 열화상 상태가 바뀌면 문구를 바꾸고, 수신을 시작하면 안내를 닫습니다(다시 누르면 열화상과 함께 녹화).
    SetThermalPrompt(PromptKind(PlanThermal(thermal_camera_ != nullptr, thermal_absence_,
                                            thermal_camera_ ? thermal_camera_->DeviceName() : std::string(),
                                            thermal_connection_)));
  }
  if (thermal_camera_) thermal_view_.SetDeviceName(QString::fromStdString(thermal_camera_->DeviceName()));
  // 보정 파일이 없거나 불러오지 못했으면 열화상을 처음 받을 때 한 번 보정을 권합니다. 장치를 열기 전에는 보정할 수 없어 수신을 기다립니다.
  const auto& shading = status.thermal.lens_shading;
  if (!lens_shading_prompt_shown_ && canEdit() && !pending_ && thermal_prompt_.isEmpty() &&
      status.thermal.connection == hwlab::platform::ThermalConnection::kReceiving && shading &&
      (shading->record.source == hwlab::record::LensShadingSource::kNone ||
       shading->record.source == hwlab::record::LensShadingSource::kFailed)) {
    lens_shading_prompt_shown_ = true;
    SetLensShadingPrompt(shading->record.source == hwlab::record::LensShadingSource::kNone ? QStringLiteral("missing")
                                                                                           : QStringLiteral("unreadable"));
  }

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now > last_poll_ms_) {
    // 화면에 보이는 fps는 받은 프레임 수로 잽니다. 설정한 fps와 실제가 다를 수 있기 때문입니다(2026-10-16 실측).
    const double fps = 0.7 * measured_fps_ + 0.3 * (status.received - last_received_) * 1000.0 / (now - last_poll_ms_);
    Assign(measured_fps_, std::round(fps * 10.0) / 10.0, &CaptureController::measuredFpsChanged);
  }
  last_received_ = status.received;
  last_poll_ms_ = now;
  Assign(camera_problem_, QString::fromStdString(camera_->OpenProblem()), &CaptureController::cameraProblemChanged);
  const bool just_finished = status.finished && !was_finished_;
  if (just_finished && status.end_reason) {
    Assign(last_result_,
           QStringLiteral("%1: %2장, 카메라 단 누락 %3장, 저장 누락 %4장")
               .arg(EndName(*status.end_reason))
               .arg(status.written)
               .arg(status.missing)
               .arg(status.dropped),
           &CaptureController::lastResultChanged);
    Assign(last_result_warning_, QString::fromStdString(hwlab::platform::DescribeSlowFrames(status.frame_rate)),
           &CaptureController::lastResultChanged);
  }
  was_finished_ = status.finished;
  if (pending_ && !pending_->first_frames_sent && status.phase == hwlab::flow::Phase::kRecording && status.written > 0 &&
      saved_videos_ != hwlab::platform::SavedVideos::kFull) {
    pending_->first_frames_sent = true;
    emit firstFramesWritten(QString::fromStdString(pending_->prepared.left_video),
                            QString::fromStdString(pending_->prepared.right_video));
  }
  if (pending_) {
    if (status.phase != hwlab::flow::Phase::kPreview) pending_->seen_active = true;
    // 안정화 중 중지는 finished를 바꾸지 않으므로, 영상 파일 없이 미리보기로 돌아온 것으로 끝을 압니다.
    const bool stopped_before_recording = pending_->seen_active && status.phase == hwlab::flow::Phase::kPreview &&
                                          !status.finished &&
                                          !AnyVideoExists(pending_->prepared);
    if (just_finished || stopped_before_recording) FinishShot(just_finished);
  }
  // 쓰기 스레드는 대기열을 비우고 파일을 닫은 뒤에 finished를 알리므로, 그 전까지는 미리보기로 돌아와도 저장 중입니다.
  Assign(saving_shot_, pending_.has_value() && pending_->seen_active && status.phase == hwlab::flow::Phase::kPreview,
         &CaptureController::canShootChanged);
  if (saving_before_close_ && !pending_ && !finishing_) {
    saving_before_close_ = false;
    LogShutdownStep(QStringLiteral("촬영 마무리 끝"));
    StopDevices();
  }
}

void CaptureController::SetAppError(const QString& message) {
  app_error_ = message;
  save_failure_.clear();
  Assign(error_, message, &CaptureController::errorChanged);
}

QString CaptureController::ReportSaveFailure(const std::string& folder, const QString& detail) {
  // 시험 옵션의 가짜 값이 아니라 지금 디스크를 읽습니다. 실패의 원인을 실제 남은 공간으로 판단하기 위함입니다.
  const std::optional<int64_t> free_bytes = hwlab::platform::FreeBytesAt(folder);
  save_failure_ = QString::fromStdString(hwlab::platform::DescribeSaveFailure(free_bytes, folder, detail.toStdString()));
  Assign(error_, save_failure_, &CaptureController::errorChanged);
  return save_failure_;
}

void CaptureController::startRecording() { Begin(false); }

void CaptureController::Begin(bool skip_confirmed, bool cold_confirmed) {
  // 연결 상태를 지금 다시 읽어 버튼을 누른 시점으로 판정합니다.
  const hwlab::platform::CaptureStatus now = capture_->Status();
  Assign(thermal_connection_, now.thermal.connection, &CaptureController::canShootChanged);
  Assign(lens_shading_busy_, now.thermal.lens_shading_busy, &CaptureController::canShootChanged);
  RefreshFreeSpace();
  // 렌즈 셰이딩 보정 중에는 렌즈를 가린 채일 수 있어 촬영하지 않습니다.
  if (lens_shading_busy_) {
    SetAppError(QStringLiteral("렌즈 보정 중입니다. 끝난 뒤 다시 누르십시오"));
    return;
  }
  if (!canShoot()) {
    if (canEdit() && !pending_) {
      if (!shot_.ready()) {
        SetAppError(QStringLiteral("환자 정보를 확인하십시오"));
      } else if (space_.verdict == hwlab::record::FreeSpace::kShort) {
        SetAppError(freeSpaceText());
      } else {
        SetAppError(QStringLiteral("발 영역을 확인하십시오"));
      }
    }
    return;
  }
  // 보내는 중인 LED 밝기는 매니페스트와 사이드카에 남길 값이 정해지지 않았으므로 기다리게 합니다.
  if (light_ != nullptr && light_->busy()) {
    SetAppError(QStringLiteral("조명 밝기를 보내는 중입니다. 잠시 뒤 다시 누르십시오"));
    return;
  }
  // 시각을 한 번만 읽습니다. 두 번 읽으면 자정 앞뒤에서 폴더 날짜와 파일 시각이 갈립니다.
  hwlab::record::ShotInfo info = shot_.InfoAt(QDateTime::currentDateTime());
  const std::string thermal_name = thermal_camera_ ? thermal_camera_->DeviceName() : std::string();
  const ThermalPlan plan = PlanThermal(thermal_camera_ != nullptr, thermal_absence_, thermal_name, thermal_connection_);
  if (plan.confirm && !skip_confirmed) {
    SetThermalPrompt(PromptKind(plan));
    return;
  }
  // 발이 차가우면 맥박 신호가 약해 재측정이 될 수 있어 진행할지 묻습니다. 판정 기준이 잠정값이라 막지는 않습니다.
  const QString warmth = plan.record ? footWarmthState() : QString();
  if (warmth == QStringLiteral("cold") && !cold_confirmed) {
    const QString text = footWarmthText();
    SetColdFootPrompt(text.left(text.indexOf(QStringLiteral(" ("))) + QStringLiteral(". 이대로 진행할까요?"));
    return;
  }
  if (plan.record && (warmth == QStringLiteral("cold") || warmth == QStringLiteral("warm"))) {
    // 매니페스트는 환자 기준 좌우입니다. 화소 왼쪽 상자(0)가 pixel_left_foot_입니다.
    const bool left_first = pixel_left_foot_ == FootSide::kLeft;
    info.foot_warmth = hwlab::record::FootWarmthRecord{foot_warmth_.Lowest(left_first ? 0 : 1),
                                                       foot_warmth_.Lowest(left_first ? 1 : 0),
                                                       hwlab::pixel::kWarmContrastMin, cold_confirmed};
  }
  info.attempt = EarlierShotsToday(info) + 1;
  info.pixel_left_foot = pixel_left_foot_;
  info.lighting = light_ != nullptr ? light_->Lighting() : lighting_;
  info.app_version = kAppVersion;
  info.app_commit = kAppCommit;
  info.spec_file = spec_file_;
  info.spec_sha256 = spec_sha256_;
  info.thermal_absence = plan.absence;
  info.thermal_skip_confirmed = !plan.record && plan.confirm && skip_confirmed;
  std::vector<hwlab::record::ShotDevice> devices = {{hwlab::StreamKind::kRgb, camera_->DeviceName()}};
  // 수신 중일 때만 열화상을 넣습니다. 찾는 중에 넣으면 열화상 파일 없이 매니페스트에만 장치가 남을 수 있습니다.
  if (plan.record) devices.push_back({hwlab::StreamKind::kThermal, thermal_name});
  const std::string limit = ShotLimitProblem(shot_.RootUtf8(), info, spec_, saved_videos_, rgb_regions_.Regions(),
                                             {thermal_view_.FrameWidth(), thermal_view_.FrameHeight()}, devices,
                                             free_bytes_override_);
  if (!limit.empty()) {
    SetAppError(QString::fromStdString(limit));
    return;
  }
  std::string why;
  const auto prepared = hwlab::platform::PrepareShot(shot_.RootUtf8(), info, spec_.codec,
                                                     saved_videos_, devices, kMaxShotNumber, &why);
  if (!prepared) {
    SetAppError(QString::fromStdString(why));
    return;
  }
  SetAppError({});
  save_failure_.clear();
  SetAnalysis({}, {});
  pending_ = PendingShot{*prepared, plan.record, false,
                         plan.record ? thermal_regions_.ReadyPair() : std::nullopt, plan.absence};
  emit canShootChanged();
  hwlab::platform::RecordingPaths paths;
  if (light_ != nullptr) {
    paths.light = light_->SidecarLight();
  } else {
    paths.light.source = lighting_.source;
    paths.light.percent = lighting_.brightness_percent;
  }
  paths.video = prepared->video;
  paths.sidecar = prepared->sidecar;
  if (saved_videos_ != hwlab::platform::SavedVideos::kFull) {
    // 화소 왼쪽 상자가 환자의 어느 발인지(pixel_left_foot)로 파일 이름의 left, right를 정합니다.
    const hwlab::pixel::SideRegions regions = rgb_regions_.Regions();
    const auto output_of = [&](const hwlab::pixel::Rect& rect, FootSide foot) {
      return hwlab::platform::CropOutput{rect, foot == FootSide::kLeft ? prepared->left_video : prepared->right_video,
                                         foot == FootSide::kLeft ? prepared->left_sidecar : prepared->right_sidecar,
                                         foot};
    };
    paths.crops.push_back(output_of(regions.left, pixel_left_foot_));
    paths.crops.push_back(output_of(regions.right, Opposite(pixel_left_foot_)));
  }
  if (plan.record) {
    paths.thermal_raw = prepared->thermal_raw;
    paths.thermal_sidecar = prepared->thermal_sidecar;
    paths.thermal_preview = prepared->thermal_preview;
  }
  // 촬영 버튼에서 PPG 장치를 다시 시작하므로, 연 PPG가 안정될 때까지 녹화를 미룹니다. 열지 않았으면 기다리지 않습니다.
  if (ppg_ != nullptr && ppg_->connected()) paths.ppg_settle_us = hwlab::platform::kPpgSettleUs;
  if (!capture_->StartRecording(paths)) {
    // 받지 않은 녹화는 끝났다는 알림이 오지 않으므로 여기서 촬영을 거둡니다. 거두지 않으면 촬영 버튼이 계속 막힙니다.
    pending_.reset();
    emit canShootChanged();
    const QString why = QString::fromStdString(capture_->Status().error);
    SetAppError(why.isEmpty() ? QStringLiteral("앞 촬영을 저장하는 중이라 녹화하지 않았습니다. 잠시 뒤 다시 누르십시오") : why);
    return;
  }
  // 안정화 동안부터 기록합니다. 결과 창은 RGB 사이드카의 공통 시각으로 녹화 구간만 잘라 씁니다.
  if (ppg_ != nullptr) ppg_->BeginShot();
}

void CaptureController::AppendResult(const QString& line) {
  // 같은 저장 실패 문장을 영상, 열화상, PPG가 각각 알려도 한 번만 보입니다.
  if (last_result_.endsWith(line)) return;
  Assign(last_result_, last_result_.isEmpty() ? line : last_result_ + QStringLiteral("\n") + line,
         &CaptureController::lastResultChanged);
}

void CaptureController::FinishShot(bool finished) {
  const PendingShot shot = *pending_;
  pending_.reset();
  emit canShootChanged();
  // 녹화가 끝난 시점의 상태로 한 번 더 확인합니다. 닫을 때 남은 버퍼를 쓰다 실패한 경우는 이때 알려집니다.
  const hwlab::platform::CaptureStatus status = capture_->Status();
  if (status.write_failed || (shot.thermal && status.thermal.write_failed)) {
    ReportSaveFailure(shot.prepared.folder, QString::fromStdString(status.write_failed ? status.error : status.thermal.error));
  }
  const bool save_failed = !save_failure_.isEmpty();
  RefreshFreeSpace();
  if (AnyVideoExists(shot.prepared)) {
    AppendResult(QString::fromStdString(shot.prepared.layout.folder));
    if (save_failed) AppendResult(save_failure_);
    PendingShot saving = shot;
    if (ppg_ != nullptr) {
      saving.save_ppg = ppg_->EndShot(shot.prepared.folder, shot.prepared.layout.stem,
                                      {shot.prepared.left_sidecar, shot.prepared.right_sidecar, shot.prepared.sidecar});
    }
    if (!shot.thermal) {
      AppendResult(QStringLiteral("열화상 없음(%1): RGB만 녹화했습니다").arg(AbsenceText(shot.thermal_absence)));
    }
    FinishInBackground(saving, save_failed);
    return;
  }
  if (ppg_ != nullptr) ppg_->CancelShot();
  if (save_failed) {
    // 영상 파일을 쓰지 못한 촬영은 녹화 전에 중지한 것과 다릅니다. 만든 폴더와 파일은 지우지 않고 실패를 알립니다.
    Assign(last_result_, save_failure_, &CaptureController::lastResultChanged);
    return;
  }
  // 영상이 없는 폴더를 남기면 촬영한 것처럼 보이므로 매니페스트와 빈 폴더를 지웁니다.
  hwlab::platform::DiscardUnrecordedShot(shot.prepared);
  if (!finished) {
    Assign(last_result_, QStringLiteral("녹화 전에 중지해 저장하지 않았습니다"), &CaptureController::lastResultChanged);
  }
}

void CaptureController::FinishInBackground(const PendingShot& shot, bool save_failed) {
  // 원시값 파일 전체를 읽어 두 파일로 쓰고 사이드카를 읽으므로(640x480, 270장이면 약 166MB) 화면 스레드에서 하지 않습니다.
  finishing_ = true;
  emit canShootChanged();
  SetAnalysis(shot.thermal ? QStringLiteral("열화상 좌우 배열 저장 중") : QStringLiteral("분석 입력 확인 중"), {});
  background_.start(QRunnable::create([this, shot, save_failed] {
    // 관류 분석 프로그램(v9)이 분석을 시작할 때 촬영 폴더의 PPG 파일을 맥박 기준 후보로 읽어, 분석을 요청하기 전에 씁니다.
    const PpgSaveResult ppg = shot.save_ppg ? shot.save_ppg() : PpgSaveResult{};
    // 분석을 막는 입력 문제는 좌우 영상과 사이드카에서만 나옵니다(CheckAnalysisInputs). 열화상 배열은 분석을 시작한 뒤에 씁니다.
    // 실촬영에서 열화상 배열 저장이 분석 요청을 약 3.6초 늦췄습니다(2026-10-26). 여기서 열화상 배열이 아직 없다는 경고는 버립니다.
    const bool rgb_ready = !hwlab::record::HasBlockingFinding(hwlab::record::CheckAnalysisInputs(
        hwlab::platform::ReadAnalysisFiles(shot.prepared), hwlab::record::kMinThermalFramesInRgbWindow));
    QMetaObject::invokeMethod(this, [this, rgb_ready = rgb_ready && !save_failed,
                                     folder = QString::fromStdString(shot.prepared.folder)] {
      emit rgbSaved(folder, rgb_ready);
    }, Qt::QueuedConnection);
    std::string why;
    const bool ok = !shot.thermal || hwlab::platform::WriteThermalArrays(shot.prepared, shot.thermal_regions, &why);
    const auto findings = hwlab::record::CheckAnalysisInputs(hwlab::platform::ReadAnalysisFiles(shot.prepared),
                                                             hwlab::record::kMinThermalFramesInRgbWindow);
    QMetaObject::invokeMethod(this, [this, ppg, ok, why, findings, save_failed, thermal = shot.thermal,
                                     folder = QString::fromStdString(shot.prepared.folder)] {
      finishing_ = false;
      emit canShootChanged();
      bool failed = save_failed;
      const std::string folder_utf8 = folder.toStdString();
      // 저장 실패 문장은 촬영마다 한 번만 결과에 남깁니다. 같은 디스크 부족이 영상, PPG, 열화상 배열에서 차례로 알려지기 때문입니다.
      if (ppg.failed) {
        const QString text = ReportSaveFailure(folder_utf8, ppg.line);
        if (!failed) AppendResult(text);
        failed = true;
      } else if (!ppg.line.isEmpty()) {
        AppendResult(ppg.line);
      }
      if (thermal) {
        if (ok) {
          AppendResult(QStringLiteral("열화상 좌우 배열 저장 완료"));
        } else {
          const std::optional<int64_t> free_bytes = hwlab::platform::FreeBytesAt(folder_utf8);
          if (free_bytes && *free_bytes < hwlab::platform::kFreeSpaceReserveBytes) {
            const QString text = ReportSaveFailure(folder_utf8, QStringLiteral("열화상 좌우 배열: ") + QString::fromStdString(why));
            if (!failed) AppendResult(text);
          } else {
            // 영역 오류처럼 공간과 무관한 실패는 이유를 그대로 보입니다.
            const QString done = QStringLiteral("열화상 좌우 배열 저장 실패: ") + QString::fromStdString(why);
            AppendResult(done);
            SetAppError(done);
          }
          failed = true;
        }
      }
      const auto [text, state] = AnalysisVerdict(findings);
      if (failed) {
        SetAnalysis(QStringLiteral("분석하지 않음: 저장에 실패한 촬영입니다"), QStringLiteral("blocked"));
      } else {
        SetAnalysis(text, state);
      }
      shot_.refreshRecent();
      RefreshFreeSpace();
      emit shotSaved(folder, !failed && state != QStringLiteral("blocked"));
    }, Qt::QueuedConnection);
  }));
}

void CaptureController::stopRecording() { capture_->StopRecording(); }

bool CaptureController::applySettings(bool auto_mode, int exposure_step, int gain, int white_balance) {
  hwlab::flow::SettingsSpec next = settings_;
  next.mode = auto_mode ? hwlab::SettingMode::kAuto : hwlab::SettingMode::kManual;
  next.exposure_step = exposure_step;
  next.gain = gain;
  next.white_balance_k = white_balance;
  if (!capture_->UpdateSettings(next)) return false;
  settings_ = next;
  if (remember_settings_) SaveSettings(*remembered_values_, settings_);
  emit settingsChanged();
  return true;
}

void CaptureController::setRememberSettings(bool on) {
  if (remember_settings_ == on) return;
  remember_settings_ = on;
  remembered_values_->SetValue(QLatin1String(kRememberSettingsKey), on);
  // 켤 때 지금 적용된 설정을 바로 남깁니다. 적용을 다시 누르지 않아도 다음 실행이 이 값으로 시작합니다.
  if (on) SaveSettings(*remembered_values_, settings_);
  emit settingsChanged();
}
