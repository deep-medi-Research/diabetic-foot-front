#include "hwlab/platform/ppg_sensor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "hwlab/platform/shot_folder.h"
#include "hwlab/timing/frame_gap.h"

namespace hwlab::platform {

namespace {

constexpr int32_t kWriteTimeoutMs = 200;
constexpr int64_t kSampleUs = 1'000'000 / kPpgSampleHz;
constexpr size_t kReadBytes = 4096;
constexpr char kNotFound[] = "이 포트에서 PPG 장치를 찾지 못했습니다";

int64_t SteadyNowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

PpgParserCounts Minus(const PpgParserCounts& a, const PpgParserCounts& b) {
  return {a.crc_failures - b.crc_failures, a.bad_lengths - b.bad_lengths, a.implausible - b.implausible,
          a.skipped_bytes - b.skipped_bytes, a.messages - b.messages};
}

}  // namespace

PpgShotRecord::PpgShotRecord(Timestamp begin) { record_.begin_time = begin; }

void PpgShotRecord::Close(Timestamp end) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) return;
  closed_ = true;
  record_.end_time = end;
}

bool PpgShotRecord::Closed() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return closed_;
}

PpgRecord PpgShotRecord::Finish() const {
  PpgRecord out;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    out = record_;
  }
  // 다시 시작 직후 먼저 온 측정 데이터까지 한 식으로 맞추면 빈틈 뒤 표본의 공통 시각이 빈틈만큼 이르게 나옵니다.
  out.clock = timing::FitClockSegments(out.anchors, PpgLink::kReceiveGapUs);
  out.common_time.reserve(out.device_time.size());
  for (const Timestamp t : out.device_time) out.common_time.push_back(timing::ToCommonTime(out.clock, t));
  return out;
}

void PpgShotRecord::AddMeasure(const PpgMeasure& measure, int64_t first_index, Timestamp host_time) {
  const auto count = static_cast<int64_t>(measure.samples.size());
  if (count == 0) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) return;
  for (int64_t i = 0; i < count; ++i) {
    record_.values.push_back(measure.samples[static_cast<size_t>(i)]);
    record_.device_time.push_back(Timestamp{(first_index + i) * kSampleUs});
    record_.measure_error.push_back(measure.error);
    record_.heart_rate.push_back(static_cast<uint8_t>(measure.heart_rate));
    record_.spo2.push_back(static_cast<uint8_t>(measure.spo2));
  }
  // 메시지의 마지막 표본이 PC에 닿은 시각이 수신 시각입니다. 앞 표본들은 시계 맞춤식으로 시각을 정합니다.
  record_.anchors.push_back({Timestamp{(first_index + count - 1) * kSampleUs}, host_time});
}

void PpgShotRecord::AddEvent(const record::PpgEvent& event) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!closed_) record_.events.push_back(event);
}

void PpgShotRecord::SetCounts(const PpgParserCounts& counts) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!closed_) record_.counts = counts;
}

void PpgShotRecord::MarkDisconnected(Timestamp at, const std::string& why) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_ || record_.disconnected_at) return;
  record_.disconnected_at = at;
  record_.disconnect_reason = why;
}

void PpgShotRecord::SetDeviceInfo(const std::string& info) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!closed_) record_.device_info = info;
}

PpgLink::PpgLink(std::function<int64_t()> clock)
    : clock_(clock ? std::move(clock) : std::function<int64_t()>(SteadyNowUs)), buffer_(kReadBytes) {}

PpgLink::~PpgLink() { Close(); }

bool PpgLink::Open(const SerialPortOpener& opener, const std::string& port, std::string* error) {
  Close();
  failure_.clear();
  port_name_ = port;
  SerialConfig config;
  config.baud_rate = 115200;
  std::string why;
  port_ = opener(port, config, &why);
  if (!port_) {
    failure_ = why.empty() ? "PPG 포트를 열지 못했습니다: " + port : why;
    if (error != nullptr) *error = failure_;
    return false;
  }
  parser_ = PpgParser();
  reading_ = PpgReading{};
  reading_.open = true;
  found_ = false;
  asked_after_found_ = false;
  device_info_.clear();
  open_us_ = clock_();
  info_us_ = 0;
  return true;
}

void PpgLink::AskDeviceInfo() {
  if (!IsOpen() || !device_info_.empty()) return;
  const int64_t now = clock_();
  if (found_) {
    if (asked_after_found_) return;
    asked_after_found_ = true;
  } else if (info_us_ != 0 && now - info_us_ < kPpgInfoIntervalUs) {
    return;
  }
  info_us_ = now;
  Send(BuildPpgInfoRequest(), nullptr);
}

