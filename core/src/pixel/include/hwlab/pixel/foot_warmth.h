#ifndef HWLAB_PIXEL_FOOT_WARMTH_H_
#define HWLAB_PIXEL_FOOT_WARMTH_H_

#include <array>
#include <cstdint>
#include <deque>

#include "hwlab/frame.h"
#include "hwlab/pixel/rect.h"

namespace hwlab::pixel {

/**
 * 발이 따뜻하다고 보는 열화상 대비의 아래 끝(원시값)입니다.
 *
 * @note 관류 분석 프로그램(v8 이후)의 발 온도 검사 기준과 같은 값입니다. 녹화 146건에서 이 값보다 작으면
 *       맥박 진폭이 급격히 줄었습니다. 열화상 장치(TE-EV1)의 원시값이라 섭씨로 바꿀 수 없고, 렌즈 셰이딩이 틀어지면
 *       대비도 흔들려 잠정값입니다.
 */
constexpr int32_t kWarmContrastMin = 60;

/**
 * 판정에 쓰는 미리보기 장 수입니다.
 *
 * @note 관류 분석 프로그램은 녹화 첫 2초(9장/초 녹화의 18장) 평균 영상으로 대비를 구합니다. 미리보기는 약 10장/초라
 *       같은 2초인 20장을 봅니다. 노트북 미리보기의 흔들림을 재서 바꿀 수 있습니다.
 */
constexpr int32_t kWarmFrames = 20;

/**
 * 발 상자 안 열화상 대비입니다. 상위 10%(발바닥)와 하위 25%(배경)의 원시값 차이입니다.
 *
 * @param y16 Y16 열화상 프레임
 * @param rect 발 상자(프레임 화소)
 * @return 대비(원시값). 상자가 비었거나 형식이 Y16이 아니면 0
 * @note 관류 분석 프로그램의 발 온도 검사와 같은 식입니다. 발을 분할하지 않고, 분할한 발 - 배경 대비와 같은 값임을
 *       확인했습니다(녹화 33건, r 0.995). 상자를 발에 바짝 줄이면 배경이 빠져 값이 작아집니다.
 */
int32_t ThermalContrast(const FrameView& y16, const Rect& rect);

/** 발 하나가 따뜻한지의 판정입니다. */
enum class Warmth : uint8_t {
  kChecking,  // 아직 kWarmFrames장이 쌓이지 않음
  kWarm,      // 최근 kWarmFrames장이 모두 kWarmContrastMin 이상
  kCold,      // 최근 kWarmFrames장 중 kWarmContrastMin 미만이 있음
};

/**
 * 미리보기 열화상의 두 발 대비를 장마다 받아 발이 차가운지 판정합니다.
 *
 * 발 하나는 최근 kWarmFrames장이 모두 kWarmContrastMin 이상이면 따뜻함, 하나라도 미만이면 차가움입니다. 장이 모자라면
 * 확인 중입니다. 기준 근처에서 판정이 장마다 바뀌지 않게 하려는 것이고, 데우는 중인 발은 기준을 넘긴 상태가 이어져야
 * 따뜻함이 됩니다.
 */
class FootWarmthCheck {
 public:
  /** 쌓은 장을 지웁니다. 발 상자가 바뀌거나 열화상이 끊기면 부릅니다. */
  void Reset();

  /**
   * 한 장의 두 발 대비를 넣습니다.
   *
   * @param contrasts 화소 왼쪽 상자, 화소 오른쪽 상자의 ThermalContrast
   */
  void Push(const std::array<int32_t, 2>& contrasts);

  /** 화소 왼쪽(0), 오른쪽(1) 상자의 판정입니다. */
  Warmth State(int side) const;

  /** 화소 왼쪽(0), 오른쪽(1) 상자의 최근 kWarmFrames장 중 가장 작은 대비입니다. 장이 없으면 0입니다. */
  int32_t Lowest(int side) const;

 private:
  std::deque<std::array<int32_t, 2>> recent_;
};

}  // namespace hwlab::pixel

#endif  // HWLAB_PIXEL_FOOT_WARMTH_H_
