#include "hwlab/platform/capture.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <future>
#include <mutex>
#include <thread>

#include "hwlab/pixel/brightness.h"
#include "hwlab/pixel/crop.h"
#include "hwlab/platform/frame_pool.h"
#include "hwlab/platform/shot_folder.h"
#include "hwlab/platform/video_writer.h"
#include "hwlab/record/sidecar.h"
#include "hwlab/timing/clock_map.h"
#include "hwlab/timing/frame_gap.h"
#include "thermal_stream.h"

namespace hwlab::platform {

namespace {

template <typename T>
class Inbox {
 public:
  void Post(const T& item) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      items_.push_back(item);
    }
    ready_.notify_one();
  }
  std::vector<T> Drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<T> out(items_.begin(), items_.end());
    items_.clear();
    return out;
  }
  bool WaitFor(T* item, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!ready_.wait_for(lock, timeout, [&] { return !items_.empty(); })) return false;
    *item = items_.front();
    items_.pop_front();
    return true;
  }

 private:
  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<T> items_;
};

// 실행한 설정 명령을 안정화를 다시 세는 사건으로 바꿉니다. fps와 잠금은 노출과 같이 다룹니다.
std::vector<flow::Event> EventsFor(const std::vector<flow::SettingCommand>& executed) {
  std::vector<flow::Event> events;
  for (const auto& c : executed) {
    SettingKind kind = SettingKind::kExposure;
    if (c.kind == flow::CommandKind::kGain) kind = SettingKind::kGain;
    if (c.kind == flow::CommandKind::kWhiteBalanceManual || c.kind == flow::CommandKind::kWhiteBalanceAuto) {
      kind = SettingKind::kWhiteBalance;
    }
    events.push_back({flow::EventKind::kSettingChanged, kind});
  }
  return events;
}

void CopyPacked(const FrameView& view, uint8_t* out) {
  const size_t row = static_cast<size_t>(view.width) * 2;
  for (int32_t y = 0; y < view.height; ++y) {
    std::memcpy(out + row * static_cast<size_t>(y),
                view.plane[0] + static_cast<ptrdiff_t>(y) * view.row_stride[0], row);
  }
}

WriteMessage MessageOf(WriteMessage::Kind kind) {
  WriteMessage m;
  m.kind = kind;
  return m;
}

std::string FileNameOf(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// 가장 큰 주기 성분이 흔들림의 절반 이상일 때만 주기로 남깁니다. periodic_fraction은 그대로 남겨 기준을 바꿔 다시 판정할 수 있습니다.
constexpr double kFlickerMinPeriodicFraction = 0.5;

// 종료할 때 수신 스레드를 기다리는 시간입니다. 재연결 대기(0.5초)와 15fps 프레임 간격보다 넉넉히 깁니다.
constexpr std::chrono::milliseconds kReceiverStopWait{2000};

}  // namespace

struct Capture::Impl {
  Impl(RgbCamera& c, flow::CaptureSpec s, ThermalCamera* thermal_camera, Light* light_device)
      : camera(c),
        thermal_device(thermal_camera),
        light(light_device),
        spec(std::move(s)),
        settings(spec.settings),
        format{PixelFormat::kUyvy, spec.width, spec.height, spec.settings.sdk_frame_rate},
        pool(static_cast<size_t>(spec.width) * static_cast<size_t>(spec.height) * 2, spec.buffer_frames) {
    if (thermal_camera != nullptr) {
      // 실험(디스크 멈춤): 열화상은 받은 모든 프레임(30장/초)이 칸을 차지하므로 기본 40초분을 둡니다. 칸 메모리는 쓸 때 잡힙니다.
      const char* slots_env = std::getenv("HWLAB_THERMAL_SLOTS");
      const int32_t slots = slots_env != nullptr && std::atoi(slots_env) > 0 ? std::atoi(slots_env) : 1200;
      thermal = std::make_unique<ThermalStream>(*thermal_camera, std::max(spec.buffer_frames, slots), [this] { SidecarWritten(); });
    }
    status.light.attached = light != nullptr;
  }

  RgbCamera& camera;
  ThermalCamera* thermal_device;  // 셔터 교정 제어용. 수신과 열기, 닫기는 thermal(ThermalStream)이 합니다
  Light* light;  // 없으면 nullptr. 명령은 제어 스레드에서만 보냅니다
  const flow::CaptureSpec spec;
  flow::SettingsSpec settings;  // 미리보기 중 화면이 바꿀 수 있습니다. status_mutex로 보호합니다
  RecordingPaths paths;          // 이번 녹화의 경로. status_mutex로 보호합니다
  const record::VideoFormat format;
  int32_t preview_every = 0;
  std::function<void(const FrameView&)> preview_sink;
  FramePool pool;
  CameraState snapshot;
  Inbox<flow::Event> events;
  Inbox<flow::ActionKind> requests;  // kLockSettings, kReapplySettings, kPrepareThermal. 설정 변경은 kReapplySettings로 보냅니다
  std::atomic<bool> stopping{false};
  std::thread receiver;
  std::future<void> receiver_done;  // 수신 스레드가 끝나면 준비됩니다. Stop이 기다릴 시간을 정할 때 씁니다
  std::thread writer;
  std::thread controller;
  mutable std::mutex status_mutex;
  std::condition_variable finished_cv;
  CaptureStatus status;
  bool started = false;
  int32_t max_queue_frames = 0;  // 수신 스레드에서만 바꾸고, 녹화 끝에 쓰기 스레드가 읽습니다(status_mutex)
  CameraState recording_settings;  // 녹화를 시작할 때 다시 읽은 값(status_mutex)
  std::unique_ptr<ThermalStream> thermal;  // 열화상 카메라가 없으면 nullptr
  // 이번 녹화의 파일을 열었고 사이드카를 아직 다 쓰지 않음(status_mutex). 이 동안 다음 녹화를 받지 않습니다.
  // 다음 녹화가 시작되면 수신 스레드가 누락 수와 설정값을 지워, 쓰기 스레드가 앞 녹화의 사이드카에 다음 녹화의 값을 적습니다
  bool writing = false;
  int32_t pending_sidecars = 0;    // 이번 녹화에서 아직 쓰지 않은 사이드카 수. 0이 되면 녹화가 끝났다고 알립니다(status_mutex)
  bool thermal_recording = false;  // 이번 녹화에 열화상을 씀. 수신 스레드에서만 씁니다
  std::optional<Timestamp> first_written_host;  // 이번 녹화의 첫 RGB 프레임의 PC 수신 시각. 수신 스레드에서만 씁니다
  Timestamp last_written_host;     // 이번 녹화의 마지막 RGB 프레임의 PC 수신 시각. 수신 스레드에서만 씁니다

