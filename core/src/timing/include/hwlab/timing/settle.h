#ifndef HWLAB_TIMING_SETTLE_H_
#define HWLAB_TIMING_SETTLE_H_

#include <cstdint>

#include "hwlab/camera_settings.h"

namespace hwlab::timing {

/**
 * 설정 변경이 영상에 반영되고 안정되기까지의 프레임 수입니다.
 *
 * 카메라마다 실측한 값을 platform이 넘깁니다(설계 문서 표 5). 프레임 수는 설정 변경 뒤 받은 프레임을 1부터 센 순서입니다.
 */
struct SettleRule {
  int32_t exposure_applied_frame = 0;       // 새 노출로 찍힌 첫 프레임
  int32_t gain_applied_frame = 0;
  int32_t white_balance_applied_frame = 0;
  int32_t black_level_settled_frame = 0;    // 설정 변경 뒤 블랙 레벨 변화가 끝나는 프레임
  // LED 명령 뒤 새 밝기로 찍힌 첫 프레임. 자동 맞춤은 이 프레임이 지난 뒤 잠급니다. 실측하지 않아 0(기다리지 않음)이 기본입니다
  int32_t light_applied_frame = 0;
};

/** 반영과 안정화까지 남은 프레임 수입니다. 값으로 주고받으며 함수 밖에 상태를 두지 않습니다. */
struct SettleState {
  int32_t frames_until_applied = 0;
  int32_t frames_until_settled = 0;
};

/**
 * 설정 변경 하나를 반영해 남은 프레임 수를 다시 셉니다.
 *
 * @param state 지금 상태
 * @param kind 바꾼 설정값의 종류
 * @param rule 카메라 실측값
 * @return 새 상태. 여러 설정값을 연달아 바꾸면 가장 늦게 끝나는 쪽을 기다립니다
 */
SettleState AfterSettingChange(SettleState state, SettingKind kind, const SettleRule& rule);

/**
 * 프레임 하나를 받은 만큼 남은 프레임 수를 줄입니다.
 *
 * @param state 지금 상태
 * @return 새 상태. 이 상태로 방금 받은 프레임을 판정합니다(IsApplied, IsSettled)
 * @note 프레임을 받은 직후에 부릅니다.
 */
SettleState AfterFrame(SettleState state);

/** 새 설정값으로 찍힌 프레임이면 true입니다. */
constexpr bool IsApplied(SettleState state) { return state.frames_until_applied <= 0; }

/** 블랙 레벨 변화까지 끝나 녹화에 쓸 수 있는 프레임이면 true입니다. */
constexpr bool IsSettled(SettleState state) { return state.frames_until_settled <= 0; }

}  // namespace hwlab::timing

#endif  // HWLAB_TIMING_SETTLE_H_
