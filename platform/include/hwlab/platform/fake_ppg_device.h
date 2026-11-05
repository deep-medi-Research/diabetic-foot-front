#ifndef HWLAB_PLATFORM_FAKE_PPG_DEVICE_H_
#define HWLAB_PLATFORM_FAKE_PPG_DEVICE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "hwlab/platform/fake_serial_port.h"
#include "hwlab/platform/ppg_wire.h"

namespace hwlab::platform {

/**
 * 장비 없이 PpgSensor를 시험하는 PPG 장치입니다. FakeSerialLine의 장치 쪽에서 명령을 읽고 메시지를 넣습니다.
 *
 * 시간을 스스로 흘려보내지 않습니다. 부르는 쪽이 HandleCommands, EmitMeasurement, EmitStatus를 불러 한 단계씩 진행합니다.
 * 맥파는 표본 번호로 정해지는 사인파라 같은 호출 순서에는 같은 바이트를 냅니다.
 *
 * @note 한 스레드에서만 부릅니다.
 */
class FakePpgDevice {
 public:
  /** @param line 포트의 장치 쪽. PpgSensor가 같은 회선으로 엽니다 */
  explicit FakePpgDevice(std::shared_ptr<FakeSerialLine> line);

  /**
   * 지난 호출 뒤 포트에 쓴 명령에 답합니다.
   *
   * 측정 중이 아닐 때 시작 명령을 받으면 상태 'v'를 넣고 표본 번호를 0부터 셉니다. 측정 중에 시작 명령을 받으면 오류 's'만
   * 넣고 측정을 이어갑니다. 측정 중에 중지 명령을 받으면 상태 'u'를 넣습니다. 정보 요청에는 장치 정보 15바이트를 넣습니다.
   * 초기화 중(StartBooting부터 FinishBooting까지)에는 어떤 명령에도 답하지 않습니다.
   *
   * @return 이번에 받은 명령. 받은 순서입니다
   * @note 측정 중 시작 명령의 오류 's'는 2026-10-20 실측(중지 없이 시작하면 8ms 뒤 오류 's', 측정 데이터는 이어짐)입니다.
   */
  std::vector<PpgMessage> HandleCommands();

  /**
   * 측정 데이터 메시지 하나를 넣습니다. 측정 중이 아니어도 넣습니다.
   *
   * @param samples 이 메시지의 표본 수
   * @param heart_rate 장치 심박수(bpm). 계산 전이면 0
   * @param spo2 장치 SpO2(%). 계산 전이면 0
   * @param error 측정 오류 바이트(0 정상, 'f' 손가락 빠짐 등)
   */
  void EmitMeasurement(size_t samples, uint8_t heart_rate, uint8_t spo2, uint8_t error = 0);

  /** 상태 메시지 하나를 넣습니다. 'p'(장치 측정 끝)이면 측정을 끝낸 것으로 봅니다. */
  void EmitStatus(uint8_t status);

  /**
   * 오류 메시지 하나를 넣습니다. 'f'와 'm'이면 측정을 끝낸 것으로 봅니다.
   *
   * @note 손가락 없이 장치 미리보기가 끝나면 장치는 오류 'f'를 보내고 측정 데이터를 멈춥니다(2026-10-21 실측). 측정 구간에서
   *       손가락이 빠지면 오류 'm'으로 측정을 끝낸다고 사양서가 적습니다.
   */
  void EmitError(uint8_t error);

  /** 전원을 켠 직후처럼 초기화를 시작합니다. FinishBooting까지 HandleCommands는 명령을 읽기만 하고 답하지 않습니다. */
  void StartBooting();

  /**
   * 초기화를 끝냅니다. 묻지 않아도 장치 정보와 상태 'o'를 넣고, 이후 명령에 답합니다.
   *
   * @note 실기(2026-10-19)에서 전원을 켜고 약 42초 뒤 장치 정보와 상태 'o'가 왔습니다. 그 전에는 측정할 수 없습니다(사양서).
   */
  void FinishBooting();

  /** 받은 정보 요청 수입니다. 초기화 중에 받아 답하지 않은 요청도 셉니다. */
  int32_t InfoRequestCount() const { return info_request_count_; }

  /**
   * 표본 번호의 맥파 값입니다. 받은 표본과 비교할 때 씁니다.
   *
   * @param sample 시작 뒤 표본 번호
   * @return 2048을 중심으로 진폭 400, 1.2Hz(500Hz 기준) 사인파
   */
  static uint16_t WaveAt(int64_t sample);

  /** 시작 명령을 받은 뒤 중지 명령이나 'p'가 없었으면 true입니다. */
  bool Running() const { return running_; }

  /** 받아들인 시작 명령 수입니다. 측정 중에 받아 오류로 답한 시작 명령은 세지 않습니다. */
  int32_t StartCount() const { return start_count_; }

  /** 받은 중지 명령 수입니다. */
  int32_t StopCount() const { return stop_count_; }

  /** 마지막으로 받아들인 시작 명령의 데이터(미리보기 초, 측정 초, 센서, 성별, 나이, 결과 기준)입니다. */
  const std::vector<uint8_t>& LastStartData() const { return last_start_data_; }

 private:
  void Emit(uint8_t code, const std::vector<uint8_t>& data);
  void EmitInfo();

  std::shared_ptr<FakeSerialLine> line_;
  PpgParser commands_;
  size_t handled_bytes_ = 0;
  int64_t next_sample_ = 0;
  bool running_ = false;
  bool booting_ = false;
  int32_t start_count_ = 0;
  int32_t stop_count_ = 0;
  int32_t info_request_count_ = 0;
  std::vector<uint8_t> last_start_data_;
};

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_FAKE_PPG_DEVICE_H_
