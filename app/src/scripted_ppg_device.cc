#include "scripted_ppg_device.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kSamplesPerMessage = 24;
// 표본 24개(장치 시각 48ms)를 PC 시계로 48.24ms마다 냅니다. 장치 시계가 약 0.5% 느린 실측을 따릅니다.
constexpr auto kMessagePeriod = std::chrono::microseconds(48'240);
constexpr auto kFirstStartDelay = std::chrono::milliseconds(190);
constexpr auto kRestartDelay = std::chrono::milliseconds(30);
constexpr auto kRestartExtra = std::chrono::milliseconds(128);
constexpr auto kRestartWindow = std::chrono::seconds(1);
constexpr auto kSettle = std::chrono::seconds(4);
constexpr auto kSignalBadFrom = std::chrono::seconds(16);
constexpr auto kSignalBadUntil = std::chrono::seconds(18);

}  // namespace

ScriptedPpgDevice::ScriptedPpgDevice(std::shared_ptr<hwlab::platform::FakeSerialLine> line)
    : line_(line), device_(std::move(line)), thread_([this] { Run(); }) {}

ScriptedPpgDevice::~ScriptedPpgDevice() {
  stop_ = true;
  line_->StopWaiting();
  thread_.join();
}

void ScriptedPpgDevice::Run() {
  int32_t starts = 0;
  int32_t stops = 0;
  bool stopped_before = false;
  Clock::time_point last_stop;
  Clock::time_point started;
  Clock::time_point next_due;
  std::chrono::seconds preview(60);
  std::chrono::seconds measure(60);
  bool restart = false;
  bool measuring_sent = false;
  int64_t sent = 0;
  size_t seen = 0;
  while (!stop_) {
    // 측정 중이 아니면 명령이 올 때까지, 측정 중이면 다음 메시지나 상태를 낼 시각까지 기다립니다.
    Clock::time_point until = Clock::now() + std::chrono::hours(1);
    if (device_.Running()) {
      until = std::min({until, next_due, started + preview + measure});
      if (!measuring_sent) until = std::min(until, started + preview);
    }
    seen = line_->WaitForWrites(seen, until);
    if (stop_) break;
    const Clock::time_point now = Clock::now();
    const bool was_running = device_.Running();
    device_.HandleCommands();
    // 앱은 시작 전에 늘 중지를 보내므로, 측정 중이던 장치를 멈춘 경우만 실측의 "중지 직후 다시 시작"으로 봅니다.
    if (device_.StopCount() != stops && !was_running) stops = device_.StopCount();
    if (device_.StopCount() != stops) {
      stops = device_.StopCount();
      last_stop = now;
      stopped_before = true;
    }
    if (device_.StartCount() != starts) {
      starts = device_.StartCount();
      restart = stopped_before && now - last_stop < kRestartWindow;
      started = now;
      next_due = now + (restart ? kRestartDelay : kFirstStartDelay);
      const auto& data = device_.LastStartData();
      preview = std::chrono::seconds(data.size() > 0 ? data[0] : 60);
      measure = std::chrono::seconds(data.size() > 1 ? data[1] : 60);
      measuring_sent = false;
      sent = 0;
    }
    if (device_.Running() && !measuring_sent && now >= started + preview) {
      device_.EmitStatus(hwlab::platform::kPpgStatusMeasuring);
      measuring_sent = true;
    }
    if (device_.Running() && now >= started + preview + measure) {
      device_.EmitStatus(hwlab::platform::kPpgStatusFinished);
    }
    while (device_.Running() && now >= next_due) {
      const bool settled = now - started >= kSettle;
      const bool signal_bad = next_due - started >= kSignalBadFrom && next_due - started < kSignalBadUntil;
      device_.EmitMeasurement(kSamplesPerMessage, settled ? 72 : 0, settled ? 98 : 0, signal_bad ? 's' : 0);
      ++sent;
      next_due += kMessagePeriod;
      if (restart && sent == 1) next_due += kRestartExtra;
    }
  }
}
