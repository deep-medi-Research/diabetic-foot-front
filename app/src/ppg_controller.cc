#include "ppg_controller.h"

#include <QFile>
#include <QMetaObject>
#include <QStringList>
#include <QVariantMap>

#include <chrono>
#include <optional>
#include <utility>
#include <vector>

#include "hwlab/platform/shot_review.h"
#include "remembered_values.h"

namespace {

constexpr char kIdentityKey[] = "ppg/identity";
// 장치 스레드가 멈췄는지 보는 간격입니다. 값이 바뀔 때만 화면에 알립니다.
constexpr int kStallCheckMs = 1000;
// 자동 연결을 다시 시도하는 간격과, 같은 장치가 보이는 동안의 최대 시도 횟수입니다. 실기(2026-10-21)에서 USB를 다시 꽂은 직후의
// 연결은 첫 읽기가 실패했고 시간이 지나 다시 누르자 연결됐습니다. 3초 간격 5번(약 15초)이면 다시 꽂은 뒤 안정될 시간을 넘는다고
// 보고 정했습니다. 그 뒤로도 안 되면 장치 문제로 보고 사람이 포트를 누를 때까지 기다립니다. 꽂은 뒤 걸리는 시간은 재지 않았습니다.
constexpr int kAutoRetryMs = 3000;
constexpr int kMaxAutoAttempts = 5;

int64_t SteadyNowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

QString IdentityOf(const hwlab::platform::SerialPortInfo& p) {
  if (!p.usb_vendor_id || !p.usb_product_id) return {};
  return QStringLiteral("%1:%2:%3")
      .arg(*p.usb_vendor_id, 4, 16, QLatin1Char('0'))
      .arg(*p.usb_product_id, 4, 16, QLatin1Char('0'))
      .arg(QString::fromStdString(p.serial_number))
      .toUpper();
}

// 결과 창이 그리는 구간과 같게 RGB 첫 장부터 마지막 장까지의 공통 시각에서 셉니다.
// RGB 영상 파일마다 사이드카의 프레임 시각이 같으므로 처음 열리는 사이드카 하나만 읽습니다.
std::optional<hwlab::record::PpgErrorWindow> SummarizeInRgbWindow(const hwlab::platform::PpgRecord& record,
                                                                  const std::vector<std::string>& rgb_sidecars,
                                                                  std::string* why) {
  QFile file;
  for (const std::string& path : rgb_sidecars) {
    if (path.empty()) continue;
    file.setFileName(QString::fromStdString(path));
    if (file.open(QIODevice::ReadOnly)) break;
  }
  if (!file.isOpen()) {
    *why = "RGB 사이드카가 없습니다";
    return std::nullopt;
  }
  const auto frames = hwlab::platform::ReadSidecarFrames(file.readAll().toStdString(), why);
  if (!frames) return std::nullopt;
  if (frames->common_time.empty()) {
    *why = "RGB 사이드카에 프레임이 없습니다";
    return std::nullopt;
  }
  return hwlab::record::SummarizePpgErrors(record.common_time, record.measure_error, frames->common_time.front(),
                                           frames->common_time.back());
}

// 녹화 중 연결이 끊겼으면 끊긴 시각을 결과 창 가로축과 같은 RGB 첫 장 기준 초로 붙입니다.
QString DisconnectText(const hwlab::platform::PpgRecord& record,
                       const std::optional<hwlab::record::PpgErrorWindow>& window) {
  if (!record.disconnected_at) return {};
  const QString why = QString::fromStdString(record.disconnect_reason);
  if (!window) {
    const double after = static_cast<double>(record.disconnected_at->us - record.begin_time.us) / 1e6;
    return QStringLiteral(", 촬영 버튼 뒤 %1초에 PPG 연결이 끊김(%2)").arg(after, 0, 'f', 1).arg(why);
  }
  const double at = static_cast<double>(record.disconnected_at->us - window->begin.us) / 1e6;
  if (at < 0) return QStringLiteral(", 녹화 시작 전에 PPG 연결이 끊김(%1)").arg(why);
  return QStringLiteral(", 녹화 %1초에 PPG 연결이 끊김(%2)").arg(at, 0, 'f', 1).arg(why);
}

PpgSaveResult SaveRecord(const hwlab::platform::PpgRecord& record, const std::string& folder, const std::string& stem,
                         const std::string& port, const std::vector<std::string>& rgb_sidecars) {
  std::string window_why;
  const auto window = SummarizeInRgbWindow(record, rgb_sidecars, &window_why);
  const QString lost = DisconnectText(record, window);
  if (record.values.empty()) {
    return {QStringLiteral("PPG 파형을 받지 못했습니다(센서와 허브 연결을 확인하십시오)") + lost, false};
  }
  const std::string samples_file = stem + hwlab::platform::kPpgFileSuffix;
  const std::string sidecar_file = stem + hwlab::platform::kPpgSidecarSuffix;
  std::string why;
  if (!hwlab::platform::WritePpgCsv(folder + "/" + samples_file, record, &why) ||
      !hwlab::platform::WritePpgSidecar(folder + "/" + sidecar_file, record, samples_file, port, window, &why)) {
    return {QStringLiteral("PPG 저장 실패: ") + QString::fromStdString(why), true};
  }
  const double seconds =
      record.common_time.empty() ? 0 : (record.common_time.back().us - record.common_time.front().us) / 1e6;
  QString text = QStringLiteral("PPG %1개 표본 저장(%2초)").arg(record.values.size()).arg(seconds, 0, 'f', 1) + lost;
  if (!window) return {text + QStringLiteral(", 녹화 구간 측정 오류는 세지 못했습니다: ") + QString::fromStdString(window_why), false};
  if (window->codes.empty()) return {text + QStringLiteral(", 녹화 구간 측정 오류 없음"), false};
  QStringList shares;
  for (const hwlab::record::PpgErrorShare& share : window->codes) {
    shares.append(QString::fromStdString(hwlab::platform::PpgErrorShareText(share, window->samples)));
  }
  return {text + QStringLiteral(", 녹화 구간 측정 오류: ") + shares.join(QStringLiteral(", ")), false};
}

}  // namespace

