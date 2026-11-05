#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "hwlab/pixel/calibration_watch.h"

namespace {

using hwlab::FrameView;
using hwlab::PixelFormat;
using hwlab::pixel::AfterCalibrationFrame;
using hwlab::pixel::CalibrationPhase;
using hwlab::pixel::CalibrationRule;
using hwlab::pixel::CalibrationWatch;
using hwlab::pixel::FrameSignature;
using hwlab::pixel::IsCalibrationFinished;

constexpr CalibrationRule kRule{3, 10, 100, 5};

// 서명 열(같은 숫자는 같은 프레임)을 차례로 넣은 상태입니다.
CalibrationWatch Feed(const std::vector<uint64_t>& signatures, const CalibrationRule& rule = kRule) {
  CalibrationWatch watch;
  for (const uint64_t s : signatures) watch = AfterCalibrationFrame(watch, s, rule);
  return watch;
}

// 반복 구간 뒤 새 프레임을 1로 세어 discard_frames장째에 끝나고, 구간 길이와 기다린 장 수를 남깁니다.
TEST(CalibrationWatch, DetectsFreezeThenDiscardsFreshFrames) {
  std::vector<uint64_t> s{1, 2, 3, 3, 3, 3, 3, 3};  // 3이 6장(반복 5)
  CalibrationWatch w = Feed(s);
  EXPECT_EQ(w.phase, CalibrationPhase::kFrozen);
  EXPECT_EQ(w.waited_frames, 2);
  EXPECT_EQ(w.frozen_frames, 6);
  for (uint64_t v = 10; v < 14; ++v) w = AfterCalibrationFrame(w, v, kRule);
  EXPECT_EQ(w.phase, CalibrationPhase::kDiscarding);
  EXPECT_EQ(w.discarded, 4);
  w = AfterCalibrationFrame(w, 14, kRule);
  EXPECT_EQ(w.phase, CalibrationPhase::kDone);
  EXPECT_EQ(w.discarded, 5);
  EXPECT_TRUE(IsCalibrationFinished(w));
  // 끝난 뒤의 프레임은 상태를 바꾸지 않습니다.
  EXPECT_EQ(AfterCalibrationFrame(w, 14, kRule).received, w.received);
}

// 교정 밖에서 보이는 짧은 반복(실측 최대 1회)은 교정으로 보지 않습니다.
TEST(CalibrationWatch, ShortRepeatIsNotFreeze) {
  const CalibrationWatch w = Feed({1, 2, 2, 3, 4, 4, 4, 5});
  EXPECT_EQ(w.phase, CalibrationPhase::kWaitingFreeze);
  EXPECT_EQ(w.repeats, 0);
}

// 기다리는 장 수 안에 반복이 없으면 교정 없음으로 끝납니다.
TEST(CalibrationWatch, EndsAsNotSeenAfterWaitFrames) {
  std::vector<uint64_t> s;
  for (uint64_t v = 0; v < 10; ++v) s.push_back(v);
  const CalibrationWatch w = Feed(s);
  EXPECT_EQ(w.phase, CalibrationPhase::kNotSeen);
  EXPECT_TRUE(IsCalibrationFinished(w));
}

// 같은 프레임이 상한을 넘으면 끝나지 않는 교정으로 보고 끝냅니다.
TEST(CalibrationWatch, EndsAsStuckWhenFreezeTooLong) {
  const CalibrationRule rule{3, 10, 8, 5};
  const CalibrationWatch w = Feed(std::vector<uint64_t>(12, 7), rule);
  EXPECT_EQ(w.phase, CalibrationPhase::kStuck);
  EXPECT_EQ(w.frozen_frames, 9);
}

// 버릴 장 수가 0이면 교정 뒤 첫 새 프레임에서 끝납니다.
TEST(CalibrationWatch, ZeroDiscardEndsAtFirstFreshFrame) {
  const CalibrationRule rule{3, 10, 100, 0};
  const CalibrationWatch w = Feed({5, 5, 5, 5, 6}, rule);
  EXPECT_EQ(w.phase, CalibrationPhase::kDone);
  EXPECT_EQ(w.discarded, 0);
  EXPECT_EQ(w.waited_frames, 0);
  EXPECT_EQ(w.frozen_frames, 4);
}

// 서명은 화소가 같으면 같고, 한 화소가 다르면 다르며, 행 끝 여백은 넣지 않습니다.
TEST(FrameSignature, IgnoresStridePaddingAndSeesOnePixel) {
  std::vector<uint8_t> tight(4 * 2 * 2, 0);
  std::vector<uint8_t> padded(2 * 12, 0xEE);
  for (int y = 0; y < 2; ++y) {
    for (int i = 0; i < 8; ++i) {
      tight[static_cast<size_t>(y * 8 + i)] = static_cast<uint8_t>(y * 8 + i);
      padded[static_cast<size_t>(y * 12 + i)] = static_cast<uint8_t>(y * 8 + i);
    }
  }
  FrameView a;
  a.format = PixelFormat::kY16;
  a.width = 4;
  a.height = 2;
  a.plane[0] = tight.data();
  a.row_stride[0] = 8;
  FrameView b = a;
  b.plane[0] = padded.data();
  b.row_stride[0] = 12;
  EXPECT_EQ(FrameSignature(a), FrameSignature(b));
  tight[5] ^= 1;
  EXPECT_NE(FrameSignature(a), FrameSignature(b));
}

}  // namespace
