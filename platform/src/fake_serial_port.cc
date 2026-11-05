#include "hwlab/platform/fake_serial_port.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace hwlab::platform {

std::vector<uint8_t> FakeSerialLine::Written() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<uint8_t> all;
  for (const auto& write : writes_) all.insert(all.end(), write.begin(), write.end());
  return all;
}

std::vector<std::vector<uint8_t>> FakeSerialLine::Writes() {
  std::lock_guard<std::mutex> lock(mutex_);
  return writes_;
}

void FakeSerialLine::Feed(const std::vector<uint8_t>& bytes) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    inbound_.insert(inbound_.end(), bytes.begin(), bytes.end());
  }
  arrived_.notify_all();
}

void FakeSerialLine::FailWritesAfter(int32_t writes, bool disconnect) {
  std::lock_guard<std::mutex> lock(mutex_);
  writes_before_failure_ = writes;
  failure_disconnects_ = disconnect;
}

void FakeSerialLine::Unplug() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    plugged_ = false;
  }
  arrived_.notify_all();
}

void FakeSerialLine::Plug() {
  std::lock_guard<std::mutex> lock(mutex_);
  plugged_ = true;
  writes_before_failure_ = -1;
}

int32_t FakeSerialLine::OpenCount() {
  std::lock_guard<std::mutex> lock(mutex_);
  return open_count_;
}

size_t FakeSerialLine::WaitForWrites(size_t seen, std::chrono::steady_clock::time_point until) {
  std::unique_lock<std::mutex> lock(mutex_);
  written_.wait_until(lock, until, [&] { return stop_waiting_ || writes_.size() > seen; });
  return writes_.size();
}

void FakeSerialLine::StopWaiting() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_waiting_ = true;
  }
  written_.notify_all();
}

void FakeSerialLine::HoldWrites() {
  std::lock_guard<std::mutex> lock(mutex_);
  hold_writes_ = true;
}

void FakeSerialLine::ReleaseWrites() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    hold_writes_ = false;
  }
  written_.notify_all();
}

FakeSerialPort::FakeSerialPort(std::string name, std::shared_ptr<FakeSerialLine> line)
    : name_(std::move(name)), line_(std::move(line)) {}

bool FakeSerialPort::IsOpen() const {
  std::lock_guard<std::mutex> lock(line_->mutex_);
  return open_;
}

bool FakeSerialPort::Write(std::span<const uint8_t> bytes, int32_t /*timeout_ms*/) {
  std::unique_lock<std::mutex> lock(line_->mutex_);
  // 멈춘 드라이버처럼 시간 제한과 상관없이 풀어 줄 때까지 돌아오지 않습니다.
  line_->written_.wait(lock, [this] { return !line_->hold_writes_; });
  if (!line_->plugged_) open_ = false;
  if (!open_) return false;
  if (line_->writes_before_failure_ == 0) {
    if (line_->failure_disconnects_) open_ = false;
    return false;
  }
  if (line_->writes_before_failure_ > 0) --line_->writes_before_failure_;
  line_->writes_.emplace_back(bytes.begin(), bytes.end());
  lock.unlock();
  line_->written_.notify_all();
  return true;
}

std::optional<size_t> FakeSerialPort::Read(std::span<uint8_t> buffer, int32_t timeout_ms) {
  std::unique_lock<std::mutex> lock(line_->mutex_);
  line_->arrived_.wait_for(lock, std::chrono::milliseconds(std::max(timeout_ms, 0)),
                           [this] { return woken_ || !open_ || !line_->plugged_ || !line_->inbound_.empty(); });
  woken_ = false;
  if (!line_->plugged_) open_ = false;
  if (!open_) return std::nullopt;
  const size_t count = std::min(buffer.size(), line_->inbound_.size());
  std::copy_n(line_->inbound_.begin(), count, buffer.begin());
  line_->inbound_.erase(line_->inbound_.begin(), line_->inbound_.begin() + static_cast<ptrdiff_t>(count));
  return count;
}

void FakeSerialPort::WakeRead() {
  {
    std::lock_guard<std::mutex> lock(line_->mutex_);
    woken_ = true;
  }
  line_->arrived_.notify_all();
}

void FakeSerialPort::Close() {
  {
    std::lock_guard<std::mutex> lock(line_->mutex_);
    open_ = false;
  }
  line_->arrived_.notify_all();
}

SerialPortOpener FakeSerialPort::Opener(std::string name, std::shared_ptr<FakeSerialLine> line) {
  return [name = std::move(name), line = std::move(line)](const std::string& requested, const SerialConfig& /*config*/,
                                                          std::string* error) -> std::unique_ptr<SerialPort> {
    {
      std::lock_guard<std::mutex> lock(line->mutex_);
      if (requested != name || !line->plugged_) {
        if (error != nullptr) *error = requested + " 포트가 없습니다";
        return nullptr;
      }
      ++line->open_count_;
    }
    return std::make_unique<FakeSerialPort>(name, line);
  };
}

}  // namespace hwlab::platform
