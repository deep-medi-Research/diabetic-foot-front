#ifndef HWLAB_PLATFORM_THERMAL_STREAM_H_
#define HWLAB_PLATFORM_THERMAL_STREAM_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "hwlab/camera_settings.h"
#include "hwlab/platform/capture.h"
#include "hwlab/platform/thermal_camera.h"
#include "hwlab/record/sidecar.h"

namespace hwlab::platform {

/** 열화상 수신 스레드가 버퍼에 복사한 프레임 하나입니다. RGB 수신 스레드의 촬영 흐름이 쓸지 정합니다. */
struct ThermalArrival {
  int32_t slot = -1;           // 화소를 복사한 버퍼. 빈 버퍼가 없었으면 -1이고, 녹화 중이면 저장 누락입니다
  int32_t width = 0;
  int32_t height = 0;
  record::FrameRecord record;  // 번호와 시각. 밝기는 쓰기 스레드가 채웁니다
  int64_t median_interval_us = 0;  // 이 프레임까지 최근 31장의 수신 간격 중앙값. 기록 주기 판정에 씁니다
  std::optional<CameraTemperature> camera_temperature;  // 이 프레임과 함께 읽은 카메라 온도. 카메라가 주지 않으면 nullopt
};

/** 녹화 한 번의 열화상 파일 설정입니다. */
struct ThermalRecording {
  std::string raw_path;
  std::string sidecar_path;
  std::string preview_path;        // 비어 있으면 확인용 영상을 쓰지 않습니다
  int64_t preview_max_frames = 0;  // 확인용 AVI 색인 자리. 넘으면 확인용 영상만 더 쓰지 않습니다
  int64_t record_period_us = 0;    // 기록 주기(timing::RecordRateRule). 0이면 받은 프레임을 모두 기록합니다
  SettingMode mode = SettingMode::kAuto;
};

/**
 * 열화상 카메라 하나의 수신 스레드와 쓰기 스레드입니다. Capture 안에서만 씁니다.
 *
 * 수신 스레드는 카메라를 열고, 프레임을 버퍼에 복사해 Drain으로 넘기고, 끊김(kLost)이면 닫고 0.5초마다 다시 엽니다.
 * 쓸지 말지는 RGB 수신 스레드가 촬영 흐름으로 정해 Write 또는 Discard를 부릅니다.
 * 쓰기 스레드는 기록 주기(timing::AdvanceRecordRate)로 기록할 프레임을 고르고 원시값 파일, 확인용 영상, 열화상 사이드카를
 * 씁니다. 미리보기에는 고르기 전의 프레임을 모두 보냅니다.
 * 카메라가 카메라 온도를 주면 원시값 파일 이름의 .raw16을 _camera_temp.csv로 바꾼 카메라 온도 파일에, 원시값 파일에 쓴
 * 프레임마다 한 줄을 씁니다. 열은 index, device_time_us, host_time_us(열화상 사이드카 per_frame의 같은 프레임 값), fpa,
 * shutter_raw, shutter_c입니다. 카메라 온도를 처음 받기 전에 쓴 프레임과 값이 없는 프레임은 값 칸을 비웁니다.
 * 녹화 동안 값을 한 번도 받지 않으면 파일을 만들지 않습니다.
 */
class ThermalStream {
 public:
  /**
   * @param camera 열지 않은 열화상 카메라
   * @param buffer_frames 버퍼 수
   * @param on_sidecar_written 열화상 사이드카를 쓴 뒤 쓰기 스레드에서 부릅니다(실패해도 부릅니다)
   */
  ThermalStream(ThermalCamera& camera, int32_t buffer_frames, std::function<void()> on_sidecar_written);
  ~ThermalStream();
  ThermalStream(const ThermalStream&) = delete;
  ThermalStream& operator=(const ThermalStream&) = delete;

  /** Start 전에 부릅니다. sink는 수신 스레드에서 불립니다. */
  void SetPreviewSink(int32_t every_frames, std::function<void(const FrameView&)> sink);

