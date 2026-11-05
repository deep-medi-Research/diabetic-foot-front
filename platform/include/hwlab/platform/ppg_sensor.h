#ifndef HWLAB_PLATFORM_PPG_SENSOR_H_
#define HWLAB_PLATFORM_PPG_SENSOR_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "hwlab/platform/ppg_wire.h"
#include "hwlab/platform/serial_port.h"
#include "hwlab/record/sidecar.h"
#include "hwlab/timestamp.h"
#include "hwlab/timing/clock_map.h"

namespace hwlab::platform {

/** PpgLink::Reading의 결과입니다. 장치 스레드가 장치를 다시 시작할 때를 정하는 데 씁니다(PpgNeedsRestart). */
struct PpgReading {
  /** 포트가 열려 있으면 true입니다. 장치 오류로 닫히면 false입니다. */
  bool open = false;
  /** 마지막 상태 메시지 값('v' 미리보기, 's' 측정 중, 'p' 측정 종료, 'u' 중지). 받은 적 없으면 0입니다. */
  uint8_t status = 0;
  /** 파형 표본이 오는 중인지. 마지막 측정 데이터를 받은 지 PpgLink::kStaleUs가 지나지 않았으면 true입니다. */
  bool streaming = false;
  /** 마지막 측정 데이터를 받은 뒤 흐른 시간(마이크로초)입니다. 시작 뒤 받은 적이 없으면 시작부터 셉니다. 시작 전이면 0입니다. */
  int64_t quiet_us = 0;
  /**
   * 장치가 마지막으로 보낸 0이 아닌 심박수(bpm)입니다. PpgLink::kHeartRateHoldUs 동안 새 값이 없거나 시작 뒤 받은 적이
   * 없으면 0입니다.
   *
   * @note 장치는 박동을 잡은 측정 데이터에만 심박수를 채우고 나머지는 0으로 보냅니다. 측정 데이터 731개 중 59개(약 0.58초마다)에
   *       심박수가 있었고 SpO2는 모두에 있었습니다(2026-10-21 실측). 마지막 측정 데이터의 값만 보이면 대부분 0이 됩니다.
   */
  int32_t heart_rate = 0;
  /** 마지막 측정 데이터의 SpO2, 측정 오류입니다. */
  int32_t spo2 = 0;
  uint8_t measure_error = 0;
  /** 시작 뒤 받은 표본 수입니다. */
  int64_t samples = 0;
  /** 장치가 보낸 오류 메시지('e') 값. 받은 적 없으면 0입니다. */
  uint8_t device_error = 0;
  /** 명령 실패 이유입니다. 없으면 빈 문자열입니다. */
  std::string problem;
  PpgParserCounts counts;
};

/**
 * 촬영 하나 동안 받은 PPG 파형입니다(PpgShotRecord::Finish).
 *
 * 표본 i의 장치 시각은 시작 뒤 표본 번호 / 500Hz이고, 측정 데이터 메시지마다 (마지막 표본의 장치 시각, PC 수신 시각)
 * 쌍을 남겨 공통 시간축으로 옮깁니다. 수신 빈틈(PpgLink::kReceiveGapUs)에서 시계 구간을 나눕니다.
 */
struct PpgRecord {
  /** 파형 표본 값입니다. */
  std::vector<uint16_t> values;
  /** 표본마다 시작 뒤 장치 시각(표본 번호 / 500Hz, 마이크로초)입니다. */
  std::vector<Timestamp> device_time;
  /** 표본마다 공통 시각(PC 시계)입니다. 그 표본이 든 clock의 시계 구간 식으로 device_time을 옮긴 값입니다. */
  std::vector<Timestamp> common_time;
  /** 표본마다 그 표본이 든 메시지의 측정 오류(0 정상, 'f' 손가락 빠짐 등)입니다. */
  std::vector<uint8_t> measure_error;
  /** 표본마다 그 표본이 든 메시지의 장치 심박수(bpm), SpO2(%)입니다. */
  std::vector<uint8_t> heart_rate;
  std::vector<uint8_t> spo2;
  /** 측정 데이터 메시지마다 (마지막 표본 장치 시각, PC 수신 시각)입니다. */
  std::vector<timing::ClockSample> anchors;
  /** anchors를 수신 빈틈에서 나눠 맞춘 시계 구간입니다. 장치를 다시 시작한 직후 빈틈이 있으면 둘 이상입니다. */
  std::vector<timing::ClockSegment> clock;
  /** 촬영 버튼을 누른 PC 시각과 촬영을 끝낸 PC 시각입니다. */
  Timestamp begin_time;
  Timestamp end_time;
  /** 기록하는 동안 해석기가 센 메시지 수와 버린 바이트입니다. */
  PpgParserCounts counts;
  /** 기록하는 동안 받은 상태 메시지와 오류 메시지입니다. 받은 순서입니다. */
  std::vector<record::PpgEvent> events;
  /** 기록 중 포트가 닫힌 PC 시각입니다. 끊기지 않았으면 비웁니다. 끊기기 전까지 받은 표본은 그대로 남습니다. */
  std::optional<Timestamp> disconnected_at;
  /** 포트가 닫힌 이유입니다. 끊기지 않았으면 빈 문자열입니다. */
  std::string disconnect_reason;
  /** 장치 정보 응답의 데이터입니다(PpgLink::DeviceInfo). 받지 못했으면 빈 문자열입니다. */
  std::string device_info;
};

/**
 * 촬영 하나의 PPG 기록입니다(PPG 촬영 기록). 촬영이 만들어 가지고, 장치 스레드는 기록하는 동안 표본과 상태 메시지를 더합니다.
 *
 * 기록의 수명이 포트 연결과 따로라서, 녹화 중 연결이 끊겨도 그때까지 받은 표본은 남고 끊긴 시각만 적힙니다. 촬영을 끝내는 쪽은
 * Close만 부르고 장치 스레드를 기다리지 않습니다.
 *
 * @note 모든 함수는 여러 스레드에서 불러도 됩니다.
 */
class PpgShotRecord {
 public:
  /** @param begin 촬영 버튼을 누른 PC 시각 */
  explicit PpgShotRecord(Timestamp begin);