int64_t PpgLink::OpenUs() const { return IsOpen() ? clock_() - open_us_ : 0; }

void PpgLink::Close(const std::string& why) {
  if (!port_) return;
  // 장치는 중지 명령을 받지 않으면 미리보기와 측정을 끝까지 이어가므로 닫기 전에 멈춥니다.
  if (started_ && port_->IsOpen()) port_->Write(BuildPpgStop(), kWriteTimeoutMs);
  if (record_) record_->MarkDisconnected(Timestamp{clock_()}, why);
  port_->Close();
  port_.reset();
  started_ = false;
  reading_ = PpgReading{};
  recent_.clear();
}

bool PpgLink::Restart(std::string* error, const PpgStartRequest& request) {
  if (!IsOpen()) {
    if (error != nullptr) *error = "PPG 포트가 닫혀 있습니다";
    return false;
  }
  const auto start = BuildPpgStart(request, error);
  if (!start) return false;
  const int64_t now = clock_();
  started_ = true;
  next_index_ = 0;
  start_us_ = now;
  restart_us_ = now;
  last_measure_us_ = 0;
  last_heart_rate_ = 0;
  last_heart_rate_us_ = 0;
  recent_.clear();
  reading_.samples = 0;
  reading_.status = 0;
  reading_.device_error = 0;
  reading_.problem.clear();
  // 다른 연결이 장치를 측정 중으로 남겼으면 중지 없는 시작은 거부되므로 중지를 늘 먼저 보냅니다.
  return Send(BuildPpgStop(), error) && Send(*start, error);
}

bool PpgLink::Pump(int32_t timeout_ms) {
  if (!port_) return false;
  std::optional<size_t> n;
  if (port_->IsOpen()) n = port_->Read(buffer_, timeout_ms);
  if (!n) {
    failure_ = reading_.problem.empty() ? "PPG 포트를 읽지 못해 닫았습니다" : reading_.problem;
    if (record_) record_->MarkDisconnected(Timestamp{clock_()}, failure_);
    port_->Close();
    started_ = false;
    reading_.open = false;
    return false;
  }
  if (*n == 0) return true;
  const int64_t now = clock_();
  for (const PpgMessage& m : parser_.Feed(std::span<const uint8_t>(buffer_.data(), *n), now)) Handle(m);
  reading_.counts = parser_.Counts();
  if (record_) record_->SetCounts(Minus(parser_.Counts(), record_counts_from_));
  return true;
}

bool PpgLink::RestartIfStopped() {
  if (record_ || !IsOpen() || !started_) return false;
  if (!PpgNeedsRestart(Reading(), clock_() - restart_us_)) return false;
  // 손가락 없이 미리보기가 끝나면 장치가 멈추고(오류 'f') 시작 명령을 다시 받아야 돌아옵니다.
  Restart();
  return true;
}

void PpgLink::Attach(std::shared_ptr<PpgShotRecord> record) {
  record_ = std::move(record);
  record_counts_from_ = parser_.Counts();
  if (record_ && !device_info_.empty()) record_->SetDeviceInfo(device_info_);
}

void PpgLink::Detach() { record_.reset(); }

bool PpgLink::IsOpen() const { return port_ && port_->IsOpen(); }

PpgReading PpgLink::Reading() const {
  const int64_t now = clock_();
  PpgReading r = reading_;
  r.open = IsOpen();
  r.streaming = started_ && last_measure_us_ > 0 && now - last_measure_us_ <= kStaleUs;
  if (started_) r.quiet_us = now - (last_measure_us_ > 0 ? last_measure_us_ : start_us_);
  const bool fresh = last_heart_rate_us_ > 0 && now - last_heart_rate_us_ <= kHeartRateHoldUs;
  r.heart_rate = fresh ? last_heart_rate_ : 0;
  return r;
}

PpgSnapshot PpgLink::Snapshot() const {
  PpgSnapshot s;
  if (!IsOpen()) return s;
  const PpgReading r = Reading();
  s.port = port_name_;
  if (!found_) {
    s.state = OpenUs() < kPpgPreparingAfterUs ? PpgState::kChecking : PpgState::kPreparing;
  } else if (r.streaming) {
    s.state = PpgState::kStreaming;
  } else {
    s.state = started_ && last_measure_us_ > 0 ? PpgState::kStalled : PpgState::kWaiting;
  }
  s.heart_rate = r.heart_rate;
  s.spo2 = r.spo2;
  s.measure_error = r.measure_error;
  s.device_error = r.device_error;
  s.problem = r.problem;
  return s;
}

