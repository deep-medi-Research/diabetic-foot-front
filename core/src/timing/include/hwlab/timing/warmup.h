#ifndef HWLAB_TIMING_WARMUP_H_
#define HWLAB_TIMING_WARMUP_H_

#include <cstdint>
#include <deque>
#include <optional>

#include "hwlab/timestamp.h"

namespace hwlab::timing {

/**
 * 열화상 카메라 온도(FPA 값)로 예열 완료와 렌즈 셰이딩 보정 유효를 판정하는 수치입니다.
 *
 * 장치마다 실측한 값을 platform이 넘깁니다. FPA 값은 단위가 없어 화면에는 셔터 온도(°C)로 옮겨 보입니다.
 */
struct WarmupRule {
  int64_t window_us = 0;         // 오르는 속도를 재는 구간. 이 구간 동안 오른 양을 구간 길이로 나눕니다
  int64_t sample_period_us = 0;  // 구간 안에 남기는 값의 간격. 앞 값에서 이 간격이 지나기 전에 온 값은 버립니다
  int64_t max_gap_us = 0;        // 값이 이보다 오래 오지 않으면 지난 값을 버리고 처음부터 잽니다
  double ready_fpa_per_min = 0;  // FPA 값이 분당 이보다 느리게 오르면 기준 아래입니다
  int64_t hold_us = 0;           // 기준 아래에 이만큼 머물면 예열 완료입니다
  double shutter_c_per_fpa = 0;  // FPA 값 1에 해당하는 셔터 온도 변화(°C)
  double lens_fit_max_c = 0;     // 보정 때와 지금의 카메라 온도 차이(셔터 온도로 옮긴 값)가 이보다 크면 다시 보정합니다
};

/** 예열 판정의 단계입니다. */
enum class WarmupPhase : uint8_t {
  kMeasuring,  // 값이 구간만큼 모이지 않아 오르는 속도를 아직 모름
  kWarming,    // 오르는 속도가 기준 이상
  kSettling,   // 기준 아래로 내려왔고 머문 시간이 hold_us보다 짧음
  kReady,      // 기준 아래에 hold_us 이상 머묾
};

/** 예열 판정 결과입니다. 화면과 렌즈 셰이딩 보정 기록에 씁니다. */
struct WarmupStatus {
  WarmupPhase phase = WarmupPhase::kMeasuring;
  std::optional<double> rate_fpa_per_min;  // 지난 구간 동안 FPA 값이 오른 속도. kMeasuring이면 nullopt
  // kMeasuring은 오르는 속도를 처음 잴 때까지, kSettling은 예열 완료까지 남은 시간입니다. 그 밖에는 0입니다
  int64_t remaining_us = 0;

  friend bool operator==(const WarmupStatus&, const WarmupStatus&) = default;
};

/** 구간 안에 남긴 카메라 온도 하나입니다. */
struct WarmupSample {
  Timestamp time;
  double fpa = 0;
};

/** 예열 판정 상태입니다. 값으로 주고받으며 함수 밖에 상태를 두지 않습니다. 카메라를 처음 열 때 기본값으로 시작합니다. */
struct WarmupState {
  WarmupStatus status;
  std::deque<WarmupSample> samples;      // sample_period_us 간격으로 남긴 값. 맨 앞이 가장 오래됨
  std::optional<Timestamp> below_since;  // 오르는 속도가 기준 아래로 내려온 시각. 기준 이상이면 nullopt
};

/**
 * 카메라 온도 하나로 판정을 진행합니다.
 *
 * 오르는 속도는 지금 값에서 window_us 전의 값을 빼 분으로 나눈 값입니다. 오르는 속도가 기준 아래로 내려와 hold_us 동안
 * 머물면 예열 완료이고, 그 사이 다시 기준 이상이 되면 머문 시간을 처음부터 셉니다. 예열 완료 뒤에도 기준 이상이 되면
 * 예열 중으로 돌아갑니다.
 *
 * @param state 지금 상태
 * @param time 값을 읽은 시각. 받은 순서대로 넣습니다
 * @param fpa FPA 값
 * @param rule 판정 수치
 * @return 다음 상태. 앞 값보다 이른 시각이나 max_gap_us를 넘는 빈틈 뒤의 값은 처음부터 다시 잽니다
 */
WarmupState AfterCameraTemperature(WarmupState state, Timestamp time, double fpa, const WarmupRule& rule);

/**
 * FPA 값의 차이를 셔터 온도 차이(°C)로 옮깁니다. 오르는 속도와 보정 때와의 온도 차이를 화면에 보일 때 씁니다.
 *
 * @param fpa_delta FPA 값의 차이
 * @param rule 판정 수치
 * @return 셔터 온도 차이(°C)
 */
constexpr double ShutterCelsius(double fpa_delta, const WarmupRule& rule) { return fpa_delta * rule.shutter_c_per_fpa; }

/** 렌즈 셰이딩 보정을 한 때의 카메라 온도와 예열 단계입니다. 보정 파일과 함께 남깁니다. */
struct LensShadingConditions {
  double shutter_c = 0;  // 셔터 온도(°C). 셔터가 마지막으로 닫힌 때 잰 값입니다
  double fpa = 0;
  WarmupPhase warmup = WarmupPhase::kMeasuring;

  friend bool operator==(const LensShadingConditions&, const LensShadingConditions&) = default;
};

/** 적용된 렌즈 셰이딩 보정을 지금 카메라 온도와 견준 판정입니다. */
enum class LensShadingFit : uint8_t {
  kUnknown,           // 보정 때의 기록이 없거나 지금 카메라 온도를 모름
  kMadeWhileWarming,  // 예열 완료 전에 한 보정. 예열이 끝난 뒤 다시 보정합니다
  kWaitWarmup,        // 지금 카메라가 예열 완료가 아니라 맞지 않을 수 있음. 예열이 끝난 뒤 다시 판정합니다
  kTemperatureMoved,  // 보정 때와 지금의 카메라 온도 차이가 lens_fit_max_c를 넘음. 다시 보정합니다
  kFits,              // 보정 때와 지금이 모두 예열 완료이고 온도 차이가 lens_fit_max_c 안
};

/**
 * 적용된 렌즈 셰이딩 보정을 지금 카메라 온도와 견주어 판정합니다.
 *
 * @param made 보정을 한 때의 기록. 기록이 없는 보정 파일이면 nullopt
 * @param fpa_now 지금 FPA 값. 모르면 nullopt
 * @param now 지금 예열 단계
 * @param rule 판정 수치
 * @return 판정. 예열 완료 전에 한 보정이면 지금 상태와 상관없이 kMadeWhileWarming입니다
 */
LensShadingFit JudgeLensShadingFit(const std::optional<LensShadingConditions>& made, std::optional<double> fpa_now,
                                   WarmupPhase now, const WarmupRule& rule);

}  // namespace hwlab::timing

#endif  // HWLAB_TIMING_WARMUP_H_