  /** 촬영을 끝냅니다. 이 뒤로 더하는 표본과 메시지는 버립니다. 두 번째부터는 아무것도 하지 않습니다. */
  void Close(Timestamp end);

  /** Close를 불렀으면 true입니다. */
  bool Closed() const;

  /**
   * 지금까지 더한 표본을 시계 구간으로 맞춰 돌려줍니다. 부른 스레드에서 계산합니다.
   *
   * @return 기록. 표본마다 공통 시각을 채우고, 측정 데이터 사이 수신 빈틈에서 시계 구간을 나눕니다
   */
  PpgRecord Finish() const;

  /**
   * 측정 데이터 하나의 표본을 더합니다. 장치 스레드가 부릅니다.
   *
   * @param measure 측정 데이터
   * @param first_index 이 메시지 첫 표본의 시작 뒤 표본 번호
   * @param host_time 메시지를 받은 PC 시각
   */
  void AddMeasure(const PpgMeasure& measure, int64_t first_index, Timestamp host_time);

  /** 상태 메시지나 오류 메시지 하나를 더합니다. 장치 스레드가 부릅니다. */
  void AddEvent(const record::PpgEvent& event);

  /** 기록하는 동안의 해석기 집계를 바꿉니다. 장치 스레드가 부릅니다. */
  void SetCounts(const PpgParserCounts& counts);

  /** 포트가 닫힌 시각과 이유를 적습니다. 처음 한 번만 적고, Close 뒤에는 적지 않습니다. 장치 스레드가 부릅니다. */
  void MarkDisconnected(Timestamp at, const std::string& why);

  /** 장치 정보 응답의 데이터를 적습니다. Close 뒤에는 적지 않습니다. 장치 스레드가 부릅니다. */
  void SetDeviceInfo(const std::string& info);

