#include "light_controller.h"

#include <QVariantMap>

#include <cstdio>
#include <utility>

#include "hwlab/flow/flow.h"
#include "remembered_values.h"

namespace {

constexpr char kVidPidKey[] = "light/vid_pid";
constexpr char kSerialKey[] = "light/serial";

QString VidPidText(uint16_t vid, uint16_t pid) {
  char text[16];
  std::snprintf(text, sizeof(text), "%04X:%04X", vid, pid);
  return QString::fromLatin1(text);
}

std::optional<hwlab::platform::LightPortIdentity> ReadRemembered(const RememberedValues& values) {
  const QStringList parts = values.Value(QLatin1String(kVidPidKey)).toString().split(QLatin1Char(':'));
  if (parts.size() != 2) return std::nullopt;
  bool vid_ok = false;
  bool pid_ok = false;
  const uint vid = parts[0].toUInt(&vid_ok, 16);
  const uint pid = parts[1].toUInt(&pid_ok, 16);
  if (!vid_ok || !pid_ok || vid > 0xFFFF || pid > 0xFFFF) return std::nullopt;
  return hwlab::platform::LightPortIdentity{static_cast<uint16_t>(vid), static_cast<uint16_t>(pid),
                                            values.Value(QLatin1String(kSerialKey)).toString().toStdString()};
}

QString ResponseState(const hwlab::platform::LightStatus& s) {
  if (s.judging) return QStringLiteral("judging");
  switch (s.response) {
    case hwlab::pixel::LightResponse::kApplied: return QStringLiteral("applied");
    case hwlab::pixel::LightResponse::kNotSeen: return QStringLiteral("not_seen");
    case hwlab::pixel::LightResponse::kUnknown: return QStringLiteral("unknown");
  }
  return QStringLiteral("unknown");
}

}  // namespace

LightController::LightController(std::unique_ptr<hwlab::platform::Light> light,
                                 std::function<std::vector<hwlab::platform::SerialPortInfo>()> list_ports,
                                 std::string product, RememberedValues* remembered_values,
                                 std::optional<hwlab::platform::LightPortIdentity> remembered)
    : light_(std::move(light)),
      list_ports_(std::move(list_ports)),
      product_(std::move(product)),
      remembered_values_(remembered_values),
      remembered_(remembered ? remembered : ReadRemembered(*remembered_values)) {
  refreshPorts();
}

void LightController::refreshPorts() {
  infos_ = list_ports_ ? list_ports_() : std::vector<hwlab::platform::SerialPortInfo>{};
  const std::optional<std::string> suggested =
      remembered_ ? hwlab::platform::FindRememberedLightPort(infos_, *remembered_) : std::nullopt;
  suggested_ = suggested ? QString::fromStdString(*suggested) : QString();
  UpdatePortRows();
}

void LightController::UpdatePortRows() {
  QVariantList list;
  for (const auto& info : infos_) {
    const QString name = QString::fromStdString(info.name);
    QVariantMap row;
    row[QStringLiteral("name")] = name;
    row[QStringLiteral("vidPid")] = info.usb_vendor_id && info.usb_product_id
                                        ? VidPidText(*info.usb_vendor_id, *info.usb_product_id)
                                        : QString();
    row[QStringLiteral("serial")] = QString::fromStdString(info.serial_number);
    row[QStringLiteral("description")] = QString::fromStdString(info.description);
    row[QStringLiteral("suggested")] = name == suggested_;
    row[QStringLiteral("ppgOpen")] = name == ppg_open_;
    row[QStringLiteral("lastPpg")] = name == last_ppg_;
    list.push_back(row);
  }
  if (list == ports_) return;
  ports_ = list;
  emit portsChanged();
}

void LightController::SetPpgPorts(const QString& open, const QString& last_ppg) {
  if (open == ppg_open_ && last_ppg == last_ppg_) return;
  ppg_open_ = open;
  last_ppg_ = last_ppg;
  UpdatePortRows();
}

void LightController::Update(const hwlab::platform::CaptureStatus& status) {
  const hwlab::platform::LightStatus& s = status.light;
  bool changed = false;
  const auto set = [&changed](auto& field, const auto& value) {
    if (field == value) return;
    field = value;
    changed = true;
  };
  set(connected_, s.connected);
  set(port_name_, s.connected ? QString::fromStdString(s.port) : QString());
  set(percent_, s.connected ? s.percent : std::nullopt);
  set(busy_, s.pending > 0);
  set(can_change_, hwlab::flow::AllowsLightChange(status.phase) && s.pending == 0);
  set(response_state_, ResponseState(s));
  set(error_, QString::fromStdString(s.error));
  // 쓰기 실패로 연결이 끊기면 확인도 지웁니다. 다시 연 포트가 같은 보드라는 근거가 없기 때문입니다.
  if (!s.connected && s.pending == 0) set(confirmed_, false);
  if (changed) emit stateChanged();
}

