#include "hwlab/platform/light.h"

#include <algorithm>
#include <utility>

namespace hwlab::platform {

namespace {

constexpr uint8_t kLightHeader = 0xFE;

}  // namespace

int32_t ClampLightPercent(int32_t percent) { return std::clamp(percent, kLightMinPercent, kLightMaxPercent); }

LightCommand MakeLightCommand(int32_t percent) {
  return {kLightHeader, static_cast<uint8_t>(ClampLightPercent(percent))};
}

std::optional<LightPortIdentity> IdentityOf(const SerialPortInfo& port) {
  if (!port.usb_vendor_id || !port.usb_product_id) return std::nullopt;
  return LightPortIdentity{*port.usb_vendor_id, *port.usb_product_id, port.serial_number};
}

std::optional<std::string> FindRememberedLightPort(const std::vector<SerialPortInfo>& ports,
                                                   const LightPortIdentity& remembered) {
  std::optional<std::string> found;
  int32_t matches = 0;
  for (const SerialPortInfo& port : ports) {
    const auto identity = IdentityOf(port);
    if (!identity || identity->usb_vendor_id != remembered.usb_vendor_id ||
        identity->usb_product_id != remembered.usb_product_id) {
      continue;
    }
    if (!remembered.serial_number.empty() && identity->serial_number != remembered.serial_number) continue;
    found = port.name;
    ++matches;
  }
  return matches == 1 ? found : std::nullopt;
}

Light::Light(SerialPortOpener opener) : opener_(std::move(opener)) {}

Light::~Light() { Close(); }

bool Light::Open(const std::string& port_name, int32_t percent, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  CloseLocked();
  std::string reason;
  port_ = opener_(port_name, kLightSerial, &reason);
  if (port_ == nullptr) {
    if (error != nullptr) *error = reason.empty() ? port_name + " 포트를 열지 못했습니다" : reason;
    return false;
  }
  // 보드의 현재 밝기를 읽을 수 없으므로 열자마자 보내 LastSentPercent를 장치와 맞춥니다.
  return SendLocked(percent, error);
}

bool Light::SetPercent(int32_t percent, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (port_ == nullptr) {
    if (error != nullptr) *error = "조명이 연결되어 있지 않습니다";
    return false;
  }
  return SendLocked(percent, error);
}

bool Light::SendLocked(int32_t percent, std::string* error) {
  const LightCommand command = MakeLightCommand(percent);
  if (!port_->Write(command, kLightWriteTimeoutMs)) {
    // 슬라이더를 끌면 연이어 불리므로, 빠진 장치를 연결된 채로 두면 호출마다 150ms씩 기다리게 됩니다.
    if (error != nullptr) *error = port_->Name() + " 포트에 밝기를 보내지 못해 연결을 끊었습니다";
    CloseLocked();
    return false;
  }
  last_sent_percent_ = command[1];
  return true;
}

std::optional<int32_t> Light::LastSentPercent() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (port_ == nullptr || !port_->IsOpen()) return std::nullopt;
  return last_sent_percent_;
}

bool Light::IsConnected() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return port_ != nullptr && port_->IsOpen();
}

std::string Light::PortName() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return port_ != nullptr ? port_->Name() : std::string();
}

void Light::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  CloseLocked();
}

void Light::CloseLocked() {
  if (port_ != nullptr) {
    port_->Close();
    port_.reset();
  }
  last_sent_percent_.reset();
}

}  // namespace hwlab::platform