 private:
  mutable std::mutex mutex_;
  PpgRecord record_;
  bool closed_ = false;
};

/** 장치를 확인하기 전 정보 요청을 보내는 간격입니다. 기존 앱과 같습니다. */
inline constexpr int64_t kPpgInfoIntervalUs = 1'000'000;

/**
 * 포트를 연 뒤 이 시간 동안 장치가 답하지 않으면 준비 중(PpgState::kPreparing)으로 보입니다. 기존 앱과 같습니다.
 *
 * @note 기존 앱 주석의 실기 기록으로 준비된 장치는 정보 요청에 수십 ms 안에 답합니다.
 */
inline constexpr int64_t kPpgPreparingAfterUs = 1'500'000;

/**
 * 포트를 연 뒤 이 시간 동안 장치가 답하지 않으면 PPG 장치를 찾지 못한 것으로 보고 포트를 닫습니다. 기존 앱과 같습니다.
 *
 * @note 전원을 켠 장치는 초기화를 끝내야 답합니다. 실기(2026-10-19)에서 약 42초, 기존 앱 주석으로 35~60초입니다.
 */
inline constexpr int64_t kPpgFindTimeoutUs = 90'000'000;

/** PPG 장치의 연결 상태입니다. 장치 스레드가 정합니다. */
enum class PpgState : uint8_t {
  kOff,        // 포트를 열지 않음
  kOpening,    // 포트를 여는 중
  kChecking,   // 포트를 열고 정보 요청을 보내며 장치가 답하기를 기다림(PPG 장치 확인). 연 뒤 kPpgPreparingAfterUs 전
  kPreparing,  // 장치 확인 중 연 뒤 kPpgPreparingAfterUs 넘게 답이 없음. 전원을 켠 직후 장치는 초기화를 끝낸 뒤 답함
  kWaiting,    // 장치를 확인했고 측정 데이터를 아직 받지 못함
  kStreaming,  // 측정 데이터가 오는 중
  kStalled,    // 측정 데이터가 오다가 PpgLink::kStaleUs 넘게 오지 않음
  kFailed,     // 포트를 열지 못했거나 읽기가 실패해 닫음(problem에 이유)
};

/** 화면에 보이는 PPG 장치 상태입니다. 장치 스레드가 바뀔 때만 보냅니다(PpgSensor::Callbacks::state). */
struct PpgSnapshot {
  PpgState state = PpgState::kOff;
  /** 열었거나 여는 포트 이름입니다. kOff이면 빈 문자열입니다. */
  std::string port;
  /** PpgReading::heart_rate와 같습니다(마지막 0이 아닌 값을 3초 동안). */
  int32_t heart_rate = 0;
  int32_t spo2 = 0;
  uint8_t measure_error = 0;
  /** 장치가 보낸 오류 메시지 값입니다. 다시 시작하면 0입니다. */
  uint8_t device_error = 0;
  /** kFailed의 이유나 명령 실패 이유입니다. 없으면 빈 문자열입니다. */
  std::string problem;

  friend bool operator==(const PpgSnapshot&, const PpgSnapshot&) = default;
};

/**
 * 직렬 포트 하나로 PPG 장치와 주고받는 일을 한 스레드에서 합니다. 장치 스레드(PpgSensor)가 가지고 돌립니다.
 *
 * 포트, 해석기, 상태, 화면 파형용 최근 표본, 붙인 촬영 기록을 이 객체 하나가 가집니다. 잠금이 없으므로 한 스레드에서만 부르고,
 * 다른 스레드는 Port()의 WakeRead만 부릅니다.
 *
 * @note 실험 노트북(Intel xHCI)의 USB 포트에 변환기를 바로 꽂으면 측정 데이터가 0.2~0.7초만 오고 멈춥니다. USB 허브를
 *       거치면 정상입니다(2026-10-19~20 실측). 표본이 멈추면 Snapshot().state가 kStalled가 됩니다.
 */
class PpgLink {
 public:
  /** 마지막 측정 데이터에서 이 시간이 지나면 표본이 멈춘 것으로 봅니다. 장치는 약 48ms마다 보냅니다(실측). */
  static constexpr int64_t kStaleUs = 500'000;

  /** 마지막 0이 아닌 심박수를 보이는 시간입니다. 박동이 0.4~1.5초(40~150bpm)마다 오므로 그보다 깁니다. */
  static constexpr int64_t kHeartRateHoldUs = 3'000'000;

  /**
   * 이웃한 측정 데이터 사이에서 PC 수신 시각이 장치 시각보다 이 시간 넘게 더 흐르면 수신 빈틈으로 보고 시계 구간을 나눕니다.
   *
   * @note 허브를 거친 정상 수신에서 측정 데이터 간격은 최대 64~71ms(한 번 102.8ms)로, 장치 시각(약 48ms)보다 더 벌어진
   *       시간은 대부분 약 23ms 이하였습니다. 중지 직후 다시 시작하면 13번 중 11번은 측정 데이터 1~2개가 먼저 온 뒤
   *       143~176ms 동안 오지 않아 95~128ms 더 벌어졌습니다(2026-10-20 실측). 정상 수신을 잘못 나눠도 구간마다 오프셋만
   *       다시 맞추므로 오차는 수신 지연의 흔들림 정도입니다.
   */
  static constexpr int64_t kReceiveGapUs = 50'000;

  /** 화면 파형용으로 보관하는 최근 표본 수입니다(500Hz로 10초). */
  static constexpr size_t kRecentCapacity = 5000;

