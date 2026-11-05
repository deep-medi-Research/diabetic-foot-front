#include "hwlab/platform/frame_pool.h"

namespace hwlab::platform {

FramePool::FramePool(size_t slot_bytes, int32_t slots) {
  buffers_.resize(static_cast<size_t>(slots), std::vector<uint8_t>(slot_bytes));
  for (int32_t i = slots - 1; i >= 0; --i) {
    free_.push_back(i);
  }
}

std::optional<int32_t> FramePool::TryAcquire() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (free_.empty()) {
    return std::nullopt;
  }
  const int32_t slot = free_.back();
  free_.pop_back();
  return slot;
}

void FramePool::Release(int32_t slot) {
  std::lock_guard<std::mutex> lock(mutex_);
  free_.push_back(slot);
}

void FramePool::Push(const WriteMessage& message) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.push_back(message);
  }
  ready_.notify_one();
}

WriteMessage FramePool::Pop() {
  std::unique_lock<std::mutex> lock(mutex_);
  ready_.wait(lock, [&] { return !messages_.empty(); });
  WriteMessage message = messages_.front();
  messages_.pop_front();
  return message;
}

int32_t FramePool::FreeSlots() {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<int32_t>(free_.size());
}

}  // namespace hwlab::platform