hwlab::record::LightingInfo LightController::Lighting() const {
  hwlab::record::LightingInfo info;
  info.product = product_;
  if (connected_ && percent_) {
    info.source = hwlab::record::LightSource::kApp;
    info.brightness_percent = percent_;
  } else {
    info.source = hwlab::record::LightSource::kExternal;
  }
  return info;
}

hwlab::record::LightRecord LightController::SidecarLight() const {
  const hwlab::record::LightingInfo lighting = Lighting();
  hwlab::record::LightRecord record;
  record.source = lighting.source;
  record.percent = lighting.brightness_percent;
  if (record.source != hwlab::record::LightSource::kApp) return record;
  if (requested_port_ && QString::fromStdString(requested_port_->name) == port_name_) {
    record.usb_vendor_id = requested_port_->usb_vendor_id;
    record.usb_product_id = requested_port_->usb_product_id;
    record.serial = requested_port_->serial_number;
  }
  record.confirmed = confirmed_;
  return record;
}

QString LightController::responseText() const {
  if (response_state_ == QStringLiteral("judging")) return QStringLiteral("영상 밝기 변화 판정 중");
  if (response_state_ == QStringLiteral("applied")) return QStringLiteral("영상 밝기가 LED를 따라 바뀜");
  if (response_state_ == QStringLiteral("not_seen")) return QStringLiteral("영상 밝기 변화가 보이지 않음");
  return QStringLiteral("영상 밝기 변화는 판정하지 않음");
}

QString LightController::summary() const {
  const QString product = product_.empty() ? QString() : QStringLiteral(" (%1)").arg(QString::fromStdString(product_));
  const hwlab::record::LightingInfo lighting = Lighting();
  if (lighting.source != hwlab::record::LightSource::kApp) {
    return QStringLiteral("조명: 앱 밖에서 켬, 밝기 미확인") + product;
  }
  return QStringLiteral("조명: 앱 제어 %1%, %2%3")
             .arg(*lighting.brightness_percent)
             .arg(port_name_, confirmed_ ? QString() : QStringLiteral(", 연결 확인 전")) +
         product;
}

void LightController::connectPort(const QString& name, int percent) {
  if (name.isEmpty() || name == ppg_open_) return;
  // 열자마자 밝기 명령을 보내므로 지난번 PPG 장치로 연결한 포트는 보내기 전에 사람에게 묻습니다.
  if (name == last_ppg_) {
    pending_port_ = name;
    emit stateChanged();
    return;
  }
  cancelPending();
  Open(name, percent);
}

void LightController::openPending(int percent) {
  const QString name = pending_port_;
  cancelPending();
  if (!name.isEmpty() && name != ppg_open_) Open(name, percent);
}

void LightController::cancelPending() {
  if (pending_port_.isEmpty()) return;
  pending_port_.clear();
  emit stateChanged();
}

void LightController::Open(const QString& name, int percent) {
  if (capture_ == nullptr) return;
  const std::string port = name.toStdString();
  if (!capture_->OpenLight(port, percent)) return;
  requested_port_.reset();
  for (const auto& info : infos_) {
    if (info.name == port) requested_port_ = info;
  }
  confirmed_ = false;
  busy_ = true;
  emit stateChanged();
}

void LightController::disconnectPort() {
  if (capture_ == nullptr) return;
  capture_->CloseLight();
  confirmed_ = false;
  emit stateChanged();
}

bool LightController::apply(int percent) {
  if (capture_ == nullptr || !connected_ || !capture_->SetLight(percent)) return false;
  busy_ = true;
  emit stateChanged();
  return true;
}

void LightController::confirm() {
  if (!connected_ || confirmed_) return;
  confirmed_ = true;
  emit stateChanged();
  if (!requested_port_ || QString::fromStdString(requested_port_->name) != port_name_) return;
  const auto identity = hwlab::platform::IdentityOf(*requested_port_);
  if (!identity) return;
  remembered_ = identity;
  // 화면에서 확인한 변환기만 기억합니다. --light-remember로 준 값은 기억하지 않고 이번 실행에만 씁니다.
  remembered_values_->SetValue(QLatin1String(kVidPidKey), VidPidText(identity->usb_vendor_id, identity->usb_product_id));
  remembered_values_->SetValue(QLatin1String(kSerialKey), QString::fromStdString(identity->serial_number));
  refreshPorts();
}
