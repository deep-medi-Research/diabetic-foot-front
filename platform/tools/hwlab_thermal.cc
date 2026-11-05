// 열화상 카메라 실기 확인 도구입니다. 정한 시간 동안 받아 프레임 간격과 누락을 재고, raw16으로 쓴 뒤 다시 읽어
// 받은 바이트와 같은지 확인합니다. 받은 프레임 하나를 sample.raw16으로 남깁니다.
// 프레임마다 전체 평균 원시값, 가운데 영역 평균, 공간 표준편차를 frames.csv에, 셔터 교정 명령의
// 시작과 끝을 events.csv에 남겨 자동 셔터 교정의 간격과 교정 뒤 원시값 변화를 잽니다.
//
//   hwlab_thermal <출력 폴더> [초=60] [--keep] [--calibrate-at 초,초...] [--hold-at 초] [--release-at 초]
//
// --calibrate-at: 첫 프레임 뒤 그 시각에 다른 스레드에서 CalibrateNow를 부릅니다(수신 중 교정)
// --hold-at, --release-at: 그 시각에 HoldAutoCalibration(true), (false)를 부릅니다. 끝날 때 멈춘 상태면 되돌립니다
// --reopen: 측정을 마치고 닫은 뒤 다시 열어 자동 셔터 교정 설정을 읽고 닫습니다(설정 복원 확인)
// --discard N: 셔터 교정 뒤 버릴 새 프레임 수를 N으로 바꿉니다(기본은 장치 기본값). 교정 직후 값 변화를 잴 때 0으로 둡니다
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "hwlab/platform/thermal_camera.h"
#include "hwlab/platform/thermal_writer.h"
#include "hwlab/timing/frame_gap.h"

namespace {

using Clock = std::chrono::steady_clock;

// 수신과 쓰기 사이의 버퍼 수입니다. 30fps에서 약 2초 분량이라 디스크가 잠깐 멈춰도 수신 스레드가 기다리지 않습니다.
constexpr size_t kPoolFrames = 64;

uint64_t Fnv1a(const uint8_t* data, size_t size) {
  uint64_t hash = 1469598103934665603ull;
  for (size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

double Ms(int64_t us) { return static_cast<double>(us) / 1000.0; }

double MsSince(Clock::time_point from, Clock::time_point to) {
  return std::chrono::duration<double, std::milli>(to - from).count();
}

struct Slot {
  std::vector<uint8_t> bytes;
};

// 프레임 하나의 측정값입니다. 수신 스레드에서 화소를 한 번 훑어 구합니다.
struct FrameStat {
  double t_s = 0;        // 첫 프레임부터의 시각
  double gap_ms = 0;     // 앞 프레임과의 수신 간격
  double mean = 0;       // 전체 화소 평균
  double center = 0;     // 가운데 1/4 영역(가로 세로 절반) 평균
  double spread = 0;     // 4화소 간격 표본의 공간 표준편차
};

struct Event {
  double t_s = 0;
  std::string what;
  double took_ms = 0;
  bool ok = false;
  std::string error;
  std::optional<hwlab::pixel::CalibrationWatch> watch;
};

FrameStat Measure(const hwlab::FrameView& view) {
  FrameStat stat;
  const int32_t w = view.width;
  const int32_t h = view.height;
  double sum = 0;
  double center_sum = 0;
  int64_t center_count = 0;
  double sample_sum = 0;
  double sample_sq = 0;
  int64_t sample_count = 0;
  for (int32_t y = 0; y < h; ++y) {
    const uint8_t* row = view.plane[0] + static_cast<ptrdiff_t>(y) * view.row_stride[0];
    const bool center_row = y >= h / 4 && y < h * 3 / 4;
    for (int32_t x = 0; x < w; ++x) {
      const double v = static_cast<double>(row[x * 2] | (row[x * 2 + 1] << 8));
      sum += v;
      if (center_row && x >= w / 4 && x < w * 3 / 4) {
        center_sum += v;
        ++center_count;
      }
      if ((x & 3) == 0 && (y & 3) == 0) {
        sample_sum += v;
        sample_sq += v * v;
        ++sample_count;
      }
    }
  }
  const double n = static_cast<double>(w) * static_cast<double>(h);
  stat.mean = n > 0 ? sum / n : 0;
  stat.center = center_count > 0 ? center_sum / static_cast<double>(center_count) : 0;
  if (sample_count > 1) {
    const double m = sample_sum / static_cast<double>(sample_count);
    stat.spread = std::sqrt(std::max(0.0, sample_sq / static_cast<double>(sample_count) - m * m));
  }
  return stat;
}

std::vector<double> ParseSeconds(const char* text) {
  std::vector<double> out;
  std::string s = text;
  size_t begin = 0;
  while (begin <= s.size()) {
    const size_t comma = s.find(',', begin);
    const std::string part = s.substr(begin, comma == std::string::npos ? std::string::npos : comma - begin);
    if (!part.empty()) out.push_back(std::atof(part.c_str()));
    if (comma == std::string::npos) break;
    begin = comma + 1;
  }
  std::sort(out.begin(), out.end());
  return out;
}

// 수신 스레드가 채운 버퍼를 쓰기 스레드로 넘기고, 쓴 버퍼를 다시 수신 스레드로 돌려줍니다.
class SlotQueue {
 public:
  void Push(Slot slot) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      slots_.push_back(std::move(slot));
    }
    ready_.notify_one();
  }
  std::optional<Slot> TryPop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slots_.empty()) return std::nullopt;
    Slot slot = std::move(slots_.front());
    slots_.pop_front();
    return slot;
  }
  std::optional<Slot> WaitPop() {
    std::unique_lock<std::mutex> lock(mutex_);
    ready_.wait(lock, [this] { return !slots_.empty() || closed_; });
    if (slots_.empty()) return std::nullopt;
    Slot slot = std::move(slots_.front());
    slots_.pop_front();
    return slot;
  }
  void CloseQueue() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    ready_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<Slot> slots_;
  bool closed_ = false;
};

}  // namespace