  /** @param clock PC 시각(마이크로초). 비우면 steady_clock입니다. 촬영 파이프라인과 같은 시계를 넘깁니다 */
  explicit PpgLink(std::function<int64_t()> clock = {});

  /** Close와 같습니다. */
  ~PpgLink();
  PpgLink(const PpgLink&) = delete;
  PpgLink& operator=(const PpgLink&) = delete;

  /**
   * 포트를 엽니다. 열려 있던 포트는 먼저 닫습니다. 아직 명령은 보내지 않습니다. 장치 확인(Found)을 처음부터 다시 합니다.
   *
   * @return 열었으면 true. 열지 못하면 false이고 error에 이유를 씁니다
   */
  bool Open(const SerialPortOpener& opener, const std::string& port, std::string* error);

  /**
   * 장치 정보 요청을 보냅니다. 장치를 확인하기 전이면 kPpgInfoIntervalUs마다 보내고, 정보 응답이 아닌 메시지로 장치를
   * 확인했으면 한 번만 더 보냅니다. 장치 정보를 받았으면 보내지 않습니다.
   *
   * @note 정보 응답이 아닌 메시지로 확인하는 경우는 앞 연결이 장치를 측정 중으로 남긴 때입니다. 측정 중인 장치가 정보 요청에
   *       답하는지는 확인하지 않았습니다.
   */
  void AskDeviceInfo();

  /**
   * 포트를 연 뒤 장치가 보낸 메시지(정보, 상태, 오류, 측정 데이터)를 하나라도 받았으면 true입니다(PPG 장치 확인).
   *
   * @note 전원을 켠 장치는 초기화를 끝내면 묻지 않아도 장치 정보와 상태 'o'를 보냅니다. 실기(2026-10-19)에서 전원을 켜고 약
   *       42초 뒤였습니다.
   */
  bool Found() const { return found_; }

  /** 시작 명령을 보낸 뒤 닫지 않았으면 true입니다. */
  bool Started() const { return started_; }

  /** 촬영 기록을 붙여 두었으면 true입니다. */
  bool Attached() const { return record_ != nullptr; }

  /** 포트를 연 뒤 흐른 시간(마이크로초)입니다. 포트가 닫혀 있으면 0입니다. */
  int64_t OpenUs() const;

  /**
   * 장치 정보 응답의 데이터입니다. ASCII 15바이트로 MP-SDK 버전 6자리, HRV 기준 6자리, APG 기준 3자리입니다(예: 실기
   * "100002280280320"은 MP-SDK 1.0.0.0.0.2, HRV 기준 2.8.0.2.8.0, APG 기준 3.2.0). 받지 못했으면 빈 문자열입니다.
   */
  const std::string& DeviceInfo() const { return device_info_; }

  /**
   * 시작했으면 중지 명령을 보내고 포트를 닫습니다. 촬영 기록을 붙여 두었으면 끊긴 시각을 적습니다.
   *
   * @param why 촬영 기록에 적을 이유
   * @note 드라이버가 멈추면 이 함수가 돌아오지 않을 수 있습니다. 그래서 장치 스레드에서만 부릅니다.
   */
  void Close(const std::string& why = "PPG 연결을 끊었습니다");

  /**
   * 중지 명령 뒤 시작 명령을 보냅니다. 표본 번호를 0부터 다시 셉니다.
   *
   * @param error 실패하면 이유를 씁니다
   * @param request 시작 명령 값. 기본값은 미리보기 60초, 측정 60초, 센서 'P'입니다. 장치는 미리보기 동안에도 파형을 보내고,
   *        PPG 안정 대기(kPpgSettleUs)와 녹화 30초가 미리보기 60초 안에 들어갑니다
   * @return 두 명령을 보냈으면 true. 쓰기에 실패하면 false이고 error와 Snapshot().problem에 이유를 씁니다
   * @note 앞 연결이나 다른 프로그램이 장치를 측정 중으로 남기면 중지 없이 보낸 시작 명령은 오류 's'로 거부되고 장치는 앞 측정을
   *       이어갑니다(2026-10-20 실측). 장치는 측정 중이었으면 상태 'u'로 답합니다.
   */
  bool Restart(std::string* error = nullptr, const PpgStartRequest& request = {});

  /**
   * 받은 바이트를 최대 timeout_ms 기다려 읽고 해석합니다. 다른 스레드가 WakeRead를 부르면 일찍 돌아옵니다.
   *
   * @return 포트가 열려 있으면 true. 읽기가 실패하면 포트를 닫고 촬영 기록에 끊긴 시각을 적은 뒤 false입니다
   */
  bool Pump(int32_t timeout_ms);

