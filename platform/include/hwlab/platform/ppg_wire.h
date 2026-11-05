#ifndef HWLAB_PLATFORM_PPG_WIRE_H_
#define HWLAB_PLATFORM_PPG_WIRE_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hwlab::platform {

/**
 * PPG 장치와 주고받는 메시지 하나입니다.
 *
 * 형식은 `FF`, 발신지(PC 0x55, 장치 0x4D), 길이 2바이트(큰 쪽 먼저, 코드부터 CRC까지의 바이트 수), 코드 1바이트,
 * 데이터, CRC16 2바이트(작은 쪽 먼저)입니다. 전체 크기는 4 + 길이입니다.
 *
 * @note 제조사 장치 사양서(MP-SDK Interface Basic Rev.06, 2025-07-31, 이 레포에는 없음)와 실기(2026-10-19~20, 이슈 #4)로
 *       확인한 형식입니다.
 */
struct PpgMessage {
  uint8_t source = 0;
  uint8_t code = 0;
  std::vector<uint8_t> data;
  /** 메시지의 마지막 바이트가 들어 있던 수신 덩어리의 PC 시각입니다. */
  int64_t host_time_us = 0;
};

/** 메시지 발신지입니다. */
inline constexpr uint8_t kPpgFromPc = 0x55;      // 'U'
inline constexpr uint8_t kPpgFromDevice = 0x4D;  // 'M'

/** 장치가 보내는 메시지 코드입니다. */
inline constexpr uint8_t kPpgMeasure = 'm';  // 파형 표본, 심박수, SpO2, 측정 오류
inline constexpr uint8_t kPpgResult = 'r';
inline constexpr uint8_t kPpgStatus = 's';
inline constexpr uint8_t kPpgError = 'e';
inline constexpr uint8_t kPpgInfo = 'i';
inline constexpr uint8_t kPpgTime = 't';

/** 상태 메시지의 값입니다. */
inline constexpr uint8_t kPpgStatusBoot = 'o';
inline constexpr uint8_t kPpgStatusPreview = 'v';
inline constexpr uint8_t kPpgStatusMeasuring = 's';
inline constexpr uint8_t kPpgStatusFinished = 'p';
inline constexpr uint8_t kPpgStatusStopped = 'u';

/**
 * 장치의 파형 표본 주기입니다.
 *
 * @note 장치 표본 시계는 PC보다 약 0.5% 느립니다(2026-10-19 실측, 30초에 약 0.15초). 표본 번호만으로 시각을 매기지 않고
 *       PC 수신 시각으로 맞춥니다.
 */
inline constexpr int32_t kPpgSampleHz = 500;

/**
 * CRC16을 계산합니다. 초기값 0xFFFF, 다항식 0xA001(반사)입니다.
 *
 * @param bytes 머리(FF)부터 데이터 끝까지
 * @return CRC 값. 메시지에는 작은 쪽 바이트를 먼저 씁니다
 * @note 사양서 예제 `FF 55 00 03 49`의 CRC 바이트는 `E0 DE`입니다.
 */
uint16_t PpgCrc16(std::span<const uint8_t> bytes);

/**
 * PC가 보내는 메시지를 만듭니다.
 *
 * @param code 명령 코드
 * @param data 데이터
 * @return 머리부터 CRC까지의 바이트
 */
std::vector<uint8_t> BuildPpgCommand(uint8_t code, std::span<const uint8_t> data = {});

/** 측정 시작 명령에 넣는 값입니다. */
struct PpgStartRequest {
  /** 미리보기 길이(초). 장치는 10~60을 받습니다. 파형은 미리보기 동안에도 옵니다. */
  int32_t preview_s = 60;
  /** 측정 길이(초). 장치는 60 또는 150만 받습니다. */
  int32_t measure_s = 60;
  /** 센서 종류. 'P' 집게형, 'F' 손가락형입니다. 실험 장치는 'P'입니다. */
  char sensor = 'P';
  /** 성별 'M' 또는 'F'. 장치의 결과 계산에만 쓰이고 파형에는 영향이 없습니다. */
  char gender = 'M';
  int32_t age = 40;
  /** 결과 기준 'A' 동양인, 'W' 서양인입니다. */
  char reference = 'A';
};

