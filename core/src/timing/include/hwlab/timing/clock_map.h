#ifndef HWLAB_TIMING_CLOCK_MAP_H_
#define HWLAB_TIMING_CLOCK_MAP_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "hwlab/timestamp.h"

namespace hwlab::timing {

/**
 * 같은 프레임(또는 PPG 표본)에 붙은 장치 시각과 PC 수신 시각입니다.
 *
 * @note host는 PC가 프레임을 받은 시각이라 촬영 시각보다 항상 늦고, 늦은 정도(수신 지연)는 프레임마다 다릅니다.
 */
struct ClockSample {
  Timestamp device;  // 장치 또는 드라이버가 붙인 시각
  Timestamp host;    // PC가 받은 시각(공통 시간축)
};

/**
 * 장치 시각을 공통 시간축(PC 시계)으로 옮기는 선형식입니다.
 *
 * 공통 시각은 offset_us + rate * device(마이크로초, 반올림)입니다. 기본값은 항등식입니다.
 *
 * @note 열화상(i3 SDK)은 프레임에 장치 시각이 없어 PC 수신 시각을 장치 시각으로 쓰므로 항등식입니다.
 *       PPG는 표본 번호를 표본률로 나눈 값이 장치 시각입니다. RGB는 Windows Media Foundation 시각입니다.
 */
struct ClockMap {
  int64_t offset_us = 0;
  double rate = 1.0;            // 장치 시각 1마이크로초에 해당하는 PC 시각(마이크로초)
  int64_t max_residual_us = 0;  // 맞춘 표본 중 PC 수신 시각이 선형식보다 가장 늦은 값(수신 지연의 변동 폭)

  friend bool operator==(const ClockMap&, const ClockMap&) = default;
};

/**
 * 장치 시각과 PC 수신 시각의 쌍에서 ClockMap을 구합니다.
 *
 * 수신은 촬영보다 항상 늦으므로 모든 표본의 아래쪽을 지나는 직선(최소 지연 하한선)에 맞춥니다.
 * 표본점의 아래 볼록 껍질 변 가운데 장치 시각 평균 위치를 지나는 변을 고릅니다. 이 직선은 모든 표본 아래에 있는
 * 직선 중 수신 지연 합이 가장 작습니다. 따라서 공통 시각은 수신 지연이 가장 짧은 경우의 PC 수신 시각입니다.
 *
 * @param samples 받은 순서대로의 표본
 * @return 맞춘 선형식. 표본이 없으면 항등식입니다. 다음 경우에는 rate를 1로 두고 offset_us를 가장 작은
 *         (host - device)로 정합니다: 표본이 2개 이하, 장치 시각이 앞 표본보다 작아짐, 구한 rate가 0 이하
 * @note 장치 시각이 같은 표본이 여럿이면 PC 수신 시각이 가장 이른 것만 씁니다.
 */
ClockMap FitClockMap(std::span<const ClockSample> samples);

/**
 * 장치 시각을 공통 시간축 시각으로 옮깁니다.
 *
 * @param map FitClockMap의 결과
 * @param device 장치 시각
 * @return 공통 시간축 시각(PC 시계, 마이크로초)
 */
Timestamp ToCommonTime(const ClockMap& map, Timestamp device);

/**
 * 수신 빈틈으로 나눈 시계 구간 하나입니다.
 *
 * @note map의 속도 비는 같은 FitClockSegments 결과의 모든 구간이 같고, offset_us와 max_residual_us만 구간마다 다릅니다.
 */
struct ClockSegment {
  Timestamp last_device;  // 이 구간 마지막 표본의 장치 시각. 앞 구간의 last_device보다 크고 이 값 이하인 장치 시각이 이 구간입니다
  ClockMap map;

  friend bool operator==(const ClockSegment&, const ClockSegment&) = default;
};

/**
 * 표본을 수신 빈틈에서 시계 구간으로 나누고 구간마다 ClockMap을 구합니다.
 *
 * 이웃한 두 표본 사이에서 PC 수신 시각이 장치 시각보다 gap_us 넘게 더 흐르면 그 사이를 수신 빈틈으로 봅니다.
 * 속도 비는 장치 시각 폭이 가장 긴 구간을 FitClockMap으로 맞춘 값을 모든 구간에 씁니다. 오프셋은 구간마다 그 구간의
 * 모든 표본 아래를 지나는 값 중 가장 큰 값(가장 짧은 수신 지연)입니다. 가장 긴 구간의 map은 FitClockMap 결과와 같습니다.
 *
 * @param samples 받은 순서대로의 표본
 * @param gap_us 수신 빈틈으로 볼, PC 수신 시각이 장치 시각보다 더 흐른 시간. 0 이하이면 나누지 않습니다
 * @return 장치 시각 순서의 구간. 표본이 없으면 빈 목록입니다. 장치 시각이 앞 표본보다 작아지는 입력은 나누지 않고
 *         모든 표본의 FitClockMap 결과 하나입니다
 * @note 장치가 표본을 보내지 않다가 다시 보내도 표본 번호로 구한 장치 시각은 이어집니다. 이런 표본을 한 식으로 맞추면
 *       아래 껍질이 빈틈 앞 표본에서 꺾여 속도 비가 커지고, 빈틈 뒤 표본의 공통 시각이 빈틈만큼 이르게 나옵니다.
 *       빈틈 앞 구간이 표본 한두 개로 짧아도 속도 비를 긴 구간에서 가져오므로 오프셋만 맞추면 됩니다.
 */
std::vector<ClockSegment> FitClockSegments(std::span<const ClockSample> samples, int64_t gap_us);

/**
 * 장치 시각을 그 시각이 든 시계 구간의 식으로 공통 시간축에 옮깁니다.
 *
 * @param segments FitClockSegments의 결과
 * @param device 장치 시각
 * @return 공통 시각. last_device가 device 이상인 첫 구간의 식을 쓰고, 모든 구간의 last_device보다 크면 마지막 구간의 식을
 *         씁니다. 구간이 없으면 장치 시각 그대로입니다
 */
Timestamp ToCommonTime(std::span<const ClockSegment> segments, Timestamp device);

/**
 * 기준 스트림의 프레임마다 공통 시각이 가장 가까운 다른 스트림 프레임의 위치를 찾습니다.
 *
 * @param reference 기준 스트림(RGB)의 공통 시각. 오름차순
 * @param other 다른 스트림의 공통 시각. 오름차순
 * @return reference와 같은 길이의 목록. i번째 값은 reference[i]와 가장 가까운 other의 위치입니다.
 *         거리가 같으면 앞 위치를 고릅니다. other가 비어 있으면 빈 목록입니다
 * @note 오름차순이 아닌 입력에도 같은 입력에는 같은 결과를 내지만, 가장 가까운 위치라는 보장은 없습니다.
 */
std::vector<size_t> NearestFrames(std::span<const Timestamp> reference, std::span<const Timestamp> other);

}  // namespace hwlab::timing

#endif  // HWLAB_TIMING_CLOCK_MAP_H_
