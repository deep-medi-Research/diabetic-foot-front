#ifndef HWLAB_APP_SCRIPTED_RGB_CAMERA_H_
#define HWLAB_APP_SCRIPTED_RGB_CAMERA_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "hwlab/platform/rgb_camera.h"

/**
 * 가짜 RGB 카메라 앞에 두어 정한 종류의 설정 명령을 실패시키는 카메라입니다. 명령줄 시험 옵션(--fake-restore-fails)에만 씁니다.
 *
 * 앱 종료 때 복원이 맞지 않는 경우(실패한 명령, 실행 전과 다른 항목)의 안내 화면을 장비 없이 재현합니다.
 */
class ScriptedRgbCamera final : public hwlab::platform::RgbCamera {
 public:
  /**
   * @param inner 실제로 프레임을 내는 카메라(보통 가짜 RGB)
   * @param failing 이 종류의 명령은 실행하지 않고 실패를 돌려줍니다
   */
  ScriptedRgbCamera(std::unique_ptr<hwlab::platform::RgbCamera> inner, hwlab::flow::CommandKind failing)
      : inner_(std::move(inner)), failing_(failing) {}

  std::string DeviceName() const override { return inner_->DeviceName(); }
  std::optional<hwlab::platform::ReceivedFrame> ReadFrame() override { return inner_->ReadFrame(); }
  bool Reopen() override { return inner_->Reopen(); }
  void Interrupt() override { inner_->Interrupt(); }
  hwlab::CameraState ReadState() override { return inner_->ReadState(); }
  hwlab::CameraState ReadStoredState() override { return inner_->ReadStoredState(); }
  bool Execute(const hwlab::flow::SettingCommand& command) override {
    return command.kind != failing_ && inner_->Execute(command);
  }

 private:
  std::unique_ptr<hwlab::platform::RgbCamera> inner_;
  const hwlab::flow::CommandKind failing_;
};

#endif  // HWLAB_APP_SCRIPTED_RGB_CAMERA_H_
