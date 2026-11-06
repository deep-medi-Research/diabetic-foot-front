#include "hwlab/platform/keep_awake.h"

#include <utility>

namespace hwlab::platform {

KeepAwake::KeepAwake(SleepBlockCall call) : call_(std::move(call)) {
  if (call_) state_ = call_(true) ? KeepAwakeState::kHeld : KeepAwakeState::kRefused;
}

KeepAwake::~KeepAwake() {
  if (state_ == KeepAwakeState::kHeld) call_(false);
}

// Windows 구현은 windows/keep_awake_windows.cc에 있습니다.
#ifndef _WIN32
SleepBlockCall SystemSleepBlockCall() { return nullptr; }
#endif

}  // namespace hwlab::platform
