#include "thermal_stream.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

#include "hwlab/pixel/brightness.h"
#include "hwlab/platform/shot_folder.h"
#include "hwlab/platform/thermal_writer.h"
#include "hwlab/timing/clock_map.h"
#include "hwlab/timing/frame_gap.h"
#include "hwlab/timing/record_rate.h"
#include "hwlab/timing/warmup.h"

namespace hwlab::platform {

namespace {

// ReadFrame 한 번의 대기 시간입니다. 멈춤 요청과 끊김 판정을 이 간격으로 확인합니다.
constexpr int64_t kReadTimeoutUs = 100'000;
constexpr auto kReopenInterval = std::chrono::milliseconds(500);
constexpr size_t kRecentFrames = 31;

std::string FileNameOf(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// 기록 파일의 fps입니다. 고르는 동안은 기록 주기, 모두 기록하면 실측 간격으로 정합니다. 재생 속도에만 쓰입니다.
int32_t RecordedFrameRate(const timing::RecordRateRule& rule, int64_t median_interval_us) {
  int64_t period_us = median_interval_us > 0 ? median_interval_us : kThermalFramePeriodUs;
  if (rule.period_us > 0 && period_us * 100 < rule.period_us * rule.thin_below_percent) period_us = rule.period_us;
  return std::max<int32_t>(1, static_cast<int32_t>(std::llround(1e6 / static_cast<double>(period_us))));
}

int64_t FileBytes(const std::string& utf8) {
  std::error_code code;
  const auto bytes = std::filesystem::file_size(std::filesystem::path(std::u8string(utf8.begin(), utf8.end())), code);
  return code ? -1 : static_cast<int64_t>(bytes);
}

struct FileCloser {
  void operator()(std::FILE* file) const { std::fclose(file); }
};

constexpr char kCameraTemperatureHeader[] = "index,device_time_us,host_time_us,fpa,shutter_raw,shutter_c\n";

// 사이드카와 매니페스트의 형식을 바꾸지 않으려고 카메라 온도 파일 이름을 원시값 파일 이름에서 만듭니다.
std::string CameraTemperaturePath(const std::string& raw_path) {
  constexpr std::string_view kRawSuffix = ".raw16";
  std::string base = raw_path;
  if (base.ends_with(kRawSuffix)) base.resize(base.size() - kRawSuffix.size());
  return base + "_camera_temp.csv";
}

// FPA 값은 단위를 몰라 반올림하지 않고, 다시 읽으면 같은 float가 되는 가장 짧은 글로 씁니다.
std::string FloatText(float value) {
  char text[32];
  for (int digits = 6; digits <= 9; ++digits) {
    std::snprintf(text, sizeof(text), "%.*g", digits, static_cast<double>(value));
    if (std::strtof(text, nullptr) == value) break;
  }
  return text;
}

// 번호와 시각은 열화상 사이드카 per_frame과 같은 값이라 두 파일을 index로 맞출 수 있습니다.
std::string CameraTemperatureLine(const record::FrameRecord& r, const std::optional<CameraTemperature>& t) {
  char text[96];
  std::snprintf(text, sizeof(text), "%lld,%lld,%lld,", static_cast<long long>(r.index),
                static_cast<long long>(r.device_time.us), static_cast<long long>(r.host_time.us));
  std::string line = text;
  if (!t) return line + ",,\n";
  std::snprintf(text, sizeof(text), ",%u,%.4f\n", static_cast<unsigned>(t->shutter_raw), t->shutter_c);
  return line + FloatText(t->fpa) + text;
}

}  // namespace

ThermalStream::ThermalStream(ThermalCamera& camera, int32_t buffer_frames, std::function<void()> on_sidecar_written)
    : camera_(camera),
      buffer_frames_(std::max(buffer_frames, 1)),
      on_sidecar_written_(std::move(on_sidecar_written)),
      buffers_(static_cast<size_t>(buffer_frames_)) {
  for (int32_t i = buffer_frames_ - 1; i >= 0; --i) free_slots_.push_back(i);
  status_.attached = true;
}

ThermalStream::~ThermalStream() { Stop(); }

void ThermalStream::SetPreviewSink(int32_t every_frames, std::function<void(const FrameView&)> sink) {
  preview_every_ = every_frames;
  preview_sink_ = std::move(sink);
}

void ThermalStream::Start() {
  if (started_) return;
  started_ = true;
  writer_ = std::thread([this] { WriteLoop(); });
  receiver_ = std::thread([this] { ReceiveLoop(); });
}

void ThermalStream::Stop() {
  if (!started_) return;
  started_ = false;
  {
    std::lock_guard<std::mutex> lock(stop_mutex_);
    stopping_ = true;
  }
  stop_cv_.notify_all();
  receiver_.join();
  for (const ThermalArrival& a : Drain()) Discard(a);
  Message quit;
  quit.kind = Message::Kind::kQuit;
  Post(std::move(quit));
  writer_.join();
}

bool ThermalStream::WaitStopping(std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(stop_mutex_);
  return stop_cv_.wait_for(lock, timeout, [&] { return stopping_; });
}

std::vector<ThermalArrival> ThermalStream::Drain() {
  std::lock_guard<std::mutex> lock(arrival_mutex_);
  std::vector<ThermalArrival> out;
  out.swap(arrivals_);
  return out;
}

void ThermalStream::Post(Message message) {
  {
    std::lock_guard<std::mutex> lock(message_mutex_);
    messages_.push_back(std::move(message));
  }
  message_cv_.notify_one();
}

void ThermalStream::ReleaseSlot(int32_t slot) {
  if (slot < 0) return;
  std::lock_guard<std::mutex> lock(slot_mutex_);
  free_slots_.push_back(slot);
}

void ThermalStream::Begin(const ThermalRecording& recording) {
  Message m;
  m.kind = Message::Kind::kBegin;
  m.recording = recording;
  Post(std::move(m));
}

void ThermalStream::Write(const ThermalArrival& arrival) {
  Message m;
  m.kind = arrival.slot < 0 ? Message::Kind::kDropped : Message::Kind::kFrame;
  m.arrival = arrival;
  Post(std::move(m));
}

void ThermalStream::Discard(const ThermalArrival& arrival) { ReleaseSlot(arrival.slot); }

void ThermalStream::End(Timestamp rgb_first_host_time, Timestamp rgb_last_host_time) {
  Message m;
  m.kind = Message::Kind::kEnd;
  m.rgb_first_host_time = rgb_first_host_time;
  m.rgb_last_host_time = rgb_last_host_time;
  Post(std::move(m));
}

void ThermalStream::RefreshLensShading() {
  // 수신 스레드가 끊김으로 지운 뒤에 앞 값을 다시 넣지 않도록 상태를 잡은 채 읽습니다(카메라 상태 잠금만 안에서 잡음).
  std::lock_guard<std::mutex> lock(status_mutex_);
  status_.lens_shading = camera_.LensShading();
}

ThermalStatus ThermalStream::Status() const {
  std::lock_guard<std::mutex> lock(status_mutex_);
  return status_;
}

void ThermalStream::ReceiveLoop() {
  bool open = false;
  bool opened_before = false;
  std::deque<Timestamp> recent;
  timing::WarmupState warmup;
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(stop_mutex_);
      if (stopping_) break;
    }
    if (!open) {
      std::string why;
      if (!camera_.Open(&why)) {
        {
          std::lock_guard<std::mutex> lock(status_mutex_);
          status_.connection = ThermalConnection::kClosed;
          status_.error = "열화상 카메라를 열지 못함: " + why;
        }
        if (WaitStopping(kReopenInterval)) break;
        continue;
      }
      open = true;
      recent.clear();
      const std::string name = camera_.DeviceName();
      std::lock_guard<std::mutex> lock(status_mutex_);
      device_name_ = name;
      // RefreshLensShading과 같게 상태를 잡은 채 읽어, 그 사이 끝난 보정 결과를 앞 값으로 덮지 않습니다.
      status_.lens_shading = camera_.LensShading();
      if (opened_before) ++status_.reconnects;
      opened_before = true;
    }
    const std::optional<ReceivedFrame> frame = camera_.ReadFrame(kReadTimeoutUs);
    const ThermalConnection connection = camera_.Connection();
    if (connection == ThermalConnection::kLost) {
      // SDK는 USB를 뽑아도 열린 상태로 남으므로 프레임이 오지 않은 시간으로 판정하고 닫은 뒤 다시 엽니다.
      camera_.Close();
      open = false;
      recent.clear();
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_.connection = connection;
      status_.measured_fps = 0;
      // 닫은 장치의 보정은 남지 않으므로(실측) 다시 열어 보정 파일을 불러올 때까지 지웁니다.
      status_.lens_shading.reset();
      status_.camera_temperature.reset();
      continue;
    }
    if (!frame) {
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_.connection = connection;
      continue;
    }
    recent.push_back(frame->host_time);
    if (recent.size() > kRecentFrames) recent.pop_front();
    const int64_t median_us = timing::MedianIntervalUs(std::vector<Timestamp>(recent.begin(), recent.end()));
    const std::optional<CameraTemperature> temperature = camera_.LastCameraTemperature();
    if (temperature) {
      warmup = timing::AfterCameraTemperature(std::move(warmup), frame->host_time, temperature->fpa, kTeEv1WarmupRule);
    }
    {
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_.connection = connection;
      ++status_.received;
      status_.measured_fps = median_us > 0 ? 1e6 / static_cast<double>(median_us) : 0.0;
      status_.camera_temperature = temperature;
      status_.warmup = warmup.status;
    }
    if (preview_sink_ && preview_every_ > 0 && static_cast<int64_t>(frame->view.index) % preview_every_ == 0) {
      preview_sink_(frame->view);
    }