std::vector<uint16_t> PpgLink::RecentValues(size_t count) const {
  const size_t n = std::min(count, recent_.size());
  return std::vector<uint16_t>(recent_.end() - static_cast<std::ptrdiff_t>(n), recent_.end());
}

void PpgLink::Handle(const PpgMessage& m) {
  if (m.source != kPpgFromDevice) return;
  // CRC까지 맞는 장치 메시지라 PPG 장치로 봅니다. 기존 앱도 정보 응답, 상태, 측정 데이터 중 하나를 받으면 시작했습니다.
  found_ = true;
  switch (m.code) {
    case kPpgInfo:
      device_info_.assign(m.data.begin(), m.data.end());
      if (record_) record_->SetDeviceInfo(device_info_);
      break;
    case kPpgStatus:
      if (m.data.empty()) break;
      reading_.status = m.data[0];
      if (record_) record_->AddEvent({Timestamp{m.host_time_us}, m.code, m.data[0]});
      break;
    case kPpgError:
      if (m.data.empty()) break;
      reading_.device_error = m.data[0];
      if (record_) record_->AddEvent({Timestamp{m.host_time_us}, m.code, m.data[0]});
      break;
    case kPpgMeasure: {
      const auto measure = DecodePpgMeasure(m.data);
      if (!measure || !started_) break;
      if (measure->heart_rate > 0) {
        last_heart_rate_ = measure->heart_rate;
        last_heart_rate_us_ = m.host_time_us;
      }
      reading_.spo2 = measure->spo2;
      reading_.measure_error = measure->error;
      last_measure_us_ = m.host_time_us;
      recent_.insert(recent_.end(), measure->samples.begin(), measure->samples.end());
      while (recent_.size() > kRecentCapacity) recent_.pop_front();
      if (record_) record_->AddMeasure(*measure, next_index_, Timestamp{m.host_time_us});
      next_index_ += static_cast<int64_t>(measure->samples.size());
      reading_.samples = next_index_;
      break;
    }
    default:
      break;
  }
}

bool PpgLink::Send(const std::vector<uint8_t>& bytes, std::string* error) {
  if (port_ && port_->Write(bytes, kWriteTimeoutMs)) return true;
  const std::string why = "PPG 장치에 명령을 보내지 못했습니다";
  if (error != nullptr) *error = why;
  reading_.problem = why;
  return false;
}

std::vector<double> PpgWavePoints(std::span<const uint16_t> values) {
  std::vector<double> points;
  const size_t groups = values.size() / kPpgWaveGroup;
  if (groups < 2) return points;
  const size_t skip = values.size() - groups * kPpgWaveGroup;
  std::vector<double> average(groups);
  for (size_t g = 0; g < groups; ++g) {
    double sum = 0;
    for (size_t k = 0; k < kPpgWaveGroup; ++k) sum += values[skip + g * kPpgWaveGroup + k];
    average[g] = sum / static_cast<double>(kPpgWaveGroup);
  }
  std::vector<double> sorted = average;
  std::sort(sorted.begin(), sorted.end());
  const double low = sorted[(groups - 1) / 100];
  const double high = sorted[(groups - 1) * 99 / 100];
  const double span = std::max(high - low, 1.0);
  points.reserve(groups);
  for (const double v : average) points.push_back(std::clamp((v - low) / span, 0.0, 1.0));
  return points;
}

struct PpgSensor::Shared {
  enum class Kind { kConnect, kDisconnect, kRefreshPorts, kBeginShot, kEndShot, kQuit };
  struct Command {
    Kind kind = Kind::kQuit;
    std::string port;
    std::shared_ptr<PpgShotRecord> record;
    int64_t posted_us = 0;
  };

  SerialPortOpener opener;
  std::function<std::vector<SerialPortInfo>()> list_ports;
  std::function<int64_t()> clock;

  std::mutex mutex;
  std::condition_variable changed;  // 명령이 들어옴, 장치 스레드가 끝남
  std::deque<Command> commands;
  SerialPort* port = nullptr;  // 장치 스레드가 연 포트. 명령을 넣는 쪽이 이 잠금 아래에서 WakeRead를 부름
  bool finished = false;

  std::mutex callback_mutex;  // 소멸자가 콜백을 지운 뒤로는 부르지 않게 함
  Callbacks callbacks;