  /**
   * 시작한 뒤 촬영 기록을 붙이지 않았고 PpgNeedsRestart가 true이면 다시 시작합니다.
   *
   * @return 다시 시작했으면 true
   */
  bool RestartIfStopped();

  /** 지금부터 받는 표본과 메시지를 record에 더합니다. 붙어 있던 기록은 뗍니다. 받아 둔 장치 정보를 record에 적습니다. */
  void Attach(std::shared_ptr<PpgShotRecord> record);

  /** 붙인 촬영 기록을 뗍니다. */
  void Detach();

  /** 포트가 열려 있으면 true입니다. */
  bool IsOpen() const;

  /** 연 포트입니다. 다른 스레드가 WakeRead를 부를 때 씁니다. 연 적이 없거나 Close 뒤이면 nullptr입니다. */
  SerialPort* Port() const { return port_.get(); }

  /** 열었거나 마지막으로 연 포트 이름입니다. */
  const std::string& PortName() const { return port_name_; }

  /** 포트를 열지 못했거나 읽기가 실패해 닫았을 때의 이유입니다. Open하면 지웁니다. 없으면 빈 문자열입니다. */
  const std::string& Failure() const { return failure_; }

  /** 지금 상태입니다. */
  PpgReading Reading() const;

  /**
   * 화면에 보일 상태입니다. 포트가 닫혀 있으면 kOff이고, 실패 여부는 부른 쪽이 Failure()로 정합니다. 장치를 확인하기 전이면
   * 연 뒤 시간에 따라 kChecking이나 kPreparing입니다.
   */
  PpgSnapshot Snapshot() const;

  /**
   * 최근에 받은 표본 값을 받은 순서대로 돌려줍니다. 화면 파형용입니다.
   *
   * 기록과 상관없이 시작 뒤 받은 측정 데이터의 표본을 kRecentCapacity개까지 보관하고, 시작하면 비웁니다.
   *
   * @param count 돌려받을 최대 표본 수. 가장 최근 표본부터 셉니다
   * @return 오래된 표본부터 최근 표본까지. 받은 표본이 없으면 빈 목록
   */
  std::vector<uint16_t> RecentValues(size_t count) const;

 private:
  void Handle(const PpgMessage& m);
  bool Send(const std::vector<uint8_t>& bytes, std::string* error);

