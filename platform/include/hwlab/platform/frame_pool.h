#ifndef HWLAB_PLATFORM_FRAME_POOL_H_
#define HWLAB_PLATFORM_FRAME_POOL_H_

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

#include "hwlab/record/sidecar.h"

namespace hwlab::platform {

/** 수신 스레드가 쓰기 스레드에 보내는 메시지입니다. 순서대로 처리합니다. */
struct WriteMessage {
  enum class Kind : uint8_t { kBegin, kFrame, kDropped, kEnd, kQuit };
  Kind kind = Kind::kFrame;
  int32_t slot = -1;           // kFrame일 때 화소가 든 버퍼 번호. kDropped는 버퍼 없이 시각만 넘깁니다
  record::FrameRecord record;  // kFrame일 때 시각과 번호. 밝기는 쓰기 스레드가 채웁니다
  int32_t end_reason = 0;      // kEnd일 때 flow::EndReason 값
};

/**
 * 수신 스레드와 쓰기 스레드 사이의 버퍼와 메시지 대기열입니다.
 *
 * 크기가 같은 버퍼를 미리 잡아 두고 돌려 씁니다. 프레임마다 메모리를 새로 잡지 않기 위해서입니다.
 */
class FramePool {
 public:
  /**
   * @param slot_bytes 버퍼 하나의 크기(프레임 하나)
   * @param slots 버퍼 수. 쓰기가 밀릴 때 버틸 프레임 수입니다
   */
  FramePool(size_t slot_bytes, int32_t slots);

  /**
   * 빈 버퍼 하나를 빌립니다.
   *
   * @return 버퍼 번호. 빈 버퍼가 없으면 기다리지 않고 nullopt(수신 스레드를 멈추지 않기 위해서입니다)
   */
  std::optional<int32_t> TryAcquire();

  /** 버퍼의 시작 주소입니다. */
  uint8_t* Data(int32_t slot) { return buffers_[static_cast<size_t>(slot)].data(); }

  /** 다 쓴 버퍼를 돌려줍니다. */
  void Release(int32_t slot);

  /** 메시지를 대기열 끝에 넣습니다. */
  void Push(const WriteMessage& message);

  /** 메시지가 올 때까지 기다렸다가 맨 앞 메시지를 꺼냅니다. */
  WriteMessage Pop();

  /** 빈 버퍼 수입니다. 쓰기 대기열 길이를 잴 때 씁니다. */
  int32_t FreeSlots();

 private:
  std::vector<std::vector<uint8_t>> buffers_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::vector<int32_t> free_;
  std::deque<WriteMessage> messages_;
};

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_FRAME_POOL_H_
