#ifndef HWLAB_PLATFORM_RGB_CAMERA_H_
#define HWLAB_PLATFORM_RGB_CAMERA_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "hwlab/camera_settings.h"
#include "hwlab/flow/settings_plan.h"
#include "hwlab/frame.h"
#include "hwlab/timestamp.h"

namespace hwlab::platform {

/** 카메라에서 받은 프레임 하나입니다. */
struct ReceivedFrame {
  FrameView view;  // 화소는 다음 ReadFrame 호출 전까지만 유효합니다
  Timestamp host_time;
  bool stream_gap = false;  // 드라이버가 이 프레임 앞에서 스트림이 끊겼다고 알림
};

/**
 * RGB 카메라 하나입니다.
 *
 * 설정은 core(flow::PlanCommands)가 만든 명령으로만 바꿉니다. 보통 직접 부르지 않고 Reconcile을 씁니다.
 */
class RgbCamera {
 public:
  /**
   * 장치를 찾아 무압축 UYVY로 엽니다. Windows 빌드에만 있습니다.
   *
   * @param name_contains 장치 이름에 들어 있는 문자열(예: "See3CAM_24CUG")
   * @param width 폭
   * @param height 높이
   * @param frame_rate 이 fps 이상을 낼 수 있는 형식만 엽니다. USB 2 속도로 연결되면 최대 fps가 낮아 열지 않습니다
   * @param error 장치를 찾았지만 열지 못하면 이유를 씁니다. 장치를 찾지 못했으면 비워 둡니다(아직 연결 전일 수 있음)
   * @return 연 카메라. 장치나 형식을 찾지 못하면 nullptr
   */
  static std::unique_ptr<RgbCamera> Open(const std::string& name_contains, int32_t width,
                                         int32_t height, int32_t frame_rate, std::string* error);

  virtual ~RgbCamera() = default;

  /** 장치 이름입니다. 사이드카와 화면에 남깁니다. */
  virtual std::string DeviceName() const = 0;

  /**
   * 프레임 하나를 기다려 받습니다.
   *
   * @return 받은 프레임. 연결이 끊기거나 스트림이 끝나면 nullopt
   * @note 수신 스레드에서만 부릅니다. 화소는 다음 ReadFrame 호출 전까지만 유효합니다.
   */
  virtual std::optional<ReceivedFrame> ReadFrame() = 0;

  /**
   * 끊긴 장치를 다시 찾아 같은 형식으로 엽니다. ReadFrame이 끊김을 알린 뒤 수신 스레드에서 부릅니다.
   *
   * @return 다시 열었으면 true. 장치가 아직 없으면 false
   * @note 다시 열린 카메라는 공장 기본 설정이므로 부른 쪽이 목표 상태로 다시 맞춰야 합니다.
   */
  virtual bool Reopen() = 0;

  /**
   * 장치를 찾았지만 촬영 선언의 형식으로 열지 못한 이유입니다. 예: USB 2 속도로 연결돼 UYVY 1920x1200이 최대 6fps
   *
   * @return 이유. 열었거나 장치를 찾지 못했으면 빈 문자열
   * @note 어느 스레드에서 불러도 됩니다.
   */
  virtual std::string OpenProblem() const { return {}; }

  /**
   * 카메라 설정을 다시 읽습니다.
   *
   * @return 읽은 설정. 읽지 못한 항목과 자동 노출 중의 노출값은 비어 있습니다
   * @note 제어 스레드에서만 부릅니다. 수백 ms 걸릴 수 있어 수신 스레드에서 부르면 프레임이 빠집니다.
   */
  virtual CameraState ReadState() = 0;

  /**
   * 카메라 설정을 저장값까지 다시 읽습니다. 실험 전 상태를 읽어 두고 되돌릴 때 씁니다.
   *
   * @return ReadState와 같지만 자동 노출 중에도 카메라에 저장된 노출값(마지막 수동 값)을 채웁니다
   * @note 이 노출값은 자동 노출이 실제로 쓰는 값이 아니므로 사이드카에 남기지 않습니다. 제어 스레드에서만 부릅니다.
   */
  virtual CameraState ReadStoredState() = 0;