  std::function<int64_t()> clock_;
  std::unique_ptr<SerialPort> port_;
  std::string port_name_;
  std::string failure_;
  PpgParser parser_;
  PpgReading reading_;
  bool started_ = false;
  bool found_ = false;
  bool asked_after_found_ = false;  // 장치를 확인한 뒤 정보 요청을 한 번 더 보냈음
  std::string device_info_;
  int64_t open_us_ = 0;             // 포트를 연 PC 시각
  int64_t info_us_ = 0;             // 마지막으로 정보 요청을 보낸 PC 시각. 보낸 적 없으면 0
  int64_t next_index_ = 0;          // 시작 뒤 다음 표본 번호
  int64_t start_us_ = 0;            // 마지막 시작의 PC 시각
  int64_t restart_us_ = 0;          // 마지막으로 다시 시작한 PC 시각
  int64_t last_measure_us_ = 0;
  int32_t last_heart_rate_ = 0;     // 마지막으로 받은 0이 아닌 심박수
  int64_t last_heart_rate_us_ = 0;  // 그 값을 받은 PC 시각
  std::deque<uint16_t> recent_;
  std::shared_ptr<PpgShotRecord> record_;
  PpgParserCounts record_counts_from_;
  std::vector<uint8_t> buffer_;
};

/** 화면 파형의 점 하나에 묶는 표본 수입니다. 500Hz 표본 5개를 평균해 100Hz 점 하나로 그립니다. */
inline constexpr size_t kPpgWaveGroup = 5;

/** 화면 파형에 쓰는 최근 표본 수입니다(500Hz로 5초). */
inline constexpr size_t kPpgWaveSamples = 2500;

/**
 * 화면 파형의 점을 만듭니다. 표본을 kPpgWaveGroup개씩 평균하고 1~99% 범위를 0~1로 옮깁니다.
 *
 * 손가락을 넣고 뺄 때 튀는 값 몇 개가 파형 높이를 정하지 않게 하기 위해 최솟값과 최댓값 대신 1%와 99% 값을 씁니다.
 *
 * @param values 오래된 표본부터
 * @return 오래된 점부터 0~1 값. 점이 둘이 안 되면 빈 목록
 */
std::vector<double> PpgWavePoints(std::span<const uint16_t> values);

/**
 * 앱이 도는 동안 하나 두는 PPG 장치 스레드입니다.
 *
 * 포트, 해석기, 상태는 장치 스레드 하나가 PpgLink로 가집니다. 화면 스레드는 명령을 대기열에 넣기만 하고, 장치 스레드는
 * 상태가 바뀔 때만 스냅숏을 보냅니다. 명령이 오면 SerialPort::WakeRead로 기다리던 읽기를 깨워 바로 처리합니다.
 * 촬영 기록은 촬영이 가지는 PpgShotRecord로 넘겨, 연결이 끊겨도 기록은 남고 촬영을 끝낼 때 장치 스레드를 기다리지 않습니다.
 *
 * 포트를 열면 정보 요청만 보내고, 장치가 답한 뒤에만 중지와 시작을 보냅니다(PPG 장치 확인, PpgLink::Found). 조명 보드처럼
 * PPG 장치가 아닌 포트를 골라도 그 포트에는 정보 요청만 나가고, kPpgFindTimeoutUs 뒤 포트를 닫고 kFailed로 알립니다.
 * 녹화 중(BeginShot부터 EndShot까지)에 장치를 확인하면 녹화를 바꾸지 않도록 EndShot에서 시작합니다.
 *
 * @note 소멸자는 끝내기 명령을 넣고 kQuitWaitUs만 기다립니다. 그 안에 끝나지 않으면 장치 스레드를 놓아 둡니다(detach).
 *       장치 스레드는 자기 상태만 shared_ptr로 참조하므로 이 객체가 사라진 뒤에도 안전합니다. 2026-10-20 기존 앱 실기처럼
 *       직렬 드라이버가 멈추면 쓰기와 닫기가 돌아오지 않아 창을 닫아도 앱이 끝나지 않았기 때문입니다.
 * @note 드라이버가 끝내지 않는 입출력이 있으면 Windows가 그 스레드를 정리하지 못해, 놓아 둔 뒤에도 프로세스와 COM 포트가
 *       남을 수 있습니다. 놓아 두기는 창 닫기와 앱 종료 흐름을 막지 않게 할 뿐입니다.
 */
class PpgSensor {
 public:
  /**
   * 장치 스레드가 부르는 함수들입니다. 장치 스레드에서 잠금을 쥔 채 부르므로 짧게 끝내고, 안에서 PpgSensor 함수를 부르지 않습니다.
   * 화면에 알릴 때는 화면 스레드로 넘기기만 합니다(Qt의 QueuedConnection).
   */
  struct Callbacks {
    /** 상태가 바뀔 때마다 부릅니다. 처음에 kOff를 한 번 부릅니다. */
    std::function<void(const PpgSnapshot&)> state;
    /** 표본이 오는 동안 kWaveIntervalUs마다 최근 5초 파형(PpgWavePoints)을, 표본이 멈추면 빈 목록을 한 번 부릅니다. */
    std::function<void(std::vector<double>)> wave;
    /** RefreshPorts의 결과입니다. */
    std::function<void(std::vector<SerialPortInfo>)> ports;
  };

  /** 소멸자가 장치 스레드가 끝나기를 기다리는 최대 시간입니다. */
  static constexpr int64_t kQuitWaitUs = 1'000'000;

  /** 포트가 열려 있을 때 한 번에 읽기를 기다리는 최대 시간입니다. 명령은 WakeRead로 이보다 일찍 처리합니다. */
  static constexpr int32_t kReadTimeoutMs = 100;

  /** 화면 파형을 보내는 간격입니다. */
  static constexpr int64_t kWaveIntervalUs = 100'000;

  /**
   * 장치 스레드를 시작합니다. 포트는 아직 열지 않습니다.
   *
   * @param opener 포트를 여는 함수(OpenSerialPort, 가짜 포트)
   * @param list_ports 포트 목록을 읽는 함수. 비우면 RefreshPorts가 아무것도 하지 않습니다
   * @param callbacks 장치 스레드가 부르는 함수들
   * @param clock PC 시각(마이크로초). 비우면 steady_clock입니다
   */
  PpgSensor(SerialPortOpener opener, std::function<std::vector<SerialPortInfo>()> list_ports, Callbacks callbacks,
            std::function<int64_t()> clock = {});