/**
 * 측정 시작 명령을 만듭니다.
 *
 * @return 메시지. 값이 장치가 받는 범위를 벗어나면 nullopt이고 error에 이유를 씁니다
 */
std::optional<std::vector<uint8_t>> BuildPpgStart(const PpgStartRequest& request, std::string* error = nullptr);

/** 측정 중지 명령입니다. 장치는 상태 'u'로 답합니다(실측 32ms). */
std::vector<uint8_t> BuildPpgStop();

/** 장치 정보 요청 명령입니다. 장치는 'i' 메시지(ASCII 15바이트)로 답합니다. */
std::vector<uint8_t> BuildPpgInfoRequest();

/** PpgParser가 버린 바이트와 메시지 수입니다. 통신 상태를 기록에 남기는 데 씁니다. */
struct PpgParserCounts {
  int64_t crc_failures = 0;
  int64_t bad_lengths = 0;
  /** 사양서에 없는 코드이거나 코드에 맞지 않는 길이라 버린 시작 후보입니다. */
  int64_t implausible = 0;
  int64_t skipped_bytes = 0;
  int64_t messages = 0;
};

/**
 * 받은 바이트 덩어리를 메시지로 나눕니다. CRC가 맞지 않거나 형식이 어긋나면 1바이트씩 건너뛰며 다시 맞춥니다.
 *
 * @note 코드마다 길이가 하나로 정해진 메시지(상태, 오류, 정보, 시간)는 길이도 검사합니다. 위치를 잃었을 때 표본 값 안의
 *       `FF 4D`를 시작으로 잘못 잡고 수천 바이트를 기다리지 않기 위해서입니다.
 * @note 한 스레드에서만 씁니다.
 */
class PpgParser {
 public:
  /**
   * 바이트를 더하고 완성된 메시지를 돌려줍니다.
   *
   * @param bytes 직렬 포트에서 읽은 덩어리
   * @param host_time_us 덩어리를 읽은 PC 시각. 이 덩어리에서 끝나는 메시지에 붙습니다
   * @return 이번에 완성된 메시지들. 받은 순서입니다
   */
  std::vector<PpgMessage> Feed(std::span<const uint8_t> bytes, int64_t host_time_us);

  PpgParserCounts Counts() const { return counts_; }

 private:
  std::vector<uint8_t> buffer_;
  PpgParserCounts counts_;
};

/** 측정 데이터 메시지('m') 하나를 푼 값입니다. */
struct PpgMeasure {
  /** 파형 표본(부호 없는 16비트). 500Hz 순서대로입니다. */
  std::vector<uint16_t> samples;
  /** 장치가 계산한 심박수(bpm). 계산 전이면 0입니다. */
  int32_t heart_rate = 0;
  /** 장치가 계산한 SpO2(%). 계산 전이면 0입니다. */
  int32_t spo2 = 0;
  /** 측정 오류. 0 정상, 'f' 손가락 빠짐, 's' 신호 나쁨, 'p' 센서 연결 안 됨입니다. */
  uint8_t error = 0;
};

/**
 * 측정 데이터 메시지의 데이터를 풉니다. 형식은 16비트 표본 N개(큰 쪽 먼저), 심박수, SpO2, 측정 오류 각 1바이트입니다.
 *
 * @param data PpgMessage::data
 * @return 푼 값. 끝 3바이트를 뺀 길이가 홀수이거나 3바이트보다 짧으면 nullopt
 */
std::optional<PpgMeasure> DecodePpgMeasure(std::span<const uint8_t> data);

/** 측정 오류 값을 화면에 보일 문장으로 바꿉니다. 0이면 빈 문자열입니다. */
std::string PpgMeasureErrorText(uint8_t error);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_PPG_WIRE_H_
