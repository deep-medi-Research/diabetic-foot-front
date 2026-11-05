#include "hwlab/platform/fake_ppg_device.h"

#include <cmath>
#include <numbers>
#include <span>
#include <string>
#include <utility>

namespace hwlab::platform {

FakePpgDevice::FakePpgDevice(std::shared_ptr<FakeSerialLine> line) : line_(std::move(line)) {}

std::vector<PpgMessage> FakePpgDevice::HandleCommands() {
  const std::vector<uint8_t> written = line_->Written();
  const std::span<const uint8_t> fresh(written.data() + handled_bytes_, written.size() - handled_bytes_);
  handled_bytes_ = written.size();
  std::vector<PpgMessage> commands = commands_.Feed(fresh, 0);
  for (const PpgMessage& command : commands) {
    if (command.source != kPpgFromPc) continue;
    if (command.code == 'I') ++info_request_count_;
    if (booting_) continue;
    switch (command.code) {
      case 'D':
        if (running_) {
          Emit(kPpgError, {'s'});
          break;
        }
        running_ = true;
        ++start_count_;
        next_sample_ = 0;
        last_start_data_ = command.data;
        EmitStatus(kPpgStatusPreview);
        break;
      case 'P':
        ++stop_count_;
        if (running_) EmitStatus(kPpgStatusStopped);
        running_ = false;
        break;
      case 'I':
        EmitInfo();
        break;
      default:
        break;
    }
  }
  return commands;
}

void FakePpgDevice::StartBooting() {
  booting_ = true;
  running_ = false;
}

void FakePpgDevice::FinishBooting() {
  booting_ = false;
  EmitInfo();
  EmitStatus(kPpgStatusBoot);
}

void FakePpgDevice::EmitInfo() {
  // 사양서 장치 정보 예제와 같은 15바이트입니다.
  const std::string info = "200000270270320";
  Emit(kPpgInfo, std::vector<uint8_t>(info.begin(), info.end()));
}

void FakePpgDevice::EmitMeasurement(size_t samples, uint8_t heart_rate, uint8_t spo2, uint8_t error) {
  std::vector<uint8_t> data;
  data.reserve(samples * 2 + 3);
  for (size_t i = 0; i < samples; ++i) {
    const uint16_t v = WaveAt(next_sample_++);
    data.push_back(static_cast<uint8_t>(v >> 8));
    data.push_back(static_cast<uint8_t>(v & 0xFF));
  }
  data.insert(data.end(), {heart_rate, spo2, error});
  Emit(kPpgMeasure, data);
}

void FakePpgDevice::EmitStatus(uint8_t status) {
  if (status == kPpgStatusFinished) running_ = false;
  Emit(kPpgStatus, {status});
}

void FakePpgDevice::EmitError(uint8_t error) {
  if (error == 'f' || error == 'm') running_ = false;
  Emit(kPpgError, {error});
}

uint16_t FakePpgDevice::WaveAt(int64_t sample) {
  const double t = static_cast<double>(sample) / kPpgSampleHz;
  return static_cast<uint16_t>(std::lround(2048.0 + 400.0 * std::sin(2.0 * std::numbers::pi * 1.2 * t)));
}

void FakePpgDevice::Emit(uint8_t code, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> bytes = BuildPpgCommand(code, data);
  // BuildPpgCommand는 PC 발신지로 만들므로 발신지를 장치로 바꾸고 CRC를 다시 계산합니다.
  bytes[1] = kPpgFromDevice;
  const uint16_t crc = PpgCrc16(std::span<const uint8_t>(bytes.data(), bytes.size() - 2));
  bytes[bytes.size() - 2] = static_cast<uint8_t>(crc & 0xFF);
  bytes[bytes.size() - 1] = static_cast<uint8_t>(crc >> 8);
  line_->Feed(bytes);
}

}  // namespace hwlab::platform
