#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "hwlab/platform/keep_awake.h"

namespace hwlab::platform {

SleepBlockCall SystemSleepBlockCall() {
  return [](bool block) {
    // ES_CONTINUOUS만 주면 이 스레드가 앞서 건 요청이 풀립니다. 실패하면 0을 돌려줍니다.
    const EXECUTION_STATE flags = block ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS;
    return SetThreadExecutionState(flags) != 0;
  };
}

}  // namespace hwlab::platform