  // LED 조명 요청입니다. open이면 포트를 열며 밝기를 보내고, 아니면 밝기만 보냅니다.
  struct LightRequest {
    bool open = false;
    std::string port;
    int32_t percent = 0;
  };
  // LED 변경 전후의 RGB 평균 밝기를 모아 반영을 판정합니다. 수신 스레드에서만 씁니다.
  struct LightProbe {
    std::vector<double> before;
    int32_t sent_delta = 0;
    int32_t frames = 0;  // 변경 사건 뒤 받은 RGB 프레임 수
    std::vector<double> after;
  };
  Inbox<LightRequest> light_requests;
  std::optional<pixel::LightResponseRule> light_rule;  // Start 전에만 바꿉니다
  std::deque<int32_t> light_deltas;  // 보낸 LED 명령의 밝기 변화(%), 사건 순서. 직전 밝기를 모르면 0(status_mutex)
  record::LightRecord recording_light;  // 녹화를 시작할 때의 조명 기록(status_mutex)
  std::deque<double> light_recent;      // 최근 RGB 프레임의 평균 밝기. 수신 스레드에서만 씁니다
  std::optional<LightProbe> light_probe;  // 수신 스레드에서만 씁니다

  bool PostLight(const LightRequest& request) {
    if (light == nullptr) return false;
    {
      std::lock_guard<std::mutex> lock(status_mutex);
      if (!flow::AllowsLightChange(status.phase)) return false;
      ++status.light.pending;
    }
    light_requests.Post(request);
    return true;
  }

  // 제어 스레드에서 부릅니다. 잠금과 재설정보다 먼저 불러 자동 맞춤이 바뀐 LED 밝기에서 잠기게 합니다.
  void RunLightRequests() {
    if (light == nullptr) return;
    for (const LightRequest& r : light_requests.Drain()) {
      bool recording = false;
      {
        std::lock_guard<std::mutex> lock(status_mutex);
        recording = status.phase == flow::Phase::kRecording;
      }
      std::string error;
      bool sent = false;
      if (recording) {
        error = "녹화 중이라 조명 명령을 보내지 않았습니다";
      } else {
        const std::optional<int32_t> before = light->LastSentPercent();
        sent = r.open ? light->Open(r.port, r.percent, &error) : light->SetPercent(r.percent, &error);
        const std::optional<int32_t> after = light->LastSentPercent();
        if (sent) {
          const int32_t delta = before && after ? *after - *before : 0;
          std::lock_guard<std::mutex> lock(status_mutex);
          light_deltas.push_back(delta);
          // 대기 수가 0이 된 뒤 판정 결과를 읽는 쪽이 이전 판정을 보지 않도록 사건을 넣기 전에 바꿉니다.
          status.light.response = pixel::LightResponse::kUnknown;
          status.light.judging = MeasuresLight() && delta != 0;
        }
      }
      // 보낸 뒤에만 사건을 넣고, 사건을 넣은 뒤에 대기 수를 줄입니다. 대기 수가 0이면 촬영 흐름이 이미 이 사건을 받을 수 있습니다.
      if (sent) events.Post({flow::EventKind::kSettingChanged, SettingKind::kLight});
      std::lock_guard<std::mutex> lock(status_mutex);
      --status.light.pending;
      status.light.error = sent ? std::string() : error;
    }
  }

  bool MeasuresLight() const { return light != nullptr && light_rule && light_rule->window_frames > 0; }

  // 수신 스레드가 LED 변경 사건을 받았을 때 부릅니다. 직전 프레임들을 명령 전 밝기로 삼습니다.
  void BeginLightProbe() {
    int32_t delta = 0;
    {
      std::lock_guard<std::mutex> lock(status_mutex);
      if (!light_deltas.empty()) {
        delta = light_deltas.front();
        light_deltas.pop_front();
      }
      status.light.response = pixel::LightResponse::kUnknown;
      status.light.judging = MeasuresLight() && delta != 0;
    }
    light_probe.reset();
    if (MeasuresLight() && delta != 0) light_probe = LightProbe{{light_recent.begin(), light_recent.end()}, delta, 0, {}};
  }