    ThermalArrival arrival;
    arrival.width = frame->view.width;
    arrival.height = frame->view.height;
    arrival.record = {frame->view.index, frame->view.time, frame->host_time, 0, 0};
    arrival.median_interval_us = median_us;
    arrival.camera_temperature = temperature;
    {
      std::lock_guard<std::mutex> lock(slot_mutex_);
      if (!free_slots_.empty()) {
        arrival.slot = free_slots_.back();
        free_slots_.pop_back();
      }
    }
    if (arrival.slot >= 0 && frame->view.format == PixelFormat::kY16) {
      // SDK 버퍼는 다음 ReadFrame에서 바뀌므로 여기서 행 간격 없이 복사하고, 디스크 쓰기는 쓰기 스레드가 합니다.
      std::vector<uint8_t>& buffer = buffers_[static_cast<size_t>(arrival.slot)];
      const size_t row_bytes = static_cast<size_t>(arrival.width) * 2;
      buffer.resize(row_bytes * static_cast<size_t>(arrival.height));
      for (int32_t y = 0; y < arrival.height; ++y) {
        std::memcpy(buffer.data() + row_bytes * static_cast<size_t>(y),
                    frame->view.plane[0] + static_cast<ptrdiff_t>(y) * frame->view.row_stride[0], row_bytes);
      }
    } else if (arrival.slot >= 0) {
      ReleaseSlot(arrival.slot);
      arrival.slot = -1;
    }
    std::lock_guard<std::mutex> lock(arrival_mutex_);
    arrivals_.push_back(arrival);
  }
  if (open) camera_.Close();
  std::lock_guard<std::mutex> lock(status_mutex_);
  status_.connection = ThermalConnection::kClosed;
  status_.measured_fps = 0;
}