  std::atomic<int64_t> loop_us{0};

  void Post(Command command) {
    command.posted_us = clock();
    {
      std::lock_guard<std::mutex> lock(mutex);
      commands.push_back(std::move(command));
      if (port != nullptr) port->WakeRead();
    }
    changed.notify_all();
  }

  void SetPort(SerialPort* opened) {
    std::lock_guard<std::mutex> lock(mutex);
    port = opened;
  }

  template <typename Fn>
  void Call(Fn&& fn) {
    std::lock_guard<std::mutex> lock(callback_mutex);
    fn(callbacks);
  }

  void Run();
};

void PpgSensor::Shared::Run() {
  PpgLink link(clock);
  std::string failure;      // 포트를 열지 못했거나 읽기가 실패한 이유. 연결과 끊기 명령에서 지움
  std::string failed_port;
  std::optional<PpgSnapshot> sent;
  int64_t wave_us = 0;
  int64_t wave_samples = -1;
  bool wave_shown = false;

  const auto send = [&](const PpgSnapshot& snapshot) {
    if (sent && *sent == snapshot) return;
    sent = snapshot;
    Call([&](Callbacks& c) {
      if (c.state) c.state(snapshot);
    });
  };
  const auto publish = [&] {
    PpgSnapshot snapshot = link.Snapshot();
    if (!link.IsOpen() && !failure.empty()) {
      snapshot.state = PpgState::kFailed;
      snapshot.port = failed_port;
      snapshot.problem = failure;
    }
    send(snapshot);
  };
  // 명령을 넣는 쪽이 닫힌 포트의 WakeRead를 부르지 않도록 포트를 닫기 전에 알려 둔 포트를 지웁니다.
  const auto close = [&](const std::string& why) {
    SetPort(nullptr);
    link.Close(why);
  };

  publish();
  for (;;) {
    std::deque<Command> todo;
    {
      std::unique_lock<std::mutex> lock(mutex);
      if (!link.IsOpen()) changed.wait(lock, [&] { return !commands.empty(); });
      todo.swap(commands);
    }
    loop_us.store(clock());
    for (Command& c : todo) {
      switch (c.kind) {
        case Kind::kQuit:
          close("앱을 닫았습니다");
          {
            std::lock_guard<std::mutex> lock(mutex);
            finished = true;
          }
          changed.notify_all();
          return;
        case Kind::kConnect: {
          close("PPG 포트를 바꿨습니다");
          failure.clear();
          PpgSnapshot opening;
          opening.state = PpgState::kOpening;
          opening.port = c.port;
          send(opening);
          std::string why;
          if (link.Open(opener, c.port, &why)) {
            SetPort(link.Port());
            link.AskDeviceInfo();
            // 준비된 장치는 첫 읽기에서 바로 답하므로 읽기 전에 확인 중을 보내야 화면이 그 단계를 거칩니다.
            publish();
          } else {
            failure = why;
            failed_port = c.port;
          }
          break;
        }
        case Kind::kDisconnect:
          close("PPG 연결을 끊었습니다");
          failure.clear();
          break;
        case Kind::kRefreshPorts:
          if (list_ports) {
            std::vector<SerialPortInfo> infos = list_ports();
            Call([&](Callbacks& cb) {
              if (cb.ports) cb.ports(std::move(infos));
            });
          }
          break;
        case Kind::kBeginShot:
          if (!c.record) break;
          if (!link.IsOpen()) {
            c.record->MarkDisconnected(Timestamp{clock()}, failure.empty() ? "PPG 포트가 닫혀 있었습니다" : failure);
            break;
          }
          if (link.Found()) link.Restart();
          link.Attach(std::move(c.record));
          break;
        case Kind::kEndShot:
          link.Detach();
          if (link.IsOpen() && link.Found()) link.Restart();
          break;
      }
      loop_us.store(clock());
    }
    if (link.IsOpen()) {
      if (!link.Pump(kReadTimeoutMs)) {
        failure = link.Failure();
        failed_port = link.PortName();
        close(failure);
      } else if (!link.Found() && link.OpenUs() >= kPpgFindTimeoutUs) {
        failure = kNotFound;
        failed_port = link.PortName();
        close(failure);
      } else {
        link.AskDeviceInfo();
        // 녹화 중에 장치를 확인하면 기록 중인 파형을 바꾸지 않도록 촬영을 끝낼 때(kEndShot) 시작합니다.
        if (link.Found() && !link.Started() && !link.Attached()) link.Restart();
        link.RestartIfStopped();
      }
    }
    loop_us.store(clock());
    publish();

    const PpgReading reading = link.Reading();
    const int64_t now = clock();
    if (reading.streaming) {
      if (reading.samples != wave_samples && now - wave_us >= kWaveIntervalUs) {
        std::vector<double> points = PpgWavePoints(link.RecentValues(kPpgWaveSamples));
        wave_us = now;
        wave_samples = reading.samples;
        wave_shown = true;
        Call([&](Callbacks& c) {
          if (c.wave) c.wave(std::move(points));
        });
      }
    } else if (wave_shown) {
      wave_shown = false;
      wave_samples = -1;
      Call([&](Callbacks& c) {
        if (c.wave) c.wave({});
      });
    }
  }
}

