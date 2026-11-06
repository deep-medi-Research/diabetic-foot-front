#ifndef HWLAB_PLATFORM_KEEP_AWAKE_H_
#define HWLAB_PLATFORM_KEEP_AWAKE_H_

#include <cstdint>
#include <functional>

namespace hwlab::platform {

/** 시스템 잠자기를 막거나(true) 푸는(false) OS 호출입니다. OS가 요청을 받아들였으면 true를 돌려줍니다. */
using SleepBlockCall = std::function<bool(bool block)>;

/**
 * 이 OS의 시스템 잠자기 막기 호출입니다.
 *
 * @return Windows는 SetThreadExecutionState로 ES_CONTINUOUS | ES_SYSTEM_REQUIRED를 켜고 ES_CONTINUOUS만 주어 푸는 호출입니다.
 *         다른 OS는 빈 함수입니다
 */
SleepBlockCall SystemSleepBlockCall();

/** KeepAwake가 시스템 잠자기를 막고 있는지입니다. */
enum class KeepAwakeState : uint8_t {
  kUnsupported,  // 이 OS에서는 막지 않음(Windows가 아님)
  kHeld,         // 막고 있음
  kRefused,      // OS가 요청을 거절함
};

/**
 * 살아 있는 동안 시스템 잠자기를 막습니다. 화면 꺼짐은 막지 않습니다.
 *
 * PC가 잠자기 모드로 바뀌면 USB 전원이 꺼지고, 깨어난 뒤 RGB 카메라, 열화상 카메라, PPG가 다시 연결되어 앱이 열어 둔
 * 장치 연결이 끊깁니다.
 *
 * @note Windows는 요청을 부른 스레드에 붙이므로 만든 스레드에서 지웁니다. 스레드나 프로세스가 끝나면 Windows가 요청을 풉니다.
 * @note 덮개를 닫거나 사용자가 직접 고른 잠자기는 막지 못합니다.
 */
class KeepAwake {
 public:
  /** @param call 잠자기 막기 호출. 비어 있으면 아무것도 하지 않습니다(kUnsupported) */
  explicit KeepAwake(SleepBlockCall call = SystemSleepBlockCall());
  /** 막고 있으면 풉니다. */
  ~KeepAwake();
  KeepAwake(const KeepAwake&) = delete;
  KeepAwake& operator=(const KeepAwake&) = delete;

  KeepAwakeState State() const { return state_; }

 private:
  SleepBlockCall call_;
  KeepAwakeState state_ = KeepAwakeState::kUnsupported;
};

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_KEEP_AWAKE_H_