void ThermalStream::WriteLoop() {
  bool active = false;
  ThermalRecording recording;
  Raw16File raw;
  ThermalPreviewFile preview;
  bool raw_open = false;
  bool preview_open = false;
  int32_t width = 0;
  int32_t height = 0;
  std::vector<record::FrameRecord> records;
  std::vector<Timestamp> received_times;  // 저장 누락으로 버린 프레임도 넣습니다
  std::vector<timing::ClockSample> samples;
  timing::RecordRateRule rate_rule;
  timing::RecordRateState rate_state;
  int32_t frame_rate = 0;
  std::unique_ptr<std::FILE, FileCloser> temperature_file;  // 카메라 온도 파일. 값을 처음 받은 때 엽니다
  std::string temperature_path;
  bool temperature_failed = false;  // 열지 못했거나 쓰지 못했으면 이번 녹화에서 더 쓰지 않습니다

  const auto fail = [this](const std::string& why, bool write_failed = true) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.write_failed = status_.write_failed || write_failed;
    if (status_.error.empty()) status_.error = why;
  };

  for (;;) {
    Message m;
    {
      std::unique_lock<std::mutex> lock(message_mutex_);
      message_cv_.wait(lock, [&] { return !messages_.empty(); });
      m = std::move(messages_.front());
      messages_.pop_front();
    }
    switch (m.kind) {
      case Message::Kind::kQuit:
        return;
      case Message::Kind::kBegin: {
        // 이번 녹화의 수는 쓰기 스레드가 Begin을 꺼낸 때부터 셉니다. 앞 녹화의 End까지 처리한 뒤이기 때문입니다.
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_.written = 0;
        status_.dropped = 0;
        status_.missing = 0;
        status_.error.clear();
        status_.write_failed = false;
        max_queue_frames_ = 0;
        active = true;
        recording = m.recording;
        raw_open = false;
        preview_open = false;
        width = 0;
        height = 0;
        records.clear();
        received_times.clear();
        samples.clear();
        rate_rule = {recording.record_period_us};
        rate_state = {};
        frame_rate = 0;
        temperature_file.reset();
        temperature_path = CameraTemperaturePath(recording.raw_path);
        temperature_failed = false;
        break;
      }
      case Message::Kind::kDropped:
      case Message::Kind::kFrame: {
        const ThermalArrival& a = m.arrival;
        if (!active) {
          ReleaseSlot(a.slot);
          break;
        }
        // 누락 판정과 시계 식에는 고르기 전의 모든 프레임을 넣습니다. 고른 자리를 장치 누락으로 세지 않기 위함입니다.
        received_times.push_back(a.record.host_time);
        samples.push_back({a.record.device_time, a.record.host_time});
        const timing::RecordRateStep pace =
            timing::AdvanceRecordRate(rate_state, a.record.host_time, a.median_interval_us, rate_rule);
        rate_state = pace.state;
        if (!pace.record) {
          ReleaseSlot(a.slot);
          break;
        }
        if (a.slot < 0) {
          std::lock_guard<std::mutex> lock(status_mutex_);
          ++status_.dropped;
          break;
        }
        {
          // 수신 스레드가 복사했지만 아직 파일에 쓰지 않은 프레임 수입니다. buffer_frames에 닿으면 저장 누락이 생깁니다.
          int32_t free_slots = 0;
          {
            std::lock_guard<std::mutex> lock(slot_mutex_);
            free_slots = static_cast<int32_t>(free_slots_.size());
          }
          std::lock_guard<std::mutex> lock(status_mutex_);
          max_queue_frames_ = std::max(max_queue_frames_, buffer_frames_ - free_slots);
        }
        if (!raw_open && width == 0) {
          width = a.width;
          height = a.height;
          frame_rate = RecordedFrameRate(rate_rule, a.median_interval_us);
          raw_open = raw.Open(recording.raw_path, width, height);
          if (!raw_open) fail("열화상 원시값 파일을 열지 못함: " + recording.raw_path);
          if (!recording.preview_path.empty()) {
            preview_open = preview.Open(recording.preview_path, width, height, frame_rate,
                                        recording.preview_max_frames, std::nullopt);
            if (!preview_open) fail("열화상 확인용 영상을 열지 못함: " + recording.preview_path);
          }
        }
        FrameView view;
        view.format = PixelFormat::kY16;
        view.width = a.width;
        view.height = a.height;
        view.plane[0] = buffers_[static_cast<size_t>(a.slot)].data();
        view.row_stride[0] = a.width * 2;
        view.stream = StreamKind::kThermal;
        view.index = a.record.index;
        view.time = a.record.device_time;
        record::FrameRecord r = a.record;
        const bool same_size = a.width == width && a.height == height;
        bool ok = false;
        if (same_size) {
          const pixel::Brightness b = pixel::MeasureBrightness(view, {0, 0, width, height}, 0xFFFF);
          r.brightness_sum = b.sum;
          r.clipped_pixels = b.clipped_pixels;
          ok = raw_open && raw.WriteFrame(view);
          // 확인용 영상은 분석에 쓰지 않으므로 색인 자리를 넘겨 실패해도 원시값 저장은 계속합니다.
          if (preview_open) preview.WriteFrame(view);
        }
        ReleaseSlot(a.slot);
        if (!ok) {
          // 쓰지 못한 프레임은 원시값 파일의 프레임 순서에 들어가지 않으므로 저장 누락으로 셉니다.
          fail(same_size ? "열화상 원시값 쓰기 실패" : "열화상 프레임 크기가 녹화 중에 바뀜", same_size);
          std::lock_guard<std::mutex> lock(status_mutex_);
          ++status_.dropped;
          break;
        }
        records.push_back(r);
        if (!temperature_failed && (temperature_file || a.camera_temperature)) {
          std::string text;
          if (!temperature_file) {
            // 값을 주지 않는 카메라는 파일을 만들지 않습니다. 앞서 쓴 프레임은 값 칸을 비워 원시값 파일과 줄 수를 맞춥니다.
            temperature_file.reset(OpenFileUtf8(temperature_path, "wb"));
            text = kCameraTemperatureHeader;
            for (size_t i = 0; i + 1 < records.size(); ++i) text += CameraTemperatureLine(records[i], std::nullopt);
          }
          text += CameraTemperatureLine(r, a.camera_temperature);
          if (!temperature_file || std::fwrite(text.data(), 1, text.size(), temperature_file.get()) != text.size()) {
            temperature_failed = true;
            fail("열화상 카메라 온도 파일을 쓰지 못함: " + temperature_path);
          }
        }
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_.written = static_cast<int64_t>(records.size());
        break;
      }
      case Message::Kind::kEnd: {
        if (!active) break;
        active = false;
        if (raw_open && !raw.Close()) fail("열화상 원시값 파일을 끝까지 쓰지 못함: " + recording.raw_path);
        if (temperature_file && std::fclose(temperature_file.release()) != 0 && !temperature_failed) {
          fail("열화상 카메라 온도 파일을 다 쓰지 못함: " + temperature_path);
        }
        std::optional<pixel::Y16Range> range;
        if (preview_open) {
          range = preview.Range();
          preview.Close();
        }
        record::Sidecar sidecar;
        sidecar.video_file = FileNameOf(recording.raw_path);
        {
          std::lock_guard<std::mutex> lock(status_mutex_);
          sidecar.device = device_name_;
          if (status_.lens_shading) sidecar.lens_shading = status_.lens_shading->record;
        }
        sidecar.stream = StreamKind::kThermal;
        sidecar.format = {PixelFormat::kY16, width, height, frame_rate};
        sidecar.codec = "raw16";
        sidecar.crop = {0, 0, width, height};
        sidecar.mode = recording.mode;
        sidecar.clock = timing::FitClockMap(samples);
        sidecar.display_range = range;
        sidecar.written_frames = static_cast<int64_t>(records.size());
        sidecar.received_frames = static_cast<int64_t>(received_times.size());
        sidecar.record_period_us = recording.record_period_us;
        for (const record::FrameRecord& r : records) {
          const Timestamp common = timing::ToCommonTime(sidecar.clock, r.device_time);
          if (common >= m.rgb_first_host_time && common <= m.rgb_last_host_time) ++sidecar.written_in_rgb_window;
        }
        if (!records.empty()) {
          sidecar.first_frame = records.front().device_time;
          sidecar.last_frame = records.back().device_time;
        }
        // 열화상이 RGB 녹화보다 먼저 끊겨 다시 오지 않은 구간은 이웃한 두 프레임 사이가 아니므로, RGB 마지막 수신 시각을
        // 끝에 두고 누락을 찾습니다. 위치는 끝에만 더하므로 다른 누락 자리의 번호는 바뀌지 않습니다.
        std::vector<Timestamp> gap_times = received_times;
        if (!gap_times.empty() && m.rgb_last_host_time > gap_times.back()) gap_times.push_back(m.rgb_last_host_time);
        // 열화상 장치마다 주기가 달라(실험 장치 30Hz, 제품 장치 8~9Hz) 받은 간격의 중앙값을 누락 판정의 주기로 씁니다.
        sidecar.median_interval_us = timing::MedianIntervalUs(received_times);
        sidecar.gaps = timing::FindFrameGaps(
            gap_times, sidecar.median_interval_us > 0 ? sidecar.median_interval_us : kThermalFramePeriodUs);
        // 열화상은 받은 간격의 중앙값을 주기로 봅니다. 기록 주기(record_period_us)로 거르기 전의 받은 시각입니다.
        sidecar.frame_rate = timing::CheckFrameRate(
            received_times, sidecar.median_interval_us > 0 ? sidecar.median_interval_us : kThermalFramePeriodUs);
        sidecar.brightness_pixels = static_cast<int64_t>(width) * height;
        sidecar.clip_level = 0xFFFF;
        sidecar.records = std::move(records);
        records.clear();
        sidecar.buffer_frames = buffer_frames_;
        sidecar.file_bytes = raw_open ? FileBytes(recording.raw_path) : -1;
        {
          std::lock_guard<std::mutex> lock(status_mutex_);
          sidecar.dropped_frames = status_.dropped;
          sidecar.max_queue_frames = max_queue_frames_;
        }
        const std::string json = record::BuildSidecarJson(sidecar);
        bool saved = false;
        if (std::FILE* f = OpenFileUtf8(recording.sidecar_path, "wb")) {
          saved = std::fwrite(json.data(), 1, json.size(), f) == json.size();
          saved = std::fclose(f) == 0 && saved;
        }
        if (!saved) fail("열화상 사이드카를 쓰지 못함: " + recording.sidecar_path);
        {
          std::lock_guard<std::mutex> lock(status_mutex_);
          status_.missing = timing::CountMissingFrames(sidecar.gaps);
        }
        if (on_sidecar_written_) on_sidecar_written_();
        break;
      }
    }
  }
}

}  // namespace hwlab::platform