PpgController::PpgController(hwlab::platform::SerialPortOpener opener,
                             std::function<std::vector<hwlab::platform::SerialPortInfo>()> list_ports,
                             RememberedValues* remembered_values, QString remembered, QObject* parent)
    : QObject(parent),
      remembered_values_(remembered_values),
      remembered_(remembered.isEmpty() ? remembered_values->Value(QLatin1String(kIdentityKey)).toString()
                                       : remembered.toUpper()) {
  // 장치 스레드는 화면 스레드로 넘기기만 합니다. 이 객체가 사라지기 전에 sensor_가 콜백을 지웁니다.
  hwlab::platform::PpgSensor::Callbacks callbacks;
  callbacks.state = [this](const hwlab::platform::PpgSnapshot& snapshot) {
    QMetaObject::invokeMethod(this, [this, snapshot] { ApplySnapshot(snapshot); }, Qt::QueuedConnection);
  };
  callbacks.wave = [this](std::vector<double> points) {
    QList<qreal> wave(points.begin(), points.end());
    QMetaObject::invokeMethod(this, [this, wave = std::move(wave)] { ApplyWave(wave); }, Qt::QueuedConnection);
  };
  callbacks.ports = [this](std::vector<hwlab::platform::SerialPortInfo> infos) {
    QMetaObject::invokeMethod(this, [this, infos = std::move(infos)] { SetPorts(infos); }, Qt::QueuedConnection);
  };
  sensor_ = std::make_unique<hwlab::platform::PpgSensor>(std::move(opener), std::move(list_ports), std::move(callbacks));
  connect(&stall_timer_, &QTimer::timeout, this, &PpgController::CheckStall);
  stall_timer_.start(kStallCheckMs);
  connect(&reconnect_timer_, &QTimer::timeout, this, &PpgController::PollForReconnect);
  reconnect_timer_.start(kAutoRetryMs);
  refreshPorts();
}

PpgController::~PpgController() {
  stall_timer_.stop();
  reconnect_timer_.stop();
  sensor_.reset();
}

bool PpgController::connected() const {
  using hwlab::platform::PpgState;
  return snapshot_.state == PpgState::kOpening || snapshot_.state == PpgState::kChecking ||
         snapshot_.state == PpgState::kPreparing || snapshot_.state == PpgState::kWaiting ||
         snapshot_.state == PpgState::kStreaming || snapshot_.state == PpgState::kStalled;
}

void PpgController::refreshPorts() { sensor_->RefreshPorts(); }

void PpgController::connectPort(const QString& name) {
  if (name.isEmpty() || name == light_open_) return;
  auto_connect_ = true;
  auto_attempts_ = 0;
  sensor_->Connect(name.toStdString());
}