PpgSensor::PpgSensor(SerialPortOpener opener, std::function<std::vector<SerialPortInfo>()> list_ports,
                     Callbacks callbacks, std::function<int64_t()> clock)
    : shared_(std::make_shared<Shared>()) {
  shared_->opener = std::move(opener);
  shared_->list_ports = std::move(list_ports);
  shared_->callbacks = std::move(callbacks);
  shared_->clock = clock ? std::move(clock) : std::function<int64_t()>(SteadyNowUs);
  shared_->loop_us.store(shared_->clock());
  thread_ = std::thread([shared = shared_] { shared->Run(); });
}

PpgSensor::~PpgSensor() {
  {
    std::lock_guard<std::mutex> lock(shared_->callback_mutex);
    shared_->callbacks = {};
  }
  shared_->Post({Shared::Kind::kQuit});
  std::unique_lock<std::mutex> lock(shared_->mutex);
  const bool done = shared_->changed.wait_for(lock, std::chrono::microseconds(kQuitWaitUs),
                                              [this] { return shared_->finished; });
  lock.unlock();
  // 직렬 드라이버가 멈춰 장치 스레드가 돌아오지 않아도 앱이 끝나도록 놓아 둡니다. 장치 스레드는 shared_를 함께 가집니다.
  if (done) {
    thread_.join();
  } else {
    thread_.detach();
  }
}

void PpgSensor::Connect(const std::string& port) { shared_->Post({Shared::Kind::kConnect, port}); }

void PpgSensor::Disconnect() { shared_->Post({Shared::Kind::kDisconnect}); }

void PpgSensor::RefreshPorts() { shared_->Post({Shared::Kind::kRefreshPorts}); }

void PpgSensor::BeginShot(std::shared_ptr<PpgShotRecord> record) {
  shared_->Post({Shared::Kind::kBeginShot, {}, std::move(record)});
}

void PpgSensor::EndShot() { shared_->Post({Shared::Kind::kEndShot}); }

int64_t PpgSensor::LastLoopUs() const { return shared_->loop_us.load(); }

int64_t PpgSensor::PendingSinceUs() const {
  std::lock_guard<std::mutex> lock(shared_->mutex);
  return shared_->commands.empty() ? 0 : shared_->commands.front().posted_us;
}

bool PpgNeedsRestart(const PpgReading& reading, int64_t since_restart_us) {
  if (!reading.open || since_restart_us < kPpgRestartIntervalUs) return false;
  return reading.status == kPpgStatusMeasuring || reading.status == kPpgStatusFinished ||
         reading.quiet_us > kPpgRestartQuietUs;
}


namespace {

constexpr char kPpgCsvHeader[] = "common_time_us,value,measure_error,heart_rate,spo2";

}  // namespace

bool WritePpgCsv(const std::string& path, const PpgRecord& record, std::string* error) {
  std::FILE* file = OpenFileUtf8(path, "wb");
  if (file == nullptr) {
    if (error != nullptr) *error = "PPG 파일을 만들지 못했습니다: " + path;
    return false;
  }
  std::fprintf(file, "%s\n", kPpgCsvHeader);
  const size_t n = record.values.size();
  for (size_t i = 0; i < n; ++i) {
    std::fprintf(file, "%lld,%u,%u,%u,%u\n", static_cast<long long>(record.common_time[i].us),
                 static_cast<unsigned>(record.values[i]), static_cast<unsigned>(record.measure_error[i]),
                 static_cast<unsigned>(record.heart_rate[i]), static_cast<unsigned>(record.spo2[i]));
  }
  const bool ok = std::fflush(file) == 0 && std::ferror(file) == 0;
  std::fclose(file);
  if (!ok && error != nullptr) *error = "PPG 파일을 다 쓰지 못했습니다: " + path;
  return ok;
}

