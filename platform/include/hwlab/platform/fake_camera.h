#ifndef HWLAB_PLATFORM_FAKE_CAMERA_H_
#define HWLAB_PLATFORM_FAKE_CAMERA_H_

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/platform/rgb_camera.h"

namespace hwlab::platform {

/** 가짜 카메라의 시나리오입니다. 끊김, 재연결, 누락, fps 저하, 장치 시계의 오프셋과 속도 차이를 재현합니다. */
struct FakeCameraScript {
  int32_t width = 64;
  int32_t height = 32;
  CameraState initial;               // 열었을 때의 설정
  std::map<int64_t, int32_t> skips;  // 프레임 번호 앞에서 카메라가 빠뜨릴 장 수
  int64_t disconnect_after = -1;     // 이만큼 보낸 뒤 연결 끊김. -1이면 끊기지 않음
  int32_t reopen_failures = -1;      // 끊긴 뒤 Reopen이 실패하는 횟수. -1이면 다시 연결되지 않음
  CameraState factory;               // 다시 연결되면 돌아가는 설정(실제 카메라의 공장 기본값)
  int32_t wall_period_us = 1000;     // 실제로 기다리는 간격. 장치 시각과 따로 두어 테스트를 빨리 끝냅니다
  int64_t device_period_us = 0;      // 장치 시각 간격. 0이면 SDK fps를 따릅니다. 카메라가 fps를 낮춘 경우를 재현합니다
  // true이면 PC 시계(steady_clock, 마이크로초)로 촬영 시각과 수신 시각을 매깁니다. 프레임 간격은 wall_period_us이고
  // device_period_us와 SDK fps는 쓰지 않습니다. FakeThermalScript::pc_clock과 함께 켜면 두 가짜 장치가 같은 시간축을 씁니다
  bool pc_clock = false;
  int64_t device_offset_us = 0;  // 장치 시각 = device_offset_us + device_rate * 촬영 시각(반올림)
  double device_rate = 1.0;      // 장치 시계가 촬영 시각 1마이크로초 동안 세는 마이크로초
  // pc_clock일 때 수신 시각 = 촬영 시각 + host_latency_us + (0 이상 host_jitter_us 이하). OS 대기 시간과 관계없이
  // 같은 시나리오에는 같은 수신 지연이 나오게 실제로 돌아온 시각 대신 이 값을 씁니다
  int64_t host_latency_us = 0;
  int64_t host_jitter_us = 0;
  uint32_t host_jitter_seed = 1;
  // 이만큼 보낸 뒤 ReadFrame이 Interrupt 전까지 돌아오지 않습니다. -1이면 멈추지 않음. 프레임이 오지 않는 장치를 재현합니다
  int64_t stall_after = -1;
  bool varied_pixels = false;  // true이면 화소 바이트마다 다른 값(PatternByte)을 냅니다. 영역 자르기를 바이트 단위로 확인할 때 씁니다
};

/**
 * 장비 없이 촬영 파이프라인을 시험하는 카메라입니다.
 *
 * 프레임 i의 모든 화소 Y는 (i % 200) + 20이고 U와 V는 128입니다(varied_pixels이면 PatternByte). 설정 명령은 실제 카메라처럼
 * 상태에 반영합니다.
 *
 * 촬영 시각은 pc_clock이 false이면 0부터 세는 가상 시각이고, true이면 PC 시계로 프레임을 내보내기로 정한 시각입니다.
 * view.time은 촬영 시각을 device_offset_us와 device_rate로 옮긴 장치 시각입니다. host_time은 pc_clock이 false이면
 * 촬영 시각과 같고, true이면 촬영 시각에 시나리오의 수신 지연을 더한 PC 시각입니다.
 */
class FakeRgbCamera final : public RgbCamera {
 public:
  explicit FakeRgbCamera(FakeCameraScript script);

  std::string DeviceName() const override { return "FakeCamera"; }
  std::optional<ReceivedFrame> ReadFrame() override;
  bool Reopen() override;
  CameraState ReadState() override;
  CameraState ReadStoredState() override;
  bool Execute(const flow::SettingCommand& command) override;
  void Interrupt() override;

  /**
   * varied_pixels일 때 프레임의 바이트 값입니다.
   *
   * @param index 프레임 번호(view.index)
   * @param byte_x 행 안의 바이트 위치(UYVY이므로 화소 x의 바이트는 2x와 2x+1)
   * @param y 행
   * @return 위치와 프레임마다 다른 값
   */
  static uint8_t PatternByte(int64_t index, int32_t byte_x, int32_t y);

  /** 지금까지 실행한 설정 명령입니다. 테스트에서 순서를 확인할 때 씁니다. */
  std::vector<flow::SettingCommand> ExecutedCommands();

  /** 지금 설정입니다. ReadState와 달리 자동 노출 중에도 노출값을 감추지 않습니다. */
  CameraState State();

  /** Interrupt가 불린 횟수입니다. */
  int32_t Interrupts();

 private:
  FakeCameraScript script_;
  std::vector<uint8_t> pixels_;
  std::mutex mutex_;
  CameraState state_;
  std::vector<flow::SettingCommand> executed_;
  int64_t sent_ = 0;
  bool connected_ = true;
  int32_t reopen_attempts_ = 0;
  std::chrono::steady_clock::time_point next_due_{};  // 다음 프레임을 내보낼 시각
  int64_t device_frame_ = 0;  // 빠뜨린 장까지 센 카메라 프레임 번호
  uint32_t jitter_state_ = 1;
  std::condition_variable interrupted_cv_;
  bool interrupted_ = false;
  int32_t interrupts_ = 0;
};

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_FAKE_CAMERA_H_