void PpgController::MaybeAutoConnect() {
  if (!auto_connect_ || record_ || connected()) return;
  const QString port = LastPpgPort();
  if (port.isEmpty()) {
    // 장치가 빠졌거나 같은 장치가 둘 이상 보입니다. 다시 꽂으면 처음부터 다시 시도합니다.
    auto_attempts_ = 0;
    return;
  }
  if (port == light_open_ || auto_attempts_ >= kMaxAutoAttempts) return;
  const int64_t now = SteadyNowUs();
  if (last_auto_us_ != 0 && now - last_auto_us_ < int64_t{kAutoRetryMs} * 1000) return;
  last_auto_us_ = now;
  ++auto_attempts_;
  sensor_->Connect(port.toStdString());
}

void PpgController::PollForReconnect() {
  // 기억한 장치가 없거나 연결돼 있거나 촬영 중이면 포트 목록을 읽지 않습니다. 녹화 중에는 장치 상태를 바꾸지 않습니다.
  if (!auto_connect_ || remembered_.isEmpty() || record_ || connected()) return;
  refreshPorts();
}

QString PpgController::LastPpgPort() const {
  if (remembered_.isEmpty()) return {};
  QString found;
  int matches = 0;
  for (const auto& p : infos_) {
    if (IdentityOf(p) != remembered_) continue;
    found = QString::fromStdString(p.name);
    ++matches;
  }
  return matches == 1 ? found : QString();
}

void PpgController::SetLightPort(const QString& name) {
  if (name == light_open_) return;
  light_open_ = name;
  UpdatePortRows();
}

void PpgController::disconnectPort() {
  auto_connect_ = false;
  sensor_->Disconnect();
}

void PpgController::SetPorts(std::vector<hwlab::platform::SerialPortInfo> infos) {
  infos_ = std::move(infos);
  UpdatePortRows();
  MaybeAutoConnect();
}

void PpgController::UpdatePortRows() {
  QVariantList list;
  for (const auto& p : infos_) {
    QVariantMap item;
    item[QStringLiteral("name")] = QString::fromStdString(p.name);
    item[QStringLiteral("description")] = QString::fromStdString(p.description);
    item[QStringLiteral("vidPid")] = p.usb_vendor_id && p.usb_product_id
                                         ? QStringLiteral("%1:%2")
                                               .arg(*p.usb_vendor_id, 4, 16, QLatin1Char('0'))
                                               .arg(*p.usb_product_id, 4, 16, QLatin1Char('0'))
                                               .toUpper()
                                         : QString();
    item[QStringLiteral("serial")] = QString::fromStdString(p.serial_number);
    item[QStringLiteral("suggested")] = !remembered_.isEmpty() && IdentityOf(p) == remembered_;
    item[QStringLiteral("lightOpen")] = QString::fromStdString(p.name) == light_open_;
    list.append(item);
  }
  if (list == ports_) return;
  ports_ = list;
  emit portsChanged();
}

void PpgController::RememberPort(const QString& name) {
  if (name == remembered_port_) return;
  remembered_port_ = name;
  for (const auto& p : infos_) {
    if (QString::fromStdString(p.name) != name || IdentityOf(p).isEmpty()) continue;
    remembered_ = IdentityOf(p);
    remembered_values_->SetValue(QLatin1String(kIdentityKey), remembered_);
  }
  UpdatePortRows();
}

void PpgController::ApplySnapshot(const hwlab::platform::PpgSnapshot& snapshot) {
  using hwlab::platform::PpgState;
  if (snapshot == snapshot_) return;
  snapshot_ = snapshot;
  // 포트를 연 것만으로는 PPG 장치인지 알 수 없어, 측정 데이터를 받은 뒤에 남깁니다. 조명 포트를 잘못 골라도 남지 않습니다.
  if (snapshot_.state == PpgState::kStreaming) {
    auto_attempts_ = 0;
    RememberPort(QString::fromStdString(snapshot_.port));
  } else if (!connected()) {
    remembered_port_.clear();
  }
  emit stateChanged();
}

void PpgController::ApplyWave(const QList<qreal>& wave) {
  if (wave == wave_) return;
  wave_ = wave;
  emit waveChanged();
}

