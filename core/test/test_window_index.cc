#include <gtest/gtest.h>

#include <cstdint>

#include "hwlab/timing/window_index.h"

namespace {

using hwlab::timing::WindowIndexAt;

constexpr int64_t kSecondUs = 1'000'000;
constexpr int64_t kWindowUs = 10 * kSecondUs;
constexpr int64_t kStrideUs = 2 * kSecondUs;

// 관류 분석 프로그램 기본값(창 10초, 간격 2초, 창 11개)에서 첫 창의 가운데(5초)까지는 창 0입니다.
TEST(WindowIndex, BeforeFirstCentreIsFirstWindow) {
  EXPECT_EQ(WindowIndexAt(-kSecondUs, 11, kWindowUs, kStrideUs), 0);
  EXPECT_EQ(WindowIndexAt(0, 11, kWindowUs, kStrideUs), 0);
  EXPECT_EQ(WindowIndexAt(5 * kSecondUs, 11, kWindowUs, kStrideUs), 0);
}

// 두 창 가운데의 정확히 중간(6초)은 앞 창, 그보다 1마이크로초 뒤는 다음 창입니다.
TEST(WindowIndex, TieGoesToEarlierWindow) {
  EXPECT_EQ(WindowIndexAt(6 * kSecondUs - 1, 11, kWindowUs, kStrideUs), 0);
  EXPECT_EQ(WindowIndexAt(6 * kSecondUs, 11, kWindowUs, kStrideUs), 0);
  EXPECT_EQ(WindowIndexAt(6 * kSecondUs + 1, 11, kWindowUs, kStrideUs), 1);
  EXPECT_EQ(WindowIndexAt(7 * kSecondUs, 11, kWindowUs, kStrideUs), 1);
}

// 마지막 창의 가운데(25초) 뒤와 녹화 끝(30초)은 마지막 창입니다.
TEST(WindowIndex, AfterLastCentreIsLastWindow) {
  EXPECT_EQ(WindowIndexAt(25 * kSecondUs, 11, kWindowUs, kStrideUs), 10);
  EXPECT_EQ(WindowIndexAt(30 * kSecondUs, 11, kWindowUs, kStrideUs), 10);
  EXPECT_EQ(WindowIndexAt(1000 * kSecondUs, 11, kWindowUs, kStrideUs), 10);
}

// 창 길이가 홀수 마이크로초여도 가운데를 반올림하지 않고 비교합니다.
TEST(WindowIndex, OddWindowLengthKeepsHalfMicrosecond) {
  // 창 3us, 간격 2us: 가운데 1.5, 3.5. 2.5는 같은 거리라 앞 창, 2.5보다 뒤인 3은 다음 창입니다.
  EXPECT_EQ(WindowIndexAt(2, 2, 3, 2), 0);
  EXPECT_EQ(WindowIndexAt(3, 2, 3, 2), 1);
}

// 창이 하나이면 항상 0, 창 수나 길이나 간격이 맞지 않으면 값이 없습니다.
TEST(WindowIndex, InvalidInputsHaveNoIndex) {
  EXPECT_EQ(WindowIndexAt(123, 1, 0, 0), 0);
  EXPECT_FALSE(WindowIndexAt(0, 0, kWindowUs, kStrideUs).has_value());
  EXPECT_FALSE(WindowIndexAt(0, -1, kWindowUs, kStrideUs).has_value());
  EXPECT_FALSE(WindowIndexAt(0, 11, 0, kStrideUs).has_value());
  EXPECT_FALSE(WindowIndexAt(0, 11, kWindowUs, 0).has_value());
}

}  // namespace
