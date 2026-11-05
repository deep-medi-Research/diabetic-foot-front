#ifndef HWLAB_PIXEL_CALIBRATION_WATCH_H_
#define HWLAB_PIXEL_CALIBRATION_WATCH_H_

#include <cstdint>

#include "hwlab/frame.h"

namespace hwlab::pixel {

/**
 * 프레임 화소 내용의 서명입니다. 같은 화소의 프레임이면 같은 값입니다.
 *
 * 행 끝의 여백(row_stride가 폭보다 긴 부분)은 넣지 않고 화소 바이트만 FNV-1a 64비트로 묶습니다.
 *
 * @param frame 서명할 프레임. plane[0]만 읽습니다
 * @return 서명. 폭이나 높이가 0 이하면 FNV-1a 초기값
 * @note 열화상 SDK는 셔터 교정 동안 같은 화소의 프레임을 계속 주므로(TE-EV1 실측) 이 값의 반복으로 교정 구간을 찾습니다.
 */
uint64_t FrameSignature(const FrameView& frame);

/** 셔터 교정 구간을 프레임 내용으로 판정하는 장치별 수치입니다. 장치마다 실측한 값을 platform이 넘깁니다. */
struct CalibrationRule {
  int32_t freeze_repeats = 0;      // 앞 프레임과 같은 프레임이 이만큼 이어지면 교정 중으로 봅니다
  int32_t freeze_wait_frames = 0;  // 교정 명령 뒤 이만큼 받도록 교정 구간이 보이지 않으면 교정 없음으로 끝냅니다
  int32_t freeze_max_frames = 0;   // 같은 프레임이 이만큼을 넘으면 교정이 끝나지 않는 것으로 보고 끝냅니다
  int32_t discard_frames = 0;      // 교정 뒤 첫 새 프레임을 1로 세어 이 장 수까지 버립니다
};

/** 셔터 교정 판정의 단계입니다. */
enum class CalibrationPhase : uint8_t {
  kWaitingFreeze,  // 명령 뒤 같은 프레임 반복을 기다림
  kFrozen,         // 같은 프레임이 이어짐(교정 중)
  kDiscarding,     // 교정이 끝난 뒤 새 프레임을 버리는 중
  kDone,           // 교정과 버리기가 끝남. 다음 프레임부터 씁니다
  kNotSeen,        // freeze_wait_frames 안에 교정 구간이 보이지 않음
  kStuck,          // 같은 프레임이 freeze_max_frames를 넘음
};

/** 셔터 교정 판정 상태입니다. 값으로 주고받으며 함수 밖에 상태를 두지 않습니다. 명령을 보낼 때 기본값으로 시작합니다. */
struct CalibrationWatch {
  CalibrationPhase phase = CalibrationPhase::kWaitingFreeze;
  bool has_last = false;
  uint64_t last_signature = 0;
  int32_t repeats = 0;          // 지금 이어지는 반복 수(앞 프레임과 같은 장 수)
  int32_t received = 0;         // 명령 뒤 받은 장 수
  int32_t waited_frames = 0;    // 교정 구간의 첫 장 전에 받은 장 수
  int32_t frozen_frames = 0;    // 교정 구간의 같은 프레임 장 수(반복 시작 장 포함)
  int32_t discarded = 0;        // 교정 뒤 버린 새 프레임 수
};

/**
 * 받은 프레임 하나로 판정을 진행합니다.
 *
 * @param watch 지금 상태
 * @param signature 받은 프레임의 FrameSignature
 * @param rule 장치별 수치
 * @return 다음 상태. IsCalibrationFinished가 true가 된 뒤의 프레임은 상태를 바꾸지 않습니다
 */
CalibrationWatch AfterCalibrationFrame(CalibrationWatch watch, uint64_t signature, const CalibrationRule& rule);

/**
 * 판정이 끝났는지입니다.
 *
 * @return kDone, kNotSeen, kStuck이면 true
 */
bool IsCalibrationFinished(const CalibrationWatch& watch);

}  // namespace hwlab::pixel

#endif  // HWLAB_PIXEL_CALIBRATION_WATCH_H_