  /** 콜백을 지우고 끝내기 명령(장치 중지, 포트 닫기)을 넣은 뒤 kQuitWaitUs만 기다립니다. */
  ~PpgSensor();
  PpgSensor(const PpgSensor&) = delete;
  PpgSensor& operator=(const PpgSensor&) = delete;

  /** 포트를 열고 장치를 확인합니다. 장치가 답하면 시작합니다. 열려 있던 포트는 먼저 닫습니다. */
  void Connect(const std::string& port);

  /** 장치를 멈추고 포트를 닫습니다. */
  void Disconnect();

  /** 포트 목록을 읽어 Callbacks::ports로 보냅니다. */
  void RefreshPorts();

  /**
   * 장치를 다시 시작하고 record에 기록을 시작합니다. 포트가 닫혀 있으면 record에 끊긴 시각만 적습니다. 장치를 확인하기
   * 전이면 시작하지 않고 기록만 붙입니다.
   *
   * @note 미리보기 60초를 이 촬영에 쓰도록 다시 시작합니다. PPG 안정 대기(kPpgSettleUs)와 녹화 30초가 미리보기 60초 안에 들어갑니다.
   */
  void BeginShot(std::shared_ptr<PpgShotRecord> record);

  /**
   * 촬영 기록을 떼고 다음 촬영까지 파형이 오도록 장치를 다시 시작합니다. 장치를 확인하기 전이면 시작하지 않습니다.
   * 촬영 기록의 Close는 부른 쪽이 합니다.
   */
  void EndShot();

  /** 장치 스레드가 마지막으로 한 바퀴 돈 PC 시각입니다. 포트가 열려 있는 동안 kReadTimeoutMs마다 바뀝니다. */
  int64_t LastLoopUs() const;

  /** 아직 처리하지 않은 가장 오래된 명령을 넣은 PC 시각입니다. 없으면 0입니다. */
  int64_t PendingSinceUs() const;

  struct Shared;

