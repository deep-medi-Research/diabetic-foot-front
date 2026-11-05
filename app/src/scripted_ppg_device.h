#ifndef HWLAB_APP_SCRIPTED_PPG_DEVICE_H_
#define HWLAB_APP_SCRIPTED_PPG_DEVICE_H_

#include <atomic>
#include <memory>
#include <thread>

#include "hwlab/platform/fake_ppg_device.h"
#include "hwlab/platform/fake_serial_port.h"

/**
 * --fake 실행에서 가짜 PPG 장치를 실제 시간으로 돌립니다.
 *
 * 포트에 명령이 쓰이거나 다음 메시지 시각이 될 때까지 기다렸다가(FakeSerialLine::WaitForWrites) 명령에 답하고, 받아들인 시작
 * 명령 뒤 측정 데이터(표본 24개)를 장치 시계가 0.5% 느린 간격(48.24ms)으로 냅니다.
 * 시작 뒤 4초 동안 심박수와 SpO2는 0입니다. 시작 뒤 16초부터 2초 동안 측정 오류 's'(신호 나쁨)를 붙입니다. 미리보기 초가
 * 지나면 상태 's', 측정 초까지 지나면 상태 'p'를 내고 멈춥니다.
 *
 * @note 실측(2026-10-20)을 따릅니다. 전원을 켠 뒤 처음 시작하면 첫 측정 데이터가 약 190ms 뒤에 옵니다. 중지 직후 다시
 *       시작하면 측정 데이터 하나가 약 30ms 뒤에 먼저 오고, 다음 데이터는 평소보다 128ms 늦게 옵니다.
 * @note 실기 촬영에서 녹화 중 측정 오류 's'가 약 4.3초 이어진 적이 있습니다(2026-10-21). 촬영 버튼을 누르면 장치를 다시
 *       시작하고 PPG 안정 대기(15초) 뒤에 녹화하므로 16~18초는 --fake 촬영의 녹화 구간 안에 듭니다.
 * @note 기다림이 끝나는 시각은 OS 타이머 해상도(Windows 기본 약 15.6ms)만큼 늦을 수 있어, 위 지연과 간격도 그만큼 흔들립니다.
 */
class ScriptedPpgDevice {
 public:
  /** @param line 가짜 포트의 장치 쪽. 앱의 PpgSensor가 같은 회선으로 엽니다 */
  explicit ScriptedPpgDevice(std::shared_ptr<hwlab::platform::FakeSerialLine> line);
  ~ScriptedPpgDevice();
  ScriptedPpgDevice(const ScriptedPpgDevice&) = delete;
  ScriptedPpgDevice& operator=(const ScriptedPpgDevice&) = delete;

 private:
  void Run();

  std::shared_ptr<hwlab::platform::FakeSerialLine> line_;
  hwlab::platform::FakePpgDevice device_;
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

#endif  // HWLAB_APP_SCRIPTED_PPG_DEVICE_H_
