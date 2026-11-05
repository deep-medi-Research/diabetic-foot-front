#include "hwlab/platform/ppg_wire.h"

#include <cstddef>

namespace hwlab::platform {

namespace {

constexpr uint8_t kHeader = 0xFF;
// 가장 긴 메시지는 추가 APG 결과(길이 0x12EA)입니다. 이보다 크면 잘못 맞춘 시작으로 보고 다시 찾습니다.
constexpr size_t kMaxLength = 5000;

// 장치가 보내는 코드와 PC가 보내는 코드입니다. 이 밖의 코드는 잘못 맞춘 시작으로 봅니다.
bool KnownCode(uint8_t code) {
  switch (code) {
    case 'D':
    case 'P':
    case 'R':
    case 'I':
    case 'T':
    case kPpgMeasure:
    case kPpgResult:
    case kPpgStatus:
    case kPpgError:
    case kPpgInfo:
    case kPpgTime:
      return true;
    default:
      return false;
  }
}

bool Plausible(uint8_t code, size_t length) {
  if (!KnownCode(code)) return false;
  switch (code) {
    case kPpgStatus:
    case kPpgError:
      return length == 4;
    case kPpgInfo:
    case kPpgTime:
      return length == 0x12;
    case kPpgMeasure:
      // 코드 1 + 표본 2N + 끝 3바이트 + CRC 2이므로 짝수입니다.
      return length % 2 == 0;
    default:
      return true;
  }
}

}  // namespace

uint16_t PpgCrc16(std::span<const uint8_t> bytes) {
  uint16_t crc = 0xFFFF;
  for (const uint8_t b : bytes) {
    crc ^= b;
    for (int i = 0; i < 8; ++i) crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001) : static_cast<uint16_t>(crc >> 1);
  }
  return crc;
}

std::vector<uint8_t> BuildPpgCommand(uint8_t code, std::span<const uint8_t> data) {
  const size_t length = 1 + data.size() + 2;
  std::vector<uint8_t> out = {kHeader, kPpgFromPc, static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length & 0xFF),
                              code};
  out.insert(out.end(), data.begin(), data.end());
  const uint16_t crc = PpgCrc16(out);
  // 사양서 예제 코드의 uchCRCHi가 먼저 나가며, 이 값이 16비트 CRC의 하위 바이트입니다.
  out.push_back(static_cast<uint8_t>(crc & 0xFF));
  out.push_back(static_cast<uint8_t>(crc >> 8));
  return out;
}

std::optional<std::vector<uint8_t>> BuildPpgStart(const PpgStartRequest& request, std::string* error) {
  const auto fail = [&](const char* why) -> std::optional<std::vector<uint8_t>> {
    if (error != nullptr) *error = why;
    return std::nullopt;
  };
  if (request.preview_s < 10 || request.preview_s > 60) return fail("미리보기 길이는 10~60초입니다");
  if (request.measure_s != 60 && request.measure_s != 150) return fail("측정 길이는 60초 또는 150초입니다");
  if (request.sensor != 'P' && request.sensor != 'F') return fail("센서 종류는 P 또는 F입니다");
  if (request.gender != 'M' && request.gender != 'F') return fail("성별은 M 또는 F입니다");
  if (request.age < 1 || request.age > 120) return fail("나이는 1~120입니다");
  if (request.reference != 'A' && request.reference != 'W') return fail("결과 기준은 A 또는 W입니다");
  const uint8_t data[] = {static_cast<uint8_t>(request.preview_s), static_cast<uint8_t>(request.measure_s),
                          static_cast<uint8_t>(request.sensor),    static_cast<uint8_t>(request.gender),
                          static_cast<uint8_t>(request.age),       static_cast<uint8_t>(request.reference)};
  return BuildPpgCommand('D', data);
}

std::vector<uint8_t> BuildPpgStop() { return BuildPpgCommand('P'); }

std::vector<uint8_t> BuildPpgInfoRequest() { return BuildPpgCommand('I'); }

std::vector<PpgMessage> PpgParser::Feed(std::span<const uint8_t> bytes, int64_t host_time_us) {
  buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
  std::vector<PpgMessage> out;
  size_t pos = 0;
  const auto skip = [&](size_t n) {
    counts_.skipped_bytes += static_cast<int64_t>(n);
    pos += n;
  };
  while (pos < buffer_.size()) {
    // 머리 FF 뒤에 발신지가 오는 자리를 찾습니다. 발신지 바이트가 아직 없으면 다음 덩어리를 기다립니다.
    size_t start = pos;
    while (start < buffer_.size()) {
      if (buffer_[start] == kHeader &&
          (start + 1 == buffer_.size() || buffer_[start + 1] == kPpgFromPc || buffer_[start + 1] == kPpgFromDevice)) {
        break;
      }
      ++start;
    }
    skip(start - pos);
    if (buffer_.size() - pos < 5) break;
    const size_t length = (static_cast<size_t>(buffer_[pos + 2]) << 8) | buffer_[pos + 3];
    if (length < 3 || length > kMaxLength) {
      ++counts_.bad_lengths;
      skip(1);
      continue;
    }
    const uint8_t code = buffer_[pos + 4];
    if (!Plausible(code, length)) {
      ++counts_.implausible;
      skip(1);
      continue;
    }
    const size_t total = 4 + length;
    if (buffer_.size() - pos < total) break;
    const std::span<const uint8_t> frame(buffer_.data() + pos, total);
    const uint16_t crc = PpgCrc16(frame.first(total - 2));
    if (frame[total - 2] != (crc & 0xFF) || frame[total - 1] != (crc >> 8)) {
      ++counts_.crc_failures;
      skip(1);
      continue;
    }
    PpgMessage message;
    message.source = frame[1];
    message.code = code;
    message.data.assign(frame.begin() + 5, frame.end() - 2);
    message.host_time_us = host_time_us;
    out.push_back(std::move(message));
    ++counts_.messages;
    pos += total;
  }
  buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(pos));
  return out;
}

std::optional<PpgMeasure> DecodePpgMeasure(std::span<const uint8_t> data) {
  if (data.size() < 3) return std::nullopt;
  const size_t n2 = data.size() - 3;
  if (n2 % 2 != 0) return std::nullopt;
  PpgMeasure out;
  out.samples.reserve(n2 / 2);
  for (size_t i = 0; i < n2; i += 2) out.samples.push_back(static_cast<uint16_t>((data[i] << 8) | data[i + 1]));
  out.heart_rate = data[n2];
  out.spo2 = data[n2 + 1];
  out.error = data[n2 + 2];
  return out;
}

std::string PpgMeasureErrorText(uint8_t error) {
  switch (error) {
    case 0:
      return {};
    case 'f':
      return "손가락 빠짐";
    case 's':
      return "신호 나쁨";
    case 'p':
      return "센서 연결 안 됨";
    default:
      return "알 수 없는 측정 오류";
  }
}

}  // namespace hwlab::platform