  // RGB 프레임마다 수신 스레드에서 부릅니다. 판정 기준이 있을 때만 밝기를 재고, 녹화 중에는 재지 않습니다.
  void ProbeLight(const ReceivedFrame& frame, flow::Phase phase) {
    if (!MeasuresLight()) return;
    if (phase == flow::Phase::kRecording) {
      if (light_probe) {
        light_probe.reset();
        std::lock_guard<std::mutex> lock(status_mutex);
        status.light.judging = false;
      }
      light_recent.clear();
      return;
    }
    const double mean =
        pixel::MeanOf(pixel::MeasureBrightness(frame.view, {0, 0, spec.width, spec.height}, spec.clip_level));
    const auto window = static_cast<size_t>(light_rule->window_frames);
    if (light_probe) {
      if (++light_probe->frames >= light_rule->applied_frame) light_probe->after.push_back(mean);
      if (light_probe->after.size() >= window) {
        const pixel::LightResponse response =
            pixel::JudgeLightResponse(light_probe->before, light_probe->after, light_probe->sent_delta, *light_rule);
        light_probe.reset();
        std::lock_guard<std::mutex> lock(status_mutex);
        status.light.response = response;
        status.light.judging = false;
      }
    }
    light_recent.push_back(mean);
    while (light_recent.size() > window) light_recent.pop_front();
  }

  // RGB와 열화상 사이드카를 모두 쓴 뒤에 끝났다고 알려야 WaitFinished 뒤 두 파일을 함께 읽을 수 있습니다.
  void SidecarWritten() {
    {
      std::lock_guard<std::mutex> lock(status_mutex);
      if (--pending_sidecars > 0) return;
      status.finished = true;
      writing = false;
    }
    finished_cv.notify_all();
  }

  // 열화상 프레임도 RGB와 같은 촬영 흐름으로 판정합니다. 녹화 중일 때만 kWriteFrame이 옵니다.
  void AdvanceThermal(flow::FlowState& state, const flow::FlowRule& rule) {
    if (!thermal) return;
    for (const ThermalArrival& arrival : thermal->Drain()) {
      const flow::Step step =
          flow::Advance(state, {flow::EventKind::kFrame, SettingKind::kExposure, StreamKind::kThermal}, rule);
      state = step.state;
      bool written = false;
      for (const flow::Action& action : step.actions) {
        if (action.kind != flow::ActionKind::kWriteFrame) {
          Dispatch(action, nullptr);
        } else if (thermal_recording) {
          thermal->Write(arrival);
          written = true;
        }
      }
      if (!written) thermal->Discard(arrival);
    }
  }

  void SetReadback(const ReconcileResult& r) {
    std::lock_guard<std::mutex> lock(status_mutex);
    status.readback = r.readback;
    status.mismatches = r.mismatches;
  }

