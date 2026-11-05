#include <gtest/gtest.h>

#include <string>

#include "hwlab/platform/rgb_camera.h"

namespace {

using hwlab::platform::LowFrameRateMessage;

// 2026-11-04 현장 보고: USB 3 포트에 꽂았는데 케이블 때문에 USB 2 속도로 연결되어 UYVY 1280x720이 최대 15fps였습니다.
// 현장이 포트만 확인하지 않도록 원인 후보 셋(케이블, 허브, Type-C 방향)을 모두 말해야 합니다.
TEST(LowFrameRateMessage, NamesFormatRatesAndEveryCauseToCheck) {
  const std::string text = LowFrameRateMessage("UYVY 1280x720", 15.0, 60);
  EXPECT_NE(text.find("UYVY 1280x720 형식이 최대 15fps라 60fps로 찍을 수 없습니다"), std::string::npos) << text;
  EXPECT_NE(text.find("USB 3 케이블"), std::string::npos) << text;
  EXPECT_NE(text.find("허브"), std::string::npos) << text;
  EXPECT_NE(text.find("Type-C"), std::string::npos) << text;
}

// 최대 fps만 보고 한 추정이라 단정하지 않습니다.
TEST(LowFrameRateMessage, SaysUsb2LinkIsAnInference) {
  const std::string text = LowFrameRateMessage("UYVY 1920x1200", 6.0, 30);
  EXPECT_NE(text.find("USB 2 속도로 연결된 것으로 보입니다"), std::string::npos) << text;
}

TEST(LowFrameRateMessage, RoundsMaxFpsToWholeNumber) {
  EXPECT_NE(LowFrameRateMessage("UYVY 1280x720", 14.99, 60).find("최대 15fps"), std::string::npos);
}

// 예전에는 200바이트 버퍼에 써서 안내가 길어지면 끝이 잘렸습니다.
TEST(LowFrameRateMessage, KeepsTheLastInstructionWhole) {
  const std::string text = LowFrameRateMessage("UYVY 1920x1200", 6.0, 30);
  const std::string tail = "뒤집어 꽂으십시오";
  ASSERT_GE(text.size(), tail.size());
  EXPECT_EQ(text.substr(text.size() - tail.size()), tail) << text;
}

}  // namespace
