#ifndef HWLAB_PLATFORM_SRC_OUTPUT_LINES_H_
#define HWLAB_PLATFORM_SRC_OUTPUT_LINES_H_

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>

#include "hwlab/platform/child_process.h"

namespace hwlab::platform {

/** 자식 출력 바이트를 줄로 나눠 보관하는 대기열입니다. 읽기 스레드가 넣고 ReadLine이 꺼냅니다. */
class OutputLines {
 public:
  /** 읽은 바이트를 붙입니다. '\n'이 나올 때마다 한 줄을 대기열에 넣습니다. */
  void Append(const char* bytes, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < size; ++i) {
      if (bytes[i] == '\n') {
        PushLocked();
      } else {
        partial_.push_back(bytes[i]);
      }
    }
    ready_.notify_all();
  }

  /** 출력이 닫혔음을 알립니다. '\n' 없이 남은 조각은 마지막 줄이 됩니다. */
  void Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!partial_.empty()) PushLocked();
    closed_ = true;
    ready_.notify_all();
  }

  bool closed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
  }

  /** 한 줄을 꺼냅니다. ChildProcess::ReadLine과 같은 규칙입니다. */
  LineStatus Read(std::string* line, int64_t timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    ready_.wait_for(lock, std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms),
                    [this] { return !lines_.empty() || closed_; });
    if (!lines_.empty()) {
      *line = std::move(lines_.front());
      lines_.pop_front();
      return LineStatus::kLine;
    }
    return closed_ ? LineStatus::kClosed : LineStatus::kTimeout;
  }

 private:
  void PushLocked() {
    if (!partial_.empty() && partial_.back() == '\r') partial_.pop_back();
    lines_.push_back(std::move(partial_));
    partial_.clear();
  }

  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<std::string> lines_;
  std::string partial_;
  bool closed_ = false;
};

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_SRC_OUTPUT_LINES_H_