  /** 수신 스레드와 쓰기 스레드를 시작합니다. */
  void Start();

  /** 수신을 멈추고 카메라를 닫은 뒤, 이미 넣은 End까지 처리하고 쓰기 스레드를 멈춥니다. */
  void Stop();

  /** 지금까지 받은 프레임을 꺼냅니다. RGB 수신 스레드에서 부릅니다. */
  std::vector<ThermalArrival> Drain();

  /** 녹화를 시작합니다. 파일은 첫 프레임을 쓸 때 프레임 크기로 엽니다. */
  void Begin(const ThermalRecording& recording);

  /** Drain으로 받은 녹화 구간의 프레임을 쓰기 스레드에 넘깁니다. 기록 주기로 고른 프레임만 씁니다. Begin과 End 사이에서만 부릅니다. */
  void Write(const ThermalArrival& arrival);

  /** Drain으로 받은 프레임을 쓰지 않고 버퍼를 돌려줍니다. */
  void Discard(const ThermalArrival& arrival);

  /**
   * 녹화를 끝내고 파일을 닫고 열화상 사이드카를 씁니다.
   *
   * @param rgb_first_host_time RGB 녹화의 첫 프레임의 PC 수신 시각
   * @param rgb_last_host_time RGB 녹화의 마지막 프레임의 PC 수신 시각. 열화상이 그보다 일찍 끊겼으면 끝까지를 누락으로 남깁니다
   * @note 두 시각 사이 공통 시각에 기록한 장 수를 사이드카의 written_in_rgb_window에 남깁니다.
   */
  void End(Timestamp rgb_first_host_time, Timestamp rgb_last_host_time);

  /** 상태의 사본입니다. 어느 스레드에서 불러도 됩니다. */
  ThermalStatus Status() const;

  /** 카메라의 렌즈 셰이딩 보정 상태를 다시 읽어 상태에 넣습니다. 보정한 뒤 제어 스레드가 부릅니다. */
  void RefreshLensShading();

 private:
  struct Message {
    enum class Kind : uint8_t { kBegin, kFrame, kDropped, kEnd, kQuit };
    Kind kind = Kind::kFrame;
    ThermalArrival arrival;      // kFrame, kDropped
    ThermalRecording recording;  // kBegin
    Timestamp rgb_first_host_time;  // kEnd
    Timestamp rgb_last_host_time;   // kEnd
  };

  void ReceiveLoop();
  void WriteLoop();
  void Post(Message message);
  bool WaitStopping(std::chrono::milliseconds timeout);
  void ReleaseSlot(int32_t slot);

  ThermalCamera& camera_;
  const int32_t buffer_frames_;
  std::function<void()> on_sidecar_written_;
  int32_t preview_every_ = 0;
  std::function<void(const FrameView&)> preview_sink_;

  std::thread receiver_;
  std::thread writer_;
  std::mutex stop_mutex_;
  std::condition_variable stop_cv_;
  bool stopping_ = false;  // stop_mutex_
  bool started_ = false;

  // 버퍼: 비어 있는 버퍼는 수신 스레드만, 넘긴 버퍼는 쓰기 스레드만 만집니다. 크기는 수신 스레드가 프레임에 맞춥니다.
  std::vector<std::vector<uint8_t>> buffers_;
  std::mutex slot_mutex_;
  std::vector<int32_t> free_slots_;

  std::mutex arrival_mutex_;
  std::vector<ThermalArrival> arrivals_;

  std::mutex message_mutex_;
  std::condition_variable message_cv_;
  std::deque<Message> messages_;

  mutable std::mutex status_mutex_;
  ThermalStatus status_;
  std::string device_name_;       // 마지막으로 연 장치 이름(status_mutex_). DeviceName은 수신 스레드에서만 부릅니다
  int32_t max_queue_frames_ = 0;  // 이번 녹화 중 쓰기 스레드가 아직 처리하지 못한 버퍼 수의 최댓값(status_mutex_)
};

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_THERMAL_STREAM_H_