bool WritePpgSidecar(const std::string& path, const PpgRecord& record, const std::string& samples_file,
                     const std::string& port, const std::optional<hwlab::record::PpgErrorWindow>& rgb_window,
                     std::string* error) {
  hwlab::record::PpgSidecar s;
  s.samples_file = samples_file;
  s.port = port;
  if (!record.device_info.empty()) s.device_info = record.device_info;
  s.sample_rate_hz = kPpgSampleHz;
  s.record_begin = record.begin_time;
  s.record_end = record.end_time;
  s.clock = record.clock;
  s.samples = static_cast<int64_t>(record.values.size());
  s.messages = static_cast<int64_t>(record.anchors.size());
  if (!record.common_time.empty()) {
    s.first_sample = record.common_time.front();
    s.last_sample = record.common_time.back();
  }
  s.median_interval_us = timing::MedianIntervalUs(record.common_time);
  s.gaps = timing::FindFrameGaps(record.common_time, kSampleUs);
  for (const uint8_t e : record.measure_error) s.measure_error_samples += e != 0 ? 1 : 0;
  s.crc_failures = record.counts.crc_failures;
  s.bad_lengths = record.counts.bad_lengths;
  s.implausible = record.counts.implausible;
  s.skipped_bytes = record.counts.skipped_bytes;
  s.events = record.events;
  s.error_spans = hwlab::record::FindPpgErrorSpans(record.common_time, record.measure_error);
  s.rgb_window = rgb_window;
  const std::string json = hwlab::record::BuildPpgSidecarJson(s);
  std::FILE* file = OpenFileUtf8(path, "wb");
  if (file == nullptr) {
    if (error != nullptr) *error = "PPG 사이드카를 만들지 못했습니다: " + path;
    return false;
  }
  bool ok = std::fwrite(json.data(), 1, json.size(), file) == json.size();
  ok = std::fclose(file) == 0 && ok;
  if (!ok && error != nullptr) *error = "PPG 사이드카를 다 쓰지 못했습니다: " + path;
  return ok;
}

std::string PpgErrorShareText(const hwlab::record::PpgErrorShare& share, int64_t window_samples) {
  const double seconds = static_cast<double>(share.samples) / kPpgSampleHz;
  const double percent = window_samples > 0 ? 100.0 * static_cast<double>(share.samples) / window_samples : 0;
  char amount[64];
  // 1% 아래를 반올림해 0%로 쓰면 오류가 없었던 것처럼 읽히므로 소수점 아래 한 자리까지 씁니다.
  std::snprintf(amount, sizeof(amount), percent < 1 ? "%.1f초(%.1f%%" : "%.1f초(%.0f%%", seconds, percent);
  std::string text = PpgMeasureErrorText(share.code) + " " + amount;
  if (share.runs > 1) text += ", " + std::to_string(share.runs) + "번";
  return text + ")";
}

std::optional<PpgCsv> ReadPpgCsv(const std::string& path, std::string* error) {
  const auto fail = [&](const std::string& why) -> std::optional<PpgCsv> {
    if (error != nullptr) *error = why;
    return std::nullopt;
  };
  std::FILE* file = OpenFileUtf8(path, "rb");
  if (file == nullptr) return fail("PPG 파일이 없습니다: " + path);
  std::string text;
  char buffer[8192];
  size_t got = 0;
  while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) text.append(buffer, got);
  std::fclose(file);
  PpgCsv out;
  size_t pos = 0;
  bool header = true;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(pos, end - pos);
    pos = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    if (header) {
      if (line != kPpgCsvHeader) return fail("PPG 파일의 열 이름이 다릅니다: " + line);
      header = false;
      continue;
    }
    long long t = 0;
    unsigned v = 0, e = 0, h = 0, o = 0;
    if (std::sscanf(line.c_str(), "%lld,%u,%u,%u,%u", &t, &v, &e, &h, &o) != 5) return fail("PPG 파일에 읽을 수 없는 줄: " + line);
    out.common_time_us.push_back(t);
    out.value.push_back(static_cast<int32_t>(v));
    out.measure_error.push_back(static_cast<int32_t>(e));
    out.heart_rate.push_back(static_cast<int32_t>(h));
    out.spo2.push_back(static_cast<int32_t>(o));
  }
  if (header) return fail("PPG 파일에 열 이름 줄이 없습니다: " + path);
  return out;
}

}  // namespace hwlab::platform
