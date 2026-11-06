#include "hwlab/platform/keep_awake.h"

#include <gtest/gtest.h>

#include <vector>

using hwlab::platform::KeepAwake;
using hwlab::platform::KeepAwakeState;

namespace {

// 잠자기 막기 호출에 넘긴 값을 순서대로 모읍니다.
struct Calls {
  std::vector<bool> args;
  bool accept = true;

  hwlab::platform::SleepBlockCall Fn() {
    return [this](bool block) {
      args.push_back(block);
      return accept;
    };
  }
};

}  // namespace

// 만들 때 잠자기를 막고, 지울 때 한 번 풉니다.
TEST(KeepAwake, BlocksWhileAliveAndReleasesOnce) {
  Calls calls;
  {
    const KeepAwake awake(calls.Fn());
    EXPECT_EQ(awake.State(), KeepAwakeState::kHeld);
    EXPECT_EQ(calls.args, (std::vector<bool>{true}));
  }
  EXPECT_EQ(calls.args, (std::vector<bool>{true, false}));
}

// OS가 막기를 거절하면 풀 요청이 없으므로 지울 때 부르지 않습니다.
TEST(KeepAwake, DoesNotReleaseRefusedRequest) {
  Calls calls;
  calls.accept = false;
  {
    const KeepAwake awake(calls.Fn());
    EXPECT_EQ(awake.State(), KeepAwakeState::kRefused);
  }
  EXPECT_EQ(calls.args, (std::vector<bool>{true}));
}

// Windows가 아니면 OS 호출이 없어 아무것도 하지 않습니다. Windows는 이 스레드에 요청을 걸고 풉니다.
TEST(KeepAwake, SystemCallMatchesOs) {
  const KeepAwake awake;
#ifdef _WIN32
  EXPECT_EQ(awake.State(), KeepAwakeState::kHeld);
#else
  EXPECT_FALSE(hwlab::platform::SystemSleepBlockCall());
  EXPECT_EQ(awake.State(), KeepAwakeState::kUnsupported);
#endif
}