  /**
   * 설정 명령 하나를 실행합니다.
   *
   * @return 카메라가 성공을 돌려주면 true. 실제로 그 값이 되었는지는 ReadState로 확인해야 합니다
   * @note 제어 스레드에서만 부릅니다.
   */
  virtual bool Execute(const flow::SettingCommand& command) = 0;

  /**
   * 수신을 끝냅니다. 기다리던 ReadFrame은 nullopt로 돌아오고, 이후 ReadFrame과 Reopen은 바로 실패합니다.
   *
   * 카메라가 프레임을 보내지 않으면 ReadFrame이 돌아오지 않아 종료가 끝나지 않았습니다(2026-10-27 노트북). 종료할 때
   * 수신 스레드가 정한 시간 안에 끝나지 않으면 부릅니다.
   *
   * @note 어느 스레드에서 불러도 됩니다. 장치 스트림을 닫으므로 이후 설정 명령(노출, 게인, 색온도)은 실패할 수 있습니다.
   */
  virtual void Interrupt() = 0;
};

/**
 * 카메라가 알린 최대 fps가 선언 fps보다 낮아 열지 못할 때 화면에 보일 안내입니다.
 *
 * 같은 카메라가 USB 3 속도로 연결되면 UYVY 1280x720을 60fps 이상으로 알리고 USB 2 속도로 연결되면 15fps로 알리므로, 최대 fps만 보고
 * USB 2 속도로 연결된 것으로 "보인다"고 추정해 말합니다. 포트가 USB 3여도 케이블, 허브, Type-C 방향이 원인일 수 있어 그 셋을 안내합니다.
 *
 * @param format 형식 이름(예: "UYVY 1280x720")
 * @param max_fps 그 크기 UYVY 형식들의 최대 fps 중 가장 큰 값
 * @param frame_rate 촬영 선언의 fps
 */
std::string LowFrameRateMessage(const std::string& format, double max_fps, int32_t frame_rate);

/** 목표 상태로 맞춘 결과입니다. */
struct ReconcileResult {
  std::vector<flow::SettingCommand> executed;
  std::vector<flow::SettingCommand> failed;
  CameraState readback;                     // 실행 뒤 다시 읽은 값
  std::vector<flow::SettingField> mismatches;  // 다시 읽은 값이 목표와 다른 항목
  // Restore에서 실행 전 값을 몰라 되돌리지도 비교하지도 않은 항목(flow::FindUnknownSaved). Reconcile은 비웁니다
  std::vector<flow::SettingField> unknown_saved;
};

/**
 * 카메라를 목표 상태로 맞춥니다. 미리보기 설정, 잠금, 실험 뒤 복원, 재연결 뒤 재설정이 모두 이 함수입니다.
 *
 * 지금 상태를 읽고, 목표와 다른 항목만 바꾸고(flow::PlanCommands), 다시 읽어 확인합니다(flow::FindMismatches).
 *
 * @param camera 맞출 카메라
 * @param target 목표 상태. 빈 항목은 바꾸지 않습니다
 * @return 실행한 명령, 실패한 명령, 다시 읽은 값, 목표와 다른 항목
 * @note 제어 스레드에서만 부릅니다.
 */
ReconcileResult Reconcile(RgbCamera& camera, const CameraState& target);

/**
 * 카메라를 실험 전에 읽어 둔 상태로 되돌립니다.
 *
 * 저장값까지 읽고(RgbCamera::ReadStoredState), flow::PlanRestore의 명령을 실행하고, 다시 읽어 모드와 저장값을 모두 비교합니다
 * (flow::FindRestoreMismatches).
 *
 * @param camera 되돌릴 카메라
 * @param saved 실험 전에 RgbCamera::ReadStoredState로 읽은 상태
 * @return 실행한 명령, 실패한 명령, 다시 읽은 값(저장값 포함), 실험 전과 다른 항목
 * @note 제어 스레드에서만 부릅니다.
 */
ReconcileResult Restore(RgbCamera& camera, const CameraState& saved);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_RGB_CAMERA_H_