 private:
  std::shared_ptr<Shared> shared_;
  std::thread thread_;
};

/**
 * 촬영 버튼에서 PPG 장치를 다시 시작한 뒤 RGB 녹화를 미루는 시간입니다(PPG 안정 대기).
 *
 * 다시 시작하면 원시값이 오른 뒤 몇 초 안에 한두 번 6,000~7,000씩 떨어지고, 그동안 장치 심박수에도 틀린 값이 섞입니다.
 * 계단이 생기지 않는 촬영도 있어 장치가 안정됐는지는 판정하지 않고 이 시간을 늘 기다립니다.
 *
 * @note 실기(2026-10-21~22, 한 사람, 장치 하나): 다시 시작한 37번 모두 마지막 계단이 9.6초 안에 있었고,
 *       계단이 생긴 19번의 마지막 계단은 평균 7.4초(3.0~9.6초)였습니다. 다른 사람과 손가락 위치는 재지 않아 넉넉히 15초로
 *       정했습니다.
 * @note 열화상을 쓰면 셔터 교정이 이 대기와 함께 끝나게 요청해 같은 시각에 녹화를 시작합니다. 교정이 길어지면 그만큼 늦게
 *       시작합니다(flow::Event, 설계 문서 표 10).
 */
inline constexpr int64_t kPpgSettleUs = 15'000'000;

/** 측정 데이터가 이 시간 넘게 오지 않으면 다시 시작할 때가 됩니다(PpgNeedsRestart). */
inline constexpr int64_t kPpgRestartQuietUs = 2'000'000;

/** 다시 시작한 뒤 이 시간이 지나기 전에는 다시 시작하지 않습니다. 전원을 켠 뒤 준비 중인 장치에 시작 명령을 쌓지 않기 위함입니다. */
inline constexpr int64_t kPpgRestartIntervalUs = 5'000'000;

/**
 * 촬영하지 않는 동안 PPG 장치를 다시 시작(중지 뒤 시작)해야 하는지 정합니다.
 *
 * 장치가 측정 구간에 들어갔거나(상태 's') 측정을 끝냈거나(상태 'p'), 측정 데이터가 kPpgRestartQuietUs 넘게 오지 않으면
 * 다시 시작합니다. 장치를 측정 구간에 두지 않고 미리보기를 이어가게 해서 화면 파형이 끊기지 않게 하기 위함입니다.
 *
 * @param reading PpgLink::Reading의 결과
 * @param since_restart_us 마지막으로 다시 시작한 뒤 흐른 시간
 * @return 다시 시작해야 하면 true. 포트가 닫혔거나 since_restart_us가 kPpgRestartIntervalUs보다 짧으면 false
 * @note 손가락 없이 장치 미리보기가 끝나면 장치는 상태 's'나 'p' 없이 오류 메시지 'f'를 보내고 측정 데이터를 멈춥니다.
 *       시작 명령을 다시 받기 전까지 그대로 멈춰 있고, 받으면 바로 미리보기로 돌아갑니다(2026-10-21 실측, 미리보기 10초로
 *       시작해 80초 동안 기다림). 측정 구간에서 손가락이 빠지면 오류 'm'으로 측정을 끝낸다고 사양서가 적습니다.
 */
bool PpgNeedsRestart(const PpgReading& reading, int64_t since_restart_us);

/** 촬영 폴더에 쓰는 PPG 파일 이름의 뒷부분입니다. 파일 이름은 `<촬영 이름>_ppg.csv`입니다. */
inline constexpr char kPpgFileSuffix[] = "_ppg.csv";

/**
 * PPG 기록을 CSV로 씁니다.
 *
 * 첫 줄은 열 이름 `common_time_us,value,measure_error,heart_rate,spo2`이고, 그 뒤 표본마다 한 줄입니다.
 * common_time_us는 공통 시각(PC 시계, 마이크로초)이라 RGB 사이드카의 common_time_us와 바로 견줍니다.
 * measure_error는 0 정상, 102('f') 손가락 빠짐, 115('s') 신호 나쁨, 112('p') 센서 연결 안 됨입니다.
 *
 * @param path 파일 경로(UTF-8)
 * @param record PpgShotRecord::Finish의 결과
 * @param error 실패하면 이유를 씁니다
 * @return 썼으면 true. 표본이 없어도 열 이름 줄만 있는 파일을 씁니다
 */
bool WritePpgCsv(const std::string& path, const PpgRecord& record, std::string* error = nullptr);

/** ReadPpgCsv의 결과입니다. 열마다 같은 길이입니다. */
struct PpgCsv {
  std::vector<int64_t> common_time_us;
  std::vector<int32_t> value;
  std::vector<int32_t> measure_error;
  std::vector<int32_t> heart_rate;
  std::vector<int32_t> spo2;
};

/**
 * WritePpgCsv가 쓴 파일을 읽습니다.
 *
 * @return 읽은 값. 파일이 없거나 열 이름 줄이 다르거나 숫자가 아닌 줄이 있으면 nullopt이고 error에 이유를 씁니다
 */
std::optional<PpgCsv> ReadPpgCsv(const std::string& path, std::string* error = nullptr);

/** 촬영 폴더에 쓰는 PPG 사이드카 이름의 뒷부분입니다. 파일 이름은 `<촬영 이름>_ppg.json`입니다. */
inline constexpr char kPpgSidecarSuffix[] = "_ppg.json";

/**
 * PPG 기록의 사이드카를 씁니다(record::BuildPpgSidecarJson).
 *
 * 누락은 공통 시각의 표본 간격이 500Hz 주기의 1.5배를 넘는 자리이고(timing::FindFrameGaps), 수신 빈틈이 여기에 남습니다.
 * 측정 오류 구간은 기록 전체에서 찾습니다(record::FindPpgErrorSpans). 장치 정보를 받지 못했으면 device_info는 null입니다.
 *
 * @param path 파일 경로(UTF-8)
 * @param record PpgShotRecord::Finish의 결과
 * @param samples_file 같은 기록을 쓴 PPG 파일 이름(폴더 없이)
 * @param port 장치를 연 포트 이름
 * @param rgb_window RGB 녹화 구간 안 측정 오류 집계(record::SummarizePpgErrors). RGB 사이드카를 읽지 못했으면 nullopt
 * @param error 실패하면 이유를 씁니다
 * @return 썼으면 true
 */
bool WritePpgSidecar(const std::string& path, const PpgRecord& record, const std::string& samples_file,
                     const std::string& port, const std::optional<hwlab::record::PpgErrorWindow>& rgb_window,
                     std::string* error = nullptr);

/**
 * RGB 녹화 구간 안 측정 오류 값 하나의 양을 화면에 보일 문장으로 바꿉니다.
 *
 * @param share 값 하나의 집계(record::SummarizePpgErrors)
 * @param window_samples 같은 구간 안 표본 수. 비율의 분모입니다
 * @return 예: "신호 나쁨 4.3초(14%)". 여러 번 끊겨 나왔으면 횟수를 붙입니다: "손가락 빠짐 0.5초(2%, 3번)"
 */
std::string PpgErrorShareText(const record::PpgErrorShare& share, int64_t window_samples);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_PPG_SENSOR_H_