int main(int argc, char** argv) {
  using namespace hwlab;
  if (argc < 2) {
    std::printf("사용법: hwlab_thermal <출력 폴더> [초=60] [--keep] [--calibrate-at 초,초...] [--hold-at 초] "
                "[--release-at 초]\n");
    return 2;
  }
  const std::filesystem::path folder = argv[1];
  const int seconds = argc > 2 && argv[2][0] != '-' ? std::atoi(argv[2]) : 60;
  bool keep = false;
  bool reopen = false;
  int discard = -1;
  std::vector<double> calibrate_at;
  double hold_at = -1;
  double release_at = -1;
  for (int i = 2; i < argc; ++i) {
    keep = keep || std::strcmp(argv[i], "--keep") == 0;
    reopen = reopen || std::strcmp(argv[i], "--reopen") == 0;
    if (i + 1 < argc && std::strcmp(argv[i], "--discard") == 0) discard = std::atoi(argv[i + 1]);
    if (i + 1 < argc && std::strcmp(argv[i], "--calibrate-at") == 0) calibrate_at = ParseSeconds(argv[i + 1]);
    if (i + 1 < argc && std::strcmp(argv[i], "--hold-at") == 0) hold_at = std::atof(argv[i + 1]);
    if (i + 1 < argc && std::strcmp(argv[i], "--release-at") == 0) release_at = std::atof(argv[i + 1]);
  }
  std::filesystem::create_directories(folder);
  const std::string raw_path = (folder / "thermal.raw16").string();
  const std::string sample_path = (folder / "sample.raw16").string();

  pixel::CalibrationRule rule = platform::kTeEv1CalibrationRule;
  if (discard >= 0) rule.discard_frames = discard;
  auto camera = platform::MakeDeviceThermalCamera(rule);
  if (!camera) {
    std::printf("이 OS에는 열화상 카메라 구현이 없습니다\n");
    return 1;
  }
  std::string error;
  const auto open_began = Clock::now();
  if (!camera->Open(&error)) {
    std::printf("열기 실패: %s\n", error.c_str());
    return 1;
  }
  const auto opened = Clock::now();
  const int32_t width = camera->Width();
  const int32_t height = camera->Height();
  const size_t frame_bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 2;
  std::printf("장치 %s, %dx%d, 열기 %.0fms\n", camera->DeviceName().c_str(), width, height, MsSince(open_began, opened));
  std::printf("판정 수치: 반복 %d, 기다림 %d장, 상한 %d장, 버림 %d장\n", rule.freeze_repeats, rule.freeze_wait_frames,
              rule.freeze_max_frames, rule.discard_frames);
  if (const auto c = camera->LastCalibration()) {
    std::printf("열 때 교정: 단계 %d, 받은 %d장, 교정 전 %d장, 같은 프레임 %d장, 버림 %d장\n", static_cast<int>(c->phase),
                c->received, c->waited_frames, c->frozen_frames, c->discarded);
  } else {
    std::printf("열 때 교정: 판정 없음\n");
  }
  const std::string setting_at_open = camera->AutoCalibrationSetting();
  std::printf("자동 셔터 교정 설정: %s\n", setting_at_open.empty() ? "(모름)" : setting_at_open.c_str());

  platform::Raw16File raw;
  if (!raw.Open(raw_path, width, height)) {
    std::printf("파일을 만들지 못했습니다: %s\n", raw_path.c_str());
    camera->Close();
    return 1;
  }

  SlotQueue free_slots;
  SlotQueue filled;
  for (size_t i = 0; i < kPoolFrames; ++i) free_slots.Push(Slot{std::vector<uint8_t>(frame_bytes)});

  std::vector<uint64_t> written_hashes;
  bool write_failed = false;
  std::thread writer([&] {
    while (auto slot = filled.WaitPop()) {
      FrameView view;
      view.format = PixelFormat::kY16;
      view.width = width;
      view.height = height;
      view.plane[0] = slot->bytes.data();
      view.row_stride[0] = width * 2;
      view.stream = StreamKind::kThermal;
      if (!raw.WriteFrame(view)) write_failed = true;
      written_hashes.push_back(Fnv1a(slot->bytes.data(), slot->bytes.size()));
      free_slots.Push(std::move(*slot));
    }
  });

  // 수신 스레드(이 스레드)는 복사만 하고 디스크에는 쓰지 않습니다.
  std::vector<Timestamp> times;
  std::vector<uint8_t> sample;
  size_t sample_index = 0;  // 견본 프레임이 파일에서 몇 번째인지
  size_t pushed = 0;
  int64_t store_dropped = 0;
  std::optional<Clock::time_point> first_frame;
  platform::ThermalConnection end_state = platform::ThermalConnection::kReceiving;
  std::vector<FrameStat> stats;
  stats.reserve(static_cast<size_t>(seconds) * 40 + 100);

  // 셔터 명령은 앱의 제어 스레드처럼 수신 스레드 밖에서 부릅니다.
  std::mutex event_mutex;
  std::vector<Event> events;
  std::atomic<int64_t> first_frame_ns{0};
  std::atomic<bool> receiving_done{false};
  struct Planned {
    double t_s;
    int kind;  // 0 교정, 1 멈춤, 2 되돌림
  };
  std::vector<Planned> plan;
  for (double t : calibrate_at) plan.push_back({t, 0});
  if (hold_at >= 0) plan.push_back({hold_at, 1});
  if (release_at >= 0) plan.push_back({release_at, 2});
  std::sort(plan.begin(), plan.end(), [](const Planned& a, const Planned& b) { return a.t_s < b.t_s; });
  auto since_first_s = [&](Clock::time_point at) {
    const int64_t first = first_frame_ns.load();
    return first == 0 ? 0.0 : static_cast<double>(at.time_since_epoch().count() - first) / 1e9;
  };
  std::thread commander([&] {
    for (const Planned& p : plan) {
      while (!receiving_done.load() && (first_frame_ns.load() == 0 || since_first_s(Clock::now()) < p.t_s)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      if (receiving_done.load()) return;
      Event e;
      const auto began = Clock::now();
      e.t_s = since_first_s(began);
      if (p.kind == 0) {
        e.what = "calibrate";
        e.ok = camera->CalibrateNow(&e.error);
        e.watch = camera->LastCalibration();
      } else {
        e.what = p.kind == 1 ? "hold" : "release";
        e.ok = camera->HoldAutoCalibration(p.kind == 1, &e.error);
      }
      e.took_ms = MsSince(began, Clock::now());
      std::lock_guard<std::mutex> lock(event_mutex);
      events.push_back(e);
    }
  });

  std::optional<Clock::time_point> previous_frame;
  for (;;) {
    const auto frame = camera->ReadFrame(100'000);
    const auto now = Clock::now();
    if (frame) {
      if (!first_frame) {
        first_frame = now;
        first_frame_ns.store(static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count()));
      }
      FrameStat stat = Measure(frame->view);
      stat.t_s = MsSince(*first_frame, now) / 1000.0;
      stat.gap_ms = previous_frame ? MsSince(*previous_frame, now) : 0.0;
      stats.push_back(stat);
      previous_frame = now;
      times.push_back(frame->host_time);
      if (auto slot = free_slots.TryPop()) {
        for (int32_t y = 0; y < height; ++y) {
          std::memcpy(slot->bytes.data() + static_cast<size_t>(y) * width * 2,
                      frame->view.plane[0] + static_cast<size_t>(y) * frame->view.row_stride[0],
                      static_cast<size_t>(width) * 2);
        }
        if (sample.empty() && times.size() >= 30) {
          sample = slot->bytes;
          sample_index = pushed;
        }
        filled.Push(std::move(*slot));
        ++pushed;
      } else {
        ++store_dropped;
      }
    }
    const auto state = camera->Connection();
    if (state == platform::ThermalConnection::kLost) {
      end_state = state;
      break;
    }
    if (first_frame && now - *first_frame >= std::chrono::seconds(seconds)) break;
  }
  receiving_done.store(true);
  commander.join();
  const std::string setting_at_end = camera->AutoCalibrationSetting();
  camera->Close();
  if (reopen) {
    std::string why;
    if (camera->Open(&why)) {
      const std::string again = camera->AutoCalibrationSetting();
      std::printf("다시 연 뒤 자동 셔터 교정 설정: %s (처음 연 때와 %s)\n", again.c_str(),
                  again == setting_at_open ? "같음" : "다름");
      camera->Close();
    } else {
      std::printf("다시 열기 실패: %s\n", why.c_str());
    }
  }
  {
    // 측정값은 수신이 끝난 뒤에 써서 수신 간격에 디스크 쓰기가 들어가지 않게 합니다.
    std::ofstream csv(folder / "frames.csv", std::ios::trunc);
    csv << "frame,t_s,gap_ms,mean,center,spread\n";
    char line[200];
    for (size_t i = 0; i < stats.size(); ++i) {
      const FrameStat& f = stats[i];
      std::snprintf(line, sizeof(line), "%zu,%.4f,%.2f,%.3f,%.3f,%.3f\n", i, f.t_s, f.gap_ms, f.mean, f.center,
                    f.spread);
      csv << line;
    }
    std::ofstream ev(folder / "events.csv", std::ios::trunc);
    ev << "t_s,what,ok,took_ms,phase,waited,frozen,discarded,error\n";
    for (const Event& e : events) {
      std::snprintf(line, sizeof(line), "%.4f,%s,%d,%.1f,%d,%d,%d,%d,", e.t_s, e.what.c_str(), e.ok ? 1 : 0, e.took_ms,
                    e.watch ? static_cast<int>(e.watch->phase) : -1, e.watch ? e.watch->waited_frames : -1,
                    e.watch ? e.watch->frozen_frames : -1, e.watch ? e.watch->discarded : -1);
      ev << line << e.error << "\n";
      std::printf("명령 %s %.2f초: %s, %.0fms %s\n", e.what.c_str(), e.t_s, e.ok ? "성공" : "실패", e.took_ms,
                  e.error.c_str());
    }
    std::printf("끝날 때 자동 셔터 교정 설정(닫기 전): %s\n", setting_at_end.empty() ? "(모름)" : setting_at_end.c_str());
    // 셔터 교정 중에는 SDK가 같은 화소의 프레임을 계속 주므로(실측 43장) 평균이 그대로인 연속 구간을 셉니다.
    for (size_t i = 1, run = 0; i <= stats.size(); ++i) {
      if (i < stats.size() && stats[i].mean == stats[i - 1].mean) {
        ++run;
        continue;
      }
      if (run >= 5) std::printf("  같은 프레임 %zu장: %.2f~%.2f초\n", run + 1, stats[i - 1 - run].t_s, stats[i - 1].t_s);
      run = 0;
    }
    int stalls = 0;
    for (const FrameStat& f : stats) {
      if (f.gap_ms <= 150.0) continue;
      ++stalls;
      std::printf("  수신 멈춤 %.2f초에 끝남, %.0fms\n", f.t_s, f.gap_ms);
    }
    std::printf("수신 멈춤(150ms 초과) %d곳. frames.csv, events.csv를 썼습니다\n", stalls);
  }
  filled.CloseQueue();
  writer.join();
  const bool closed_ok = raw.Close();

  if (!first_frame) {
    std::printf("프레임을 받지 못했습니다(연결 상태 kLost)\n");
    return 1;
  }
  std::printf("첫 프레임: 열기 시작부터 %.0fms, 열기가 끝난 뒤 %.0fms\n", MsSince(open_began, *first_frame),
              MsSince(opened, *first_frame));
  if (end_state == platform::ThermalConnection::kLost) std::printf("수신 중 연결 끊김 판정으로 멈췄습니다\n");

  // 간격 통계는 core의 누락 판정과 같은 함수로 잽니다.
  const auto gaps = timing::FindFrameGaps(times, platform::kThermalFramePeriodUs);
  const int64_t median_us = timing::MedianIntervalUs(times);
  int64_t min_us = 0;
  int64_t max_us = 0;
  for (size_t i = 1; i < times.size(); ++i) {
    const int64_t gap = IntervalUs(times[i - 1], times[i]);
    min_us = i == 1 ? gap : std::min(min_us, gap);
    max_us = i == 1 ? gap : std::max(max_us, gap);
  }
  const int64_t span_us = times.size() > 1 ? IntervalUs(times.front(), times.back()) : 0;
  std::printf("받은 프레임 %zu장, %.2f초, 평균 %.2ffps, 간격 중앙값 %.2fms(%.2ffps), 최소 %.2fms, 최대 %.2fms\n",
              times.size(), static_cast<double>(span_us) / 1e6,
              span_us > 0 ? static_cast<double>(times.size() - 1) * 1e6 / static_cast<double>(span_us) : 0.0,
              Ms(median_us), median_us > 0 ? 1e6 / static_cast<double>(median_us) : 0.0, Ms(min_us), Ms(max_us));
  std::printf("누락 자리 %zu곳, 누락 추정 %lld장(간격이 %.1fms를 넘는 곳)\n", gaps.size(),
              static_cast<long long>(timing::CountMissingFrames(gaps)), Ms(platform::kThermalFramePeriodUs * 3 / 2));
  for (size_t i = 0; i < gaps.size() && i < 10; ++i) {
    std::printf("  %zu번째 뒤 %.1fms(%lld장)\n", gaps[i].before, Ms(gaps[i].interval_us),
                static_cast<long long>(gaps[i].missing_frames));
  }
  std::printf("저장 누락 %lld장, 파일에 쓴 프레임 %lld장\n", static_cast<long long>(store_dropped),
              static_cast<long long>(raw.FramesWritten()));

  // 쓴 파일을 다시 읽어 프레임마다 받은 바이트의 해시와 비교하고, 견본 프레임은 바이트 단위로 비교합니다.
  bool match = closed_ok && !write_failed;
  int64_t mismatched = 0;
  {
    const auto size = std::filesystem::file_size(raw_path);
    if (size != frame_bytes * written_hashes.size()) {
      match = false;
      std::printf("파일 크기 %llu바이트가 %zu장 분량과 다릅니다\n", static_cast<unsigned long long>(size),
                  written_hashes.size());
    }
    std::ifstream in(raw_path, std::ios::binary);
    std::vector<uint8_t> buffer(frame_bytes);
    for (size_t i = 0; i < written_hashes.size(); ++i) {
      if (!in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(frame_bytes))) {
        match = false;
        break;
      }
      if (Fnv1a(buffer.data(), frame_bytes) != written_hashes[i]) ++mismatched;
      if (i == sample_index && !sample.empty() && std::memcmp(buffer.data(), sample.data(), frame_bytes) != 0) {
        ++mismatched;
      }
    }
  }
  match = match && mismatched == 0;
  std::printf("다시 읽기: %s(다른 프레임 %lld장)\n", match ? "일치" : "불일치", static_cast<long long>(mismatched));

  if (!sample.empty()) {
    std::ofstream out(sample_path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(sample.data()), static_cast<std::streamsize>(sample.size()));
    uint16_t lo = 0xFFFF;
    uint16_t hi = 0;
    for (size_t i = 0; i + 1 < sample.size(); i += 2) {
      const uint16_t v = static_cast<uint16_t>(sample[i] | (sample[i + 1] << 8));
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
    std::printf("견본 프레임(파일의 %zu번째) %s, 원시값 %u~%u\n", sample_index, sample_path.c_str(), static_cast<unsigned>(lo),
                static_cast<unsigned>(hi));
  }
  if (!keep) std::filesystem::remove(raw_path);
  return match && end_state != platform::ThermalConnection::kLost ? 0 : 1;
}