void PpgController::CheckStall() {
  const int64_t now = SteadyNowUs();
  const int64_t pending = sensor_->PendingSinceUs();
  const bool stalled =
      (pending > 0 && now - pending > kStallUs) || (connected() && now - sensor_->LastLoopUs() > kStallUs);
  if (stalled == stalled_) return;
  stalled_ = stalled;
  emit stateChanged();
}

void PpgController::BeginShot() {
  if (!connected() || record_) return;
  record_ = std::make_shared<hwlab::platform::PpgShotRecord>(hwlab::Timestamp{SteadyNowUs()});
  sensor_->BeginShot(record_);
  emit stateChanged();
}

std::function<PpgSaveResult()> PpgController::EndShot(const std::string& folder, const std::string& stem,
                                                std::vector<std::string> rgb_sidecars) {
  if (!record_) return {};
  std::shared_ptr<hwlab::platform::PpgShotRecord> record = std::move(record_);
  record->Close(hwlab::Timestamp{SteadyNowUs()});
  sensor_->EndShot();
  emit stateChanged();
  return [record, folder, stem, rgb_sidecars = std::move(rgb_sidecars), port = snapshot_.port] {
    return SaveRecord(record->Finish(), folder, stem, port, rgb_sidecars);
  };
}

void PpgController::CancelShot() {
  if (!record_) return;
  record_->Close(hwlab::Timestamp{SteadyNowUs()});
  record_.reset();
  sensor_->EndShot();
  emit stateChanged();
}

QString PpgController::statusText() const {
  using hwlab::platform::PpgState;
  if (stalled_) return QStringLiteral("PPG 장치가 응답하지 않습니다(USB를 뽑았다 다시 꽂으십시오)");
  switch (snapshot_.state) {
    case PpgState::kOff:
      return QStringLiteral("연결 안 됨");
    case PpgState::kFailed:
      return snapshot_.problem.empty() ? QStringLiteral("PPG 포트가 닫혔습니다")
                                       : QString::fromStdString(snapshot_.problem);
    case PpgState::kOpening:
      return QStringLiteral("연결 중");
    case PpgState::kChecking:
      return QStringLiteral("PPG 장치 확인 중");
    case PpgState::kPreparing:
      return QStringLiteral("PPG 장치 준비 중");
    case PpgState::kWaiting:
    case PpgState::kStalled:
      if (snapshot_.device_error != 0) return QStringLiteral("장치 오류 '%1'").arg(QChar(snapshot_.device_error));
      return snapshot_.state == PpgState::kStalled ? QStringLiteral("파형이 멈췄습니다(USB 허브를 거쳐 연결하십시오)")
                                                   : QStringLiteral("파형 기다리는 중");
    case PpgState::kStreaming:
      break;
  }
  const std::string why = hwlab::platform::PpgMeasureErrorText(snapshot_.measure_error);
  // 신호 나쁨('s')에도 장치는 심박수를 새로 재서 보내므로 값을 함께 보입니다.
  // 손가락 빠짐과 센서 연결 안 됨에는 심박수 없이 SpO2만 같은 값으로 와서 값을 뺍니다.
  if (!why.empty() && snapshot_.measure_error != 's') return QString::fromStdString(why);
  QString text = why.empty() ? (recording() ? QStringLiteral("기록 중") : QStringLiteral("수신 중"))
                             : QString::fromStdString(why);
  if (snapshot_.heart_rate > 0) text += QStringLiteral("  %1 bpm").arg(snapshot_.heart_rate);
  if (snapshot_.spo2 > 0) text += QStringLiteral("  SpO2 %1%").arg(snapshot_.spo2);
  return text;
}

int PpgController::wavePoints() const {
  return static_cast<int>(hwlab::platform::kPpgWaveSamples / hwlab::platform::kPpgWaveGroup);
}

QString PpgController::tone() const {
  using hwlab::platform::PpgState;
  if (stalled_) return QStringLiteral("error");
  switch (snapshot_.state) {
    case PpgState::kOff:
      return QStringLiteral("off");
    case PpgState::kFailed:
      return QStringLiteral("error");
    case PpgState::kOpening:
    case PpgState::kChecking:
    case PpgState::kPreparing:
    case PpgState::kWaiting:
    case PpgState::kStalled:
      return QStringLiteral("warn");
    case PpgState::kStreaming:
      break;
  }
  if (snapshot_.device_error != 0 || snapshot_.measure_error != 0) return QStringLiteral("warn");
  return QStringLiteral("ok");
}
