#ifndef HWLAB_APP_LATE_RGB_CAMERA_H_
#define HWLAB_APP_LATE_RGB_CAMERA_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "hwlab/platform/rgb_camera.h"

/**
 * 앱을 켤 때 RGB 카메라가 없어도 앱이 뜨게 하는 카메라입니다. 장치를 찾을 때까지 끊긴 카메라처럼 동작합니다.
 *
 * ReadFrame은 장치가 없으면 바로 nullopt를 돌려줍니다. 그러면 Capture가 끊김으로 보고 0.5초마다 Reopen을 부르고,
 * Reopen이 장치를 처음 열면 재연결 사건으로 목표 설정을 다시 맞춥니다. 장치를 연 뒤에는 모든 호출을 그 장치에 넘깁니다.
 *
 * 장치는 수신 스레드(Reopen)에서 한 번만 정하고 바꾸지 않으므로, 제어 스레드와 화면 스레드는 원자 포인터로 읽습니다.
 */
class LateRgbCamera final : public hwlab::platform::RgbCamera {
 public:
  using Opener = std::function<std::unique_ptr<hwlab::platform::RgbCamera>(std::string* problem)>;

  /**
   * @param name 장치를 열기 전에 보일 이름(촬영 선언의 device)
   * @param opener 장치를 찾아 여는 함수. 없으면 nullptr를 돌려주고, 장치를 찾았지만 열지 못했으면 problem에 이유를 씁니다
   */
  LateRgbCamera(std::string name, Opener opener) : name_(std::move(name)), opener_(std::move(opener)) {
    TryOpen();
  }

  /** 장치를 열었으면 true입니다. */
  bool Opened() const { return opened_.load() != nullptr; }

  std::string DeviceName() const override {
    const auto* camera = opened_.load();
    return camera != nullptr ? camera->DeviceName() : name_;
  }

  std::optional<hwlab::platform::ReceivedFrame> ReadFrame() override {
    auto* camera = opened_.load();
    return camera != nullptr ? camera->ReadFrame() : std::nullopt;
  }

  bool Reopen() override {
    auto* camera = opened_.load();
    if (camera != nullptr) return camera->Reopen();
    return !interrupted_ && TryOpen();
  }

  std::string OpenProblem() const override {
    const auto* camera = opened_.load();
    if (camera != nullptr) return camera->OpenProblem();
    std::lock_guard<std::mutex> lock(problem_mutex_);
    return problem_;
  }

  // 장치를 열기 전에는 읽은 값이 없고 명령은 실패합니다. 장치를 연 뒤 재연결 사건이 목표 설정을 다시 맞춥니다.
  hwlab::CameraState ReadState() override {
    auto* camera = opened_.load();
    return camera != nullptr ? camera->ReadState() : hwlab::CameraState{};
  }
  hwlab::CameraState ReadStoredState() override {
    auto* camera = opened_.load();
    return camera != nullptr ? camera->ReadStoredState() : hwlab::CameraState{};
  }
  bool Execute(const hwlab::flow::SettingCommand& command) override {
    auto* camera = opened_.load();
    return camera != nullptr && camera->Execute(command);
  }
  // 장치를 열기 전이면 이후에 열지 않습니다. 수신 스레드가 Reopen 중에 열면 그 장치를 이어서 닫지 않으나, Reopen은 열자마자 돌아옵니다.
  void Interrupt() override {
    interrupted_ = true;
    auto* camera = opened_.load();
    if (camera != nullptr) camera->Interrupt();
  }

 private:
  bool TryOpen() {
    std::string problem;
    inner_ = opener_(&problem);
    {
      std::lock_guard<std::mutex> lock(problem_mutex_);
      problem_ = problem;
    }
    if (!inner_) return false;
    opened_.store(inner_.get());
    return true;
  }

  const std::string name_;
  const Opener opener_;
  std::unique_ptr<hwlab::platform::RgbCamera> inner_;  // 한 번 정하면 바꾸지 않습니다
  std::atomic<hwlab::platform::RgbCamera*> opened_{nullptr};
  std::atomic<bool> interrupted_{false};
  // 장치를 열기 전의 열기 실패 이유입니다. 수신 스레드가 쓰고 화면 스레드가 읽습니다.
  mutable std::mutex problem_mutex_;
  std::string problem_;
};

#endif  // HWLAB_APP_LATE_RGB_CAMERA_H_