  flow::FlowRule CurrentRule() {
    std::lock_guard<std::mutex> lock(status_mutex);
    // 셔터 교정 시간을 PPG 안정 대기(StartRecording)와 같게 선언 fps로 프레임 수로 바꿉니다.
    const int64_t calibration_us = thermal != nullptr && thermal_device != nullptr ? thermal_device->CalibrationUs() : 0;
    const auto calibration_frames =
        static_cast<int32_t>((calibration_us * spec.settings.sdk_frame_rate + 999'999) / 1'000'000);
    return {spec.settle, spec.target_frames, settings.mode, thermal != nullptr, calibration_frames};
  }

  flow::SettingsSpec CurrentSettings() {
    std::lock_guard<std::mutex> lock(status_mutex);
    return settings;
  }

  void ReceiveLoop() {
    flow::FlowState state;
    while (!stopping) {
      const auto frame = camera.ReadFrame();
      if (frame && preview_sink && preview_every > 0 &&
          static_cast<int64_t>(frame->view.index) % preview_every == 0) {
        preview_sink(frame->view);
      }
      const flow::FlowRule rule = CurrentRule();
      if (!frame) events.Post({flow::EventKind::kDisconnected});
      // 순서는 다른 사건, 열화상 프레임, RGB 프레임입니다. RGB 프레임이 녹화를 끝내기 전에 받은 열화상까지 씁니다.
      for (const flow::Event& event : events.Drain()) {
        const flow::Step step = flow::Advance(state, event, rule);
        state = step.state;
        for (const flow::Action& action : step.actions) Dispatch(action, nullptr);
        if (event.kind == flow::EventKind::kSettingChanged && event.setting == SettingKind::kLight) BeginLightProbe();
      }
      AdvanceThermal(state, rule);
      if (frame) {
        recent_times.push_back(frame->view.time);
        if (recent_times.size() > 31) recent_times.pop_front();
        // 쓰기 버퍼는 이 스레드만 꺼내므로 지금 빈 버퍼가 있으면 아래 kWriteFrame에서 꺼낼 수 있습니다.
        flow::Event arrived{flow::EventKind::kFrame};
        arrived.storable = pool.FreeSlots() > 0;
        const flow::Step step = flow::Advance(state, arrived, rule);
        state = step.state;
        for (const flow::Action& action : step.actions) Dispatch(action, &*frame);
        ProbeLight(*frame, state.phase);
      } else {
        recent_times.clear();
      }
      {
        std::lock_guard<std::mutex> lock(status_mutex);
        status.phase = state.phase;
        status.frames_until_settled = std::max(state.settle.frames_until_settled, state.frames_until_ppg_settled);
        if (frame) ++status.received;
      }
      if (!frame && !WaitForReconnect()) break;
    }
    // 멈추기 직전에 들어온 중지 버튼까지 처리해야 녹화 중인 파일과 사이드카가 닫힙니다.
    const flow::FlowRule rule = CurrentRule();
    for (const flow::Event& event : events.Drain()) {
      const flow::Step step = flow::Advance(state, event, rule);
      state = step.state;
      for (const flow::Action& action : step.actions) Dispatch(action, nullptr);
    }
    AdvanceThermal(state, rule);
  }

  // 끊긴 카메라를 0.5초마다 다시 엽니다. 열리면 재연결 사건을 보내고, 촬영 흐름이 설정값 다시 설정을 지시합니다.
  bool WaitForReconnect() {
    while (!stopping) {
      if (camera.Reopen()) {
        events.Post({flow::EventKind::kReconnected});
        std::lock_guard<std::mutex> lock(status_mutex);
        ++status.reconnects;
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return false;
  }

  // 녹화 시작 직전의 실제 fps 판정에 쓰는 최근 프레임 시각입니다. 수신 스레드에서만 씁니다.
  std::deque<Timestamp> recent_times;
  bool rejected = false;  // 이번 녹화를 fps 불일치로 시작하지 않음

  // 쓰기가 밀려 빈 버퍼가 없습니다. 수신을 멈추지 않고 저장 누락으로 세며, 카메라 단 누락 판정을 위해 시각은 넘깁니다.
  void PushDropped(WriteMessage m) {
    m.kind = WriteMessage::Kind::kDropped;
    pool.Push(m);
    std::lock_guard<std::mutex> lock(status_mutex);
    ++status.dropped;
  }

  void Dispatch(const flow::Action& action, const ReceivedFrame* frame) {
    switch (action.kind) {
      case flow::ActionKind::kBeginRecording: {
        // 카메라가 fps를 스스로 낮춘 상태(어두운 장면의 펌웨어 자동 노출, 2026-10-16 실기 15fps)에서는 녹화하지 않습니다.
        const std::vector<Timestamp> recent(recent_times.begin(), recent_times.end());
        const int64_t period_us = 1'000'000 / spec.settings.sdk_frame_rate;
        const int64_t median = timing::MedianIntervalUs(recent);
        if (recent.size() >= 10 && !timing::MatchesPeriod(median, period_us, 5)) {
          rejected = true;
          events.Post({flow::EventKind::kStopPressed});
          std::lock_guard<std::mutex> lock(status_mutex);
          char text[200];
          std::snprintf(text, sizeof(text),
                        "실제 프레임 간격 %.1fms(%.1ffps)가 선언 %dfps와 달라 녹화하지 않았습니다. "
                        "장면이 어두우면 자동 맞춤이 노출을 늘려 fps를 낮춥니다",
                        median / 1000.0, 1e6 / static_cast<double>(median), spec.settings.sdk_frame_rate);
          status.error = text;
          break;
        }
        rejected = false;
        std::lock_guard<std::mutex> lock(status_mutex);
        writing = true;
        // 사이드카에는 녹화를 시작할 때의 값을 남깁니다. 녹화가 끝나면 미리보기 목표로 되돌리므로 그 뒤의 값은 다릅니다.
        recording_settings = status.readback;
        recording_light = paths.light;
        if (light != nullptr && recording_light.source == record::LightSource::kApp) {
          recording_light.percent = light->LastSentPercent();
          recording_light.response = status.light.judging ? pixel::LightResponse::kUnknown : status.light.response;
        }
        status.written = 0;
        status.dropped = 0;
        max_queue_frames = 0;
        thermal_recording =
            thermal != nullptr && !paths.thermal_raw.empty() && !paths.thermal_sidecar.empty();
        pending_sidecars = thermal_recording ? 2 : 1;
        first_written_host.reset();
        if (thermal_recording) {
          // 확인용 AVI의 색인 자리입니다. RGB 누락으로 녹화가 길어져도 열화상 장 수가 RGB 목표의 두 배를 넘지 않게 잡습니다.
          thermal->Begin({paths.thermal_raw, paths.thermal_sidecar, paths.thermal_preview,
                          spec.target_frames * 2 + 300, spec.thermal_record_period_us, settings.mode});
        }
        pool.Push(MessageOf(WriteMessage::Kind::kBegin));
        break;
      }
      case flow::ActionKind::kWriteFrame: {
        if (frame == nullptr || rejected) break;
        if (!first_written_host) first_written_host = frame->host_time;
        last_written_host = frame->host_time;
        WriteMessage m = MessageOf(WriteMessage::Kind::kFrame);
        m.record = {frame->view.index, frame->view.time, frame->host_time, 0, 0};
        const auto slot = pool.TryAcquire();
        if (!slot) {
          PushDropped(m);
          break;
        }
        // 쓰기 스레드가 아직 처리하지 못한 프레임 수입니다. 녹화마다 최댓값을 사이드카에 남겨 PC의 여유를 봅니다.
        {
          std::lock_guard<std::mutex> lock(status_mutex);
          max_queue_frames = std::max(max_queue_frames, spec.buffer_frames - pool.FreeSlots());
        }
        CopyPacked(frame->view, pool.Data(*slot));
        m.slot = *slot;
        pool.Push(m);
        break;
      }
      case flow::ActionKind::kDropFrame: {
        if (frame == nullptr || rejected) break;
        if (!first_written_host) first_written_host = frame->host_time;
        last_written_host = frame->host_time;
        WriteMessage m = MessageOf(WriteMessage::Kind::kFrame);
        m.record = {frame->view.index, frame->view.time, frame->host_time, 0, 0};
        PushDropped(m);
        break;
      }
      case flow::ActionKind::kEndRecording: {
        if (rejected) {
          // 파일을 열지 않았으므로 닫을 것이 없습니다. 화면에는 끝났다고 알립니다.
          rejected = false;
          std::lock_guard<std::mutex> lock(status_mutex);
          status.finished = true;
          status.end_reason = flow::EndReason::kStopped;
          finished_cv.notify_all();
          break;
        }
        WriteMessage m = MessageOf(WriteMessage::Kind::kEnd);
        m.end_reason = static_cast<int32_t>(action.reason);
        pool.Push(m);
        if (thermal_recording) thermal->End(first_written_host.value_or(last_written_host), last_written_host);
        thermal_recording = false;
        break;
      }
      case flow::ActionKind::kLockSettings:
      case flow::ActionKind::kReapplySettings:
      case flow::ActionKind::kPrepareThermal:
        requests.Post(action.kind);
        break;
    }
  }

  // 자동 교정 멈춤은 이 두 함수만 걸고 되돌립니다. 제어 스레드가 요청 순서대로 부르고, Stop은 두 스레드를 거둔 뒤 부릅니다.
  // 촬영 흐름은 잠금과 열화상 준비 뒤 미리보기로 돌아가는 모든 경로에서 kReapplySettings를 내므로 그때 되돌립니다.
  // CalibrateNow는 교정 구간과 버릴 프레임이 끝날 때까지(프레임 내용 판정) 반환하지 않으므로 잠금 완료는 그 뒤에 알립니다.
  void HoldThermalCalibration() {
    if (thermal_device == nullptr) return;
    std::string ignored;  // 열화상이 열려 있지 않거나 교정이 실패해도 RGB 녹화는 막지 않습니다
    thermal_device->CalibrateNow(&ignored);
    if (!thermal_held) thermal_held = thermal_device->HoldAutoCalibration(true, &ignored);
  }
  void ReleaseThermalCalibration() {
    if (thermal_device == nullptr || !thermal_held) return;
    std::string ignored;  // 닫힌 장치는 다시 열 때 멈춤이 해제된 상태입니다
    thermal_device->HoldAutoCalibration(false, &ignored);
    thermal_held = false;
  }
  bool thermal_held = false;  // 제어 스레드에서만 씁니다(Stop에서는 제어 스레드를 거둔 뒤)

  bool lens_shading_busy = false;  // 렌즈 셰이딩 보정 요청을 받고 제어 스레드가 아직 끝내지 않음(status_mutex)

  // 제어 스레드에서 부릅니다. 보정하는 동안 열화상 프레임이 오지 않으므로 실행할 때도 미리보기일 때만 보정합니다.
  void RunLensShadingRequest() {
    bool preview = false;
    {
      std::lock_guard<std::mutex> lock(status_mutex);
      if (!lens_shading_busy) return;
      preview = status.phase == flow::Phase::kPreview;
    }
    // 요청 뒤 끊겨 다시 여는 중이면 몇 초 늦게 보정하게 되고 그 사이 렌즈 앞을 치웠을 수 있어 보정하지 않습니다.
    const bool receiving = thermal_device->Connection() == ThermalConnection::kReceiving;
    std::string ignored;  // 결과와 이유는 카메라의 LensShading에 남습니다
    if (preview && receiving) thermal_device->CorrectLensShading(&ignored);
    thermal->RefreshLensShading();
    std::lock_guard<std::mutex> lock(status_mutex);
    lens_shading_busy = false;
  }

  void ControlLoop() {
    while (!stopping) {
      RunLensShadingRequest();
      RunLightRequests();
      flow::ActionKind request;
      if (!requests.WaitFor(&request, std::chrono::milliseconds(50))) continue;
      // 기다리는 사이에 들어온 LED 요청을 잠금보다 먼저 보냅니다. 자동 노출을 바뀐 LED 밝기에서 잠그기 위함입니다.
      RunLightRequests();
      // 직접 지정은 녹화 목표와 미리보기 목표가 같아 카메라 설정을 다시 맞추지 않습니다.
      if (request == flow::ActionKind::kPrepareThermal) {
        HoldThermalCalibration();
        events.Post({flow::EventKind::kThermalPrepared});
        continue;
      }
      const flow::SettingsSpec current = CurrentSettings();
      const CameraState target = request == flow::ActionKind::kLockSettings ? flow::RecordingTarget(current)
                                                                            : flow::PreviewTarget(current);
      const ReconcileResult result = Reconcile(camera, target);
      SetReadback(result);
      for (const auto& e : EventsFor(result.executed)) events.Post(e);
      if (request == flow::ActionKind::kLockSettings) {
        HoldThermalCalibration();
      } else {
        ReleaseThermalCalibration();
      }
      // 잠금이 끝났음을 알려야 촬영 흐름이 녹화를 시작합니다. 설정 변경 사건 뒤에 보내 안정화가 잠금 시점부터 셉니다.
      if (request == flow::ActionKind::kLockSettings) events.Post({flow::EventKind::kLockApplied});
    }
  }

  // RGB 영상 파일 하나입니다. 전체 화면이거나 잘라 낸 영역입니다. 쓰기 스레드에서만 씁니다.
  struct VideoOutput {
    pixel::Rect rect;
    std::string video;
    std::string sidecar;
    std::optional<record::FootSide> foot;
    std::unique_ptr<VideoWriter> file;
    std::vector<uint8_t> packed;  // 잘라 낸 화소. 전체 화면이면 쓰지 않습니다
    std::vector<record::FrameRecord> records;
    bool whole = false;
  };

  void WriteLoop() {
    std::vector<VideoOutput> outputs;
    std::vector<Timestamp> received_times;  // 저장 누락으로 버린 프레임도 포함합니다
    std::vector<timing::ClockSample> samples;  // 시계 식을 맞추는 표본. 저장 누락으로 버린 프레임도 포함합니다
    int64_t frames = 0;
    FrameView view;
    view.format = PixelFormat::kUyvy;
    view.width = spec.width;
    view.height = spec.height;
    view.row_stride[0] = spec.width * 2;
    for (;;) {
      WriteMessage m = pool.Pop();
      switch (m.kind) {
        case WriteMessage::Kind::kQuit:
          return;
        case WriteMessage::Kind::kBegin: {
          outputs.clear();
          received_times.clear();
          samples.clear();
          frames = 0;
          RecordingPaths current;
          {
            std::lock_guard<std::mutex> lock(status_mutex);
            current = paths;
          }
          if (!current.video.empty()) {
            outputs.push_back({{0, 0, spec.width, spec.height}, current.video, current.sidecar, std::nullopt, nullptr, {}, {}, true});
          }
          for (const CropOutput& c : current.crops) {
            outputs.push_back({c.rect, c.video, c.sidecar, c.foot, nullptr, {}, {}, false});
            outputs.back().packed.resize(pixel::PackedBytes(PixelFormat::kUyvy, c.rect));
          }
          for (VideoOutput& o : outputs) {
            std::string why;
            o.file = MakeVideoWriter(spec.codec, &why);
            const record::VideoFormat f{PixelFormat::kUyvy, o.rect.width, o.rect.height, spec.settings.sdk_frame_rate};
            if (!o.file || !o.file->Open(o.video, f, spec.target_frames)) {
              o.file.reset();
              std::lock_guard<std::mutex> lock(status_mutex);
              status.write_failed = true;
              if (status.error.empty()) {
                status.error = (why.empty() ? std::string("영상 파일을 열지 못함: ") : why + ": ") + o.video;
              }
            }
          }
          break;
        }
        case WriteMessage::Kind::kDropped:
          received_times.push_back(m.record.device_time);
          samples.push_back({m.record.device_time, m.record.host_time});
          break;
        case WriteMessage::Kind::kFrame: {
          view.plane[0] = pool.Data(m.slot);
          bool ok = true;
          for (VideoOutput& o : outputs) {
            record::FrameRecord r = m.record;
            const pixel::Brightness b = pixel::MeasureBrightness(view, o.rect, spec.clip_level);
            r.brightness_sum = b.sum;
            r.clipped_pixels = b.clipped_pixels;
            // 영역은 StartRecording에서 격자를 확인했으므로 CopyRect가 실패하면 쓰기 실패로 남깁니다.
            const uint8_t* pixels = o.whole ? pool.Data(m.slot) : o.packed.data();
            const bool copied = o.whole || pixel::CopyRect(view, o.rect, o.packed);
            // 프레임별 기록은 파일에 들어간 프레임만 남깁니다. 사이드카의 장 수가 영상 파일과 같아야 합니다.
            const bool stored = o.file != nullptr && copied && o.file->WriteFrame(pixels);
            if (stored) o.records.push_back(r);
            ok = stored && ok;
          }
          pool.Release(m.slot);
          if (ok) ++frames;
          received_times.push_back(m.record.device_time);
          samples.push_back({m.record.device_time, m.record.host_time});
          std::lock_guard<std::mutex> lock(status_mutex);
          if (!ok) {
            // 열화상(thermal_stream.cc)과 같게 쓰지 못한 프레임은 저장 누락으로 셉니다.
            ++status.dropped;
            status.write_failed = true;
            if (status.error.empty()) status.error = "프레임 쓰기 실패";
          }
          status.written = frames;
          break;
        }
        case WriteMessage::Kind::kEnd: {
          for (VideoOutput& o : outputs) {
            // FFV1은 닫을 때 남은 프레임과 색인을 씁니다. 실패하면 영상 끝이 잘리므로 화면에 알립니다.
            if (o.file && !o.file->Close()) {
              std::lock_guard<std::mutex> lock(status_mutex);
              status.write_failed = true;
              if (status.error.empty()) status.error = "영상 파일을 끝까지 쓰지 못함: " + FileNameOf(o.video);
            }
            o.file.reset();
          }
          Finish(outputs, received_times, samples, static_cast<flow::EndReason>(m.end_reason));
          outputs.clear();
          break;
        }
      }
    }
  }

  void Finish(const std::vector<VideoOutput>& outputs, const std::vector<Timestamp>& received_times,
              const std::vector<timing::ClockSample>& samples, flow::EndReason reason) {
    // 영상 파일마다 다른 것은 파일 이름, 영역, 발, 밝기, 파일 크기뿐이고 시각과 누락은 같습니다.
    record::Sidecar common;
    {
      std::lock_guard<std::mutex> lock(status_mutex);
      common.mode = settings.mode;
      common.settings = recording_settings;
      common.dropped_frames = status.dropped;
      common.max_queue_frames = max_queue_frames;
      common.light = recording_light;
    }
    common.device = camera.DeviceName();
    common.codec = spec.codec == flow::VideoCodec::kFfv1 ? "ffv1" : "uncompressed";
    common.clock = timing::FitClockMap(samples);
    common.target_frames = spec.target_frames;
    common.received_frames = static_cast<int64_t>(received_times.size());
    common.gaps = timing::FindFrameGaps(received_times, 1'000'000 / spec.settings.sdk_frame_rate);
    common.median_interval_us = timing::MedianIntervalUs(received_times);
    common.frame_rate = timing::CheckFrameRate(received_times, 1'000'000 / spec.settings.sdk_frame_rate);
    common.clip_level = spec.clip_level;
    common.buffer_frames = spec.buffer_frames;
    std::string failed;
    for (const VideoOutput& o : outputs) {
      record::Sidecar sidecar = common;
      sidecar.video_file = FileNameOf(o.video);
      sidecar.foot = o.foot;
      sidecar.format = {PixelFormat::kUyvy, o.rect.width, o.rect.height, spec.settings.sdk_frame_rate};
      sidecar.crop = o.rect;
      sidecar.written_frames = static_cast<int64_t>(o.records.size());
      sidecar.written_in_rgb_window = sidecar.written_frames;
      if (!o.records.empty()) {
        sidecar.first_frame = o.records.front().device_time;
        sidecar.last_frame = o.records.back().device_time;
      }
      sidecar.brightness_pixels = static_cast<int64_t>(o.rect.width) * o.rect.height;
      sidecar.records = o.records;
      std::vector<int64_t> sums;
      sums.reserve(o.records.size());
      for (const record::FrameRecord& r : o.records) sums.push_back(r.brightness_sum);
      sidecar.light->flicker = pixel::SummarizeFlicker(sums, sidecar.brightness_pixels, kFlickerMinPeriodicFraction);
      std::error_code size_error;
      const auto bytes = std::filesystem::file_size(
          std::filesystem::path(reinterpret_cast<const char8_t*>(o.video.c_str())), size_error);
      sidecar.file_bytes = size_error ? -1 : static_cast<int64_t>(bytes);
      const std::string json = record::BuildSidecarJson(sidecar);
      bool saved = false;
      if (std::FILE* f = OpenFileUtf8(o.sidecar, "wb")) {
        saved = std::fwrite(json.data(), 1, json.size(), f) == json.size();
        saved = std::fclose(f) == 0 && saved;
      }
      if (!saved && failed.empty()) failed = o.sidecar;
    }
    {
      std::lock_guard<std::mutex> lock(status_mutex);
      status.missing = timing::CountMissingFrames(common.gaps);
      status.frame_rate = common.frame_rate;
      status.end_reason = reason;
      if (!failed.empty()) status.write_failed = true;
      if (!failed.empty() && status.error.empty()) status.error = "사이드카를 쓰지 못함: " + failed;
    }
    SidecarWritten();
  }
};

Capture::Capture(RgbCamera& camera, flow::CaptureSpec spec, ThermalCamera* thermal, Light* light)
    : impl_(std::make_unique<Impl>(camera, std::move(spec), thermal, light)) {}

Capture::~Capture() {
  if (impl_->started) Stop();
}

bool Capture::Start(std::string* error) {
  if (!flow::ValidateCaptureSpec(impl_->spec).empty()) {
    *error = "촬영 선언이 검증을 통과하지 못함";
    return false;
  }
  impl_->snapshot = impl_->camera.ReadStoredState();
  const ReconcileResult preview = Reconcile(impl_->camera, flow::PreviewTarget(impl_->spec.settings));
  impl_->SetReadback(preview);
  for (const auto& e : EventsFor(preview.executed)) impl_->events.Post(e);
  impl_->started = true;
  impl_->writer = std::thread([this] { impl_->WriteLoop(); });
  impl_->controller = std::thread([this] { impl_->ControlLoop(); });
  std::promise<void> receiver_done;
  impl_->receiver_done = receiver_done.get_future();
  impl_->receiver = std::thread([this, done = std::move(receiver_done)]() mutable {
    impl_->ReceiveLoop();
    done.set_value();
  });
  if (impl_->thermal) impl_->thermal->Start();
  return true;
}

bool Capture::StartRecording(const std::string& video_path, const std::string& sidecar_path) {
  RecordingPaths paths;
  paths.video = video_path;
  paths.sidecar = sidecar_path;
  return StartRecording(paths);
}

bool Capture::StartRecording(const RecordingPaths& paths) {
  // 녹화 중에 영역 오류를 알면 이미 연 파일을 버려야 하므로, 촬영 흐름에 넣기 전에 확인합니다.
  std::string problem;
  if (paths.video.empty() && paths.crops.empty()) problem = "RGB 영상 경로가 없습니다";
  if (!paths.video.empty() && paths.sidecar.empty()) problem = "전체 화면 영상의 사이드카 경로가 없습니다";
  for (const CropOutput& c : paths.crops) {
    if (c.video.empty() || c.sidecar.empty()) problem = "영역 영상 또는 사이드카 경로가 없습니다";
    if (!pixel::IsAligned(c.rect, PixelFormat::kUyvy, impl_->spec.width, impl_->spec.height)) {
      problem = "영역이 프레임 밖이거나 UYVY 색차 격자(x와 폭이 짝수)에 맞지 않습니다";
    }
  }
  {
    std::lock_guard<std::mutex> lock(impl_->status_mutex);
    if (impl_->status.phase != flow::Phase::kPreview || impl_->writing) return false;
    // 보내지 않은 LED 명령이 있으면 안정화와 잠금이 바뀌기 전 밝기를 기준으로 시작하므로 녹화하지 않습니다.
    if (problem.empty() && impl_->status.light.pending > 0) problem = "조명 명령을 보내는 중이라 녹화하지 않았습니다";
    if (!problem.empty()) {
      impl_->status.error = problem;
      impl_->status.end_reason.reset();
      impl_->status.finished = true;
      impl_->finished_cv.notify_all();
      return false;
    }
    impl_->paths = paths;
    impl_->status.finished = false;
    impl_->status.end_reason.reset();
    impl_->status.missing = 0;
    impl_->status.frame_rate = {};
    impl_->status.error.clear();
    impl_->status.write_failed = false;
  }
  flow::Event start{flow::EventKind::kStartPressed};
  // 선언 fps로 프레임 수를 정하므로 실제 대기는 카메라의 실제 fps를 따릅니다. 녹화 직전 fps 검사는 대기 전체가 아니라
  // 마지막 약 1초의 간격만 보므로, 대기 중 fps가 빨랐으면 대기는 정한 시간보다 짧습니다.
  const int64_t fps = impl_->spec.settings.sdk_frame_rate;
  if (paths.ppg_settle_us > 0) {
    start.ppg_settle_frames = static_cast<int32_t>((paths.ppg_settle_us * fps + 999'999) / 1'000'000);
  }
  impl_->events.Post(start);
  return true;
}

void Capture::StopRecording() { impl_->events.Post({flow::EventKind::kStopPressed}); }

bool Capture::UpdateSettings(const flow::SettingsSpec& settings) {
  {
    std::lock_guard<std::mutex> lock(impl_->status_mutex);
    if (impl_->status.phase != flow::Phase::kPreview) return false;
    impl_->settings = settings;
  }
  impl_->requests.Post(flow::ActionKind::kReapplySettings);
  return true;
}

void Capture::SetPreviewSink(int32_t every_frames, std::function<void(const FrameView&)> sink) {
  impl_->preview_every = every_frames;
  impl_->preview_sink = std::move(sink);
}

void Capture::SetThermalPreviewSink(int32_t every_frames, std::function<void(const FrameView&)> sink) {
  if (impl_->thermal) impl_->thermal->SetPreviewSink(every_frames, std::move(sink));
}

bool Capture::OpenLight(const std::string& port_name, int32_t percent) {
  return impl_->PostLight({true, port_name, percent});
}

bool Capture::SetLight(int32_t percent) { return impl_->PostLight({false, std::string(), percent}); }

void Capture::CloseLight() {
  if (impl_->light != nullptr) impl_->light->Close();
}

void Capture::SetLightResponseRule(const pixel::LightResponseRule& rule) { impl_->light_rule = rule; }

bool Capture::CorrectThermalLensShading() {
  if (impl_->thermal_device == nullptr || !impl_->thermal) return false;
  if (impl_->thermal_device->Connection() != ThermalConnection::kReceiving) return false;
  std::lock_guard<std::mutex> lock(impl_->status_mutex);
  if (impl_->status.phase != flow::Phase::kPreview || impl_->lens_shading_busy) return false;
  impl_->lens_shading_busy = true;
  return true;
}

CaptureStatus Capture::Status() const {
  CaptureStatus copy;
  bool lens_shading_busy = false;
  {
    std::lock_guard<std::mutex> lock(impl_->status_mutex);
    copy = impl_->status;
    lens_shading_busy = impl_->lens_shading_busy;
  }
  if (impl_->thermal) copy.thermal = impl_->thermal->Status();
  copy.thermal.lens_shading_busy = lens_shading_busy;
  // Light는 쓰기 실패로 스스로 끊기므로 연결 상태와 밝기는 저장해 두지 않고 매번 읽습니다.
  if (impl_->light != nullptr) {
    copy.light.connected = impl_->light->IsConnected();
    copy.light.port = impl_->light->PortName();
    copy.light.percent = impl_->light->LastSentPercent();
  }
  return copy;
}

bool Capture::WaitFinished(std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(impl_->status_mutex);
  return impl_->finished_cv.wait_for(lock, timeout, [&] { return impl_->status.finished; });
}

ReconcileResult Capture::Stop(const std::function<void(const char*)>& on_step) {
  if (!impl_->started) return {};
  const auto step = [&](const char* name) {
    if (on_step) on_step(name);
  };
  impl_->started = false;
  // 녹화 중이면 중지 버튼과 같게 끝내 파일과 사이드카를 닫습니다.
  step("녹화 중지 요청");
  impl_->events.Post({flow::EventKind::kStopPressed});
  impl_->stopping = true;
  step("RGB 수신 스레드 끝 기다림");
  // 프레임이 오면 수신 루프는 한 프레임 안에 끝납니다. 끝나지 않으면 ReadFrame이 오지 않는 프레임을 기다리는 중입니다.
  if (impl_->receiver_done.wait_for(kReceiverStopWait) == std::future_status::timeout) {
    step("RGB 수신 멈춤, 카메라 스트림을 닫음");
    impl_->camera.Interrupt();
  }
  impl_->receiver.join();
  step("카메라 제어 스레드 끝 기다림");
  impl_->controller.join();
  {
    // 제어 스레드가 실행하기 전에 멈춘 보정 요청은 하지 않은 채로 끝냅니다.
    std::lock_guard<std::mutex> lock(impl_->status_mutex);
    impl_->lens_shading_busy = false;
  }
  // 제어 스레드가 끝나 실행하지 못한 되돌림 요청이 있을 수 있으므로 장치를 닫기 전에 멈춤을 되돌립니다.
  step("열화상 보정 멈춤 되돌림");
  impl_->ReleaseThermalCalibration();
  // 수신 스레드가 마지막으로 넣은 열화상 End까지 처리한 뒤 멈춥니다.
  step("열화상 정지");
  if (impl_->thermal) impl_->thermal->Stop();
  step("쓰기 스레드 끝 기다림");
  impl_->pool.Push(MessageOf(WriteMessage::Kind::kQuit));
  impl_->writer.join();
  step("카메라 설정 되돌림");
  return Restore(impl_->camera, impl_->snapshot);
}

std::string DescribeSlowFrames(const timing::FrameRateCheck& check) {
  if (check.slow_intervals <= 0 || check.spans.empty()) return {};
  char text[160];
  const timing::SlowSpan& first = check.spans.front();
  std::snprintf(text, sizeof(text), "fps 저하 %.1f~%.1f초, 최저 %.1ffps", first.start_us / 1e6, first.end_us / 1e6, check.min_fps);
  std::string out = text;
  if (check.spans.size() > 1) out += " 외 " + std::to_string(check.spans.size() - 1) + "곳";
  return out;
}

}  // namespace hwlab::platform
