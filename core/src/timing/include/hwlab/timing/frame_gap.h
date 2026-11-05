#ifndef HWLAB_TIMING_FRAME_GAP_H_
#define HWLAB_TIMING_FRAME_GAP_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "hwlab/timestamp.h"

namespace hwlab::timing {

/** 프레임 간격이 정해진 주기의 1.5배를 넘은 자리 하나입니다. */
struct FrameGap {
  size_t before = 0;  // 누락 직전 프레임의 위치(times 기준)
  int64_t interval_us = 0;
  int64_t missing_frames = 0;  // 간격을 주기로 나눠 반올림한 값에서 1을 뺀 수
};

/**
 * 카메라 단 누락 자리를 찾습니다.
 *
 * 이웃한 두 프레임의 간격이 주기의 1.5배를 넘으면 그 사이에 프레임이 빠진 것으로 봅니다.
 *
 * @param times 받은 순서대로 정렬된 프레임 시각. 저장 누락으로 버린 프레임의 시각도 넣어야 합니다
 * @param period_us 선언한 fps의 프레임 주기
 * @return 누락 자리 목록. period_us가 0 이하이거나 누락이 없으면 빈 목록
 * @note 카메라가 fps를 스스로 낮춘 경우에는 모든 간격이 누락으로 잡힙니다. 먼저 MatchesPeriod로 주기를 확인합니다.
 */
std::vector<FrameGap> FindFrameGaps(std::span<const Timestamp> times, int64_t period_us);

/**
 * 누락 자리들에서 빠진 프레임 수를 더합니다.
 *
 * @param gaps FindFrameGaps의 결과
 * @return 빠진 프레임 수의 합
 */
int64_t CountMissingFrames(std::span<const FrameGap> gaps);

/**
 * 프레임 간격의 중앙값을 구합니다. 누락 몇 곳에 값이 끌려가지 않아 실제 fps를 잴 때 씁니다.
 *
 * @param times 받은 순서대로 정렬된 프레임 시각
 * @return 간격 중앙값(마이크로초). 시각이 둘 미만이면 0
 */
int64_t MedianIntervalUs(std::span<const Timestamp> times);

/**
 * 실제 프레임 간격이 선언한 주기와 맞는지 판정합니다.
 *
 * 카메라가 fps를 스스로 낮추면(어두운 장면의 펌웨어 자동 노출, 2026-10-16 실측 15fps) 누락이 아니라
 * 주기가 다른 것이므로 누락 판정과 따로 봅니다.
 *
 * @param median_interval_us MedianIntervalUs로 잰 실제 간격
 * @param period_us 선언한 fps의 프레임 주기
 * @param tolerance_percent 허용하는 차이(퍼센트)
 * @return 차이가 허용 범위 안이면 true. 간격이나 주기가 0 이하이면 false
 */
bool MatchesPeriod(int64_t median_interval_us, int64_t period_us, int32_t tolerance_percent);

/** 느린 간격이 이어진 구간 하나입니다. 시각은 첫 프레임 기준입니다. */
struct SlowSpan {
  int64_t start_us = 0;    // 구간의 첫 느린 간격이 시작한 프레임의 시각
  int64_t end_us = 0;      // 구간의 마지막 느린 간격이 끝난 프레임의 시각
  int64_t intervals = 0;   // 구간 안의 느린 간격 수
  friend bool operator==(const SlowSpan&, const SlowSpan&) = default;
};

/** 녹화 중 프레임 속도가 떨어진 정도입니다(CheckFrameRate). */
struct FrameRateCheck {
  int64_t slow_intervals = 0;   // 느린 간격 수. 주기의 1.2배를 넘고 1.5배 이하인 간격입니다
  std::vector<SlowSpan> spans;  // 느린 간격을 1초 안팎으로 묶은 구간
  double min_fps = 0;           // 1초 창(주기 기준 프레임 수)에서 잰 가장 낮은 fps. 누락도 반영합니다. 프레임이 둘 미만이면 0
  friend bool operator==(const FrameRateCheck&, const FrameRateCheck&) = default;
};

/**
 * 프레임 속도가 떨어진 구간을 찾습니다.
 *
 * 카메라가 30fps에서 25fps로 떨어지면 간격이 40ms(주기의 1.2배)가 되어 누락(1.5배 초과, FindFrameGaps)으로 잡히지 않습니다.
 * 이 함수는 주기의 1.2배를 넘고 1.5배 이하인 간격을 느린 간격으로 세고, 다음 느린 간격이 1초 안에 오면 같은 구간으로 묶습니다.
 *
 * @param times 받은 순서대로 정렬된 프레임 시각
 * @param period_us 선언한 fps의 프레임 주기. 0 이하이면 빈 결과
 * @return 느린 간격 수, 구간, 1초 창 최저 fps
 * @note 1.2배는 30fps의 흔들림(노트북 실측 27건, 33.15~33.50ms)보다 충분히 크고 25fps(40ms)를 잡는 값입니다.
 */
FrameRateCheck CheckFrameRate(std::span<const Timestamp> times, int64_t period_us);

}  // namespace hwlab::timing

#endif  // HWLAB_TIMING_FRAME_GAP_H_
