// 실기 확인 도구입니다. 촬영 선언 파일을 읽어 미리보기 뒤 촬영 버튼을 누르고, 끝나면 카메라를 실행 전 상태로 되돌립니다.
//
//   hwlab_capture [--fake] [--thermal] [--split] [--split-width <폭>] [--no-whole] <선언.json> <출력 폴더> <이름> [미리보기 장 수=90]
//
// --split은 프레임을 가운데에서 나눈 좌우 영상(<이름>_left, <이름>_right)을 함께 씁니다. --no-whole은 전체 화면 영상을 쓰지 않습니다.
// --split-width는 좌우 영역의 폭입니다. 주면 프레임 가운데 선의 양쪽으로 이 폭만큼 잡고, 없으면 프레임 폭의 절반입니다.
// 끝나면 되돌린 뒤 다시 읽은 카메라 설정을 출력하고, 실패한 명령과 실행 전과 다른 항목을 이름으로 출력합니다.
// --thermal은 열화상을 함께 녹화합니다(<이름>_thermal.raw16, _thermal.json, _thermal.avi, 카메라 온도를 주면 _thermal_camera_temp.csv).
// --fake는 RGB와 열화상 모두 PC 시계를 쓰는 가짜 장치로 실행합니다. Windows가 아닌 OS에서는 --fake만 됩니다.
// --replay <화면 왼쪽 영상> <화면 오른쪽 영상>은 --fake와 같지만 RGB 화소를 실제 녹화의 좌우 발 영상(FFV1 MKV)에서 가져옵니다.
//   두 영상의 앞 60장을 --split 좌우 영역에 넣은 전체 프레임으로 만들어 차례로 돌려 냅니다. 카메라 없이 실제 화면과 같은
//   압축 부하로 쓰기 대기열을 잴 때 씁니다. --split이 필요합니다.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "hwlab/flow/capture_spec.h"
#include "hwlab/flow/settings_plan.h"
#include "hwlab/pixel/crop.h"
#include "hwlab/pixel/rect.h"
#include "hwlab/platform/capture.h"
#include "hwlab/platform/fake_camera.h"
#include "hwlab/platform/fake_thermal_camera.h"
#include "hwlab/platform/rgb_camera.h"
#include "hwlab/platform/thermal_camera.h"
#include "hwlab/platform/video_reader.h"

namespace {

const char* NameOf(hwlab::platform::ThermalConnection connection) {
  switch (connection) {
    case hwlab::platform::ThermalConnection::kClosed: return "closed";
    case hwlab::platform::ThermalConnection::kWaiting: return "waiting";
    case hwlab::platform::ThermalConnection::kReceiving: return "receiving";
    case hwlab::platform::ThermalConnection::kLost: return "lost";
  }
  return "";
}

template <typename T>
long long Or(const std::optional<T>& value) {
  return value.has_value() ? static_cast<long long>(*value) : -99999;
}

// 실기에서 되돌린 결과를 시작 전 값과 맞춰 보도록 모드와 저장값을 모두 출력합니다. 읽지 못한 값은 -99999입니다.
void PrintRestore(const hwlab::platform::ReconcileResult& restore) {
  const hwlab::CameraState& r = restore.readback;
  std::printf("restore exposure_auto=%lld exposure_step=%lld gain=%lld white_balance_auto=%lld white_balance_k=%lld sdk_frame_rate=%lld locked=%lld\n",
              Or(r.exposure_auto), Or(r.exposure_step), Or(r.gain), Or(r.white_balance_auto), Or(r.white_balance_k),
              Or(r.sdk_frame_rate), Or(r.locked));
  for (const auto& command : restore.failed) {
    std::printf("restore_failed_command=%s value=%d\n", hwlab::flow::NameOf(command.kind), command.value);
  }
  for (const auto field : restore.mismatches) {
    std::printf("restore_mismatch=%s\n", hwlab::flow::NameOf(field));
  }
  for (const auto field : restore.unknown_saved) {
    std::printf("restore_unknown_saved=%s\n", hwlab::flow::NameOf(field));
  }
}

// 실제 녹화의 화소를 내보내는 가짜 카메라입니다. 시각, 설정 명령, 끊김은 FakeRgbCamera가 맡고 화소만 바꿉니다.
class ReplayCamera final : public hwlab::platform::RgbCamera {
 public:
  ReplayCamera(hwlab::platform::FakeCameraScript script, std::vector<std::vector<uint8_t>> frames)
      : fake_(std::move(script)), frames_(std::move(frames)) {}

  std::string DeviceName() const override { return "ReplayCamera"; }
  std::optional<hwlab::platform::ReceivedFrame> ReadFrame() override {
    auto frame = fake_.ReadFrame();
    if (!frame) return frame;
    frame->view.plane[0] = frames_[sent_++ % frames_.size()].data();
    return frame;
  }
  bool Reopen() override { return fake_.Reopen(); }
  hwlab::CameraState ReadState() override { return fake_.ReadState(); }
  hwlab::CameraState ReadStoredState() override { return fake_.ReadStoredState(); }
  bool Execute(const hwlab::flow::SettingCommand& command) override { return fake_.Execute(command); }
  void Interrupt() override { fake_.Interrupt(); }

 private:
  hwlab::platform::FakeRgbCamera fake_;
  std::vector<std::vector<uint8_t>> frames_;
  size_t sent_ = 0;
};

// 두 영상의 앞 count장을 전체 프레임(UYVY)의 두 영역에 넣습니다. 영역 밖은 검은색(Y 16, U V 128)입니다.
std::vector<std::vector<uint8_t>> LoadReplayFrames(const std::string& left_path, const std::string& right_path,
                                                   const hwlab::pixel::Rect& left, const hwlab::pixel::Rect& right,
                                                   int32_t width, int32_t height, int32_t count, std::string* error) {
  std::vector<std::vector<uint8_t>> frames;
  struct Source {
    const std::string& path;
    const hwlab::pixel::Rect& rect;
    std::unique_ptr<hwlab::platform::VideoReader> reader;
  };
  Source sources[2] = {{left_path, left, nullptr}, {right_path, right, nullptr}};
  for (auto& source : sources) {
    source.reader = hwlab::platform::MakeVideoReader(error);
    if (!source.reader || !source.reader->Open(source.path, error)) return {};
    if (source.reader->Width() != source.rect.width || source.reader->Height() != source.rect.height) {
      *error = source.path + ": 영상 크기가 좌우 영역과 다릅니다";
      return {};
    }
  }
  const size_t row_bytes = static_cast<size_t>(width) * 2;
  std::vector<uint8_t> blank(row_bytes * static_cast<size_t>(height));
  for (size_t i = 0; i < blank.size(); i += 2) {
    blank[i] = 128;
    blank[i + 1] = 16;
  }
  std::vector<uint8_t> part;
  for (int32_t i = 0; i < count; ++i) {
    std::vector<uint8_t> frame = blank;
    for (auto& source : sources) {
      if (!source.reader->ReadFrame(i, &part)) {
        *error = source.path + ": " + std::to_string(i) + "번 장을 읽지 못했습니다";
        return {};
      }
      const size_t part_row = static_cast<size_t>(source.rect.width) * 2;
      for (int32_t y = 0; y < source.rect.height; ++y) {
        std::copy_n(part.data() + static_cast<size_t>(y) * part_row, part_row,
                    frame.data() + static_cast<size_t>(source.rect.y + y) * row_bytes + static_cast<size_t>(source.rect.x) * 2);
      }
    }
    frames.push_back(std::move(frame));
  }
  return frames;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace hwlab;
  bool fake = false;
  bool with_thermal = false;
  bool split = false;
  bool whole = true;
  int32_t split_width = 0;
  std::string replay_left;
  std::string replay_right;
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--fake") {
      fake = true;
    } else if (arg == "--thermal") {
      with_thermal = true;
    } else if (arg == "--split") {
      split = true;
    } else if (arg == "--split-width" && i + 1 < argc) {
      split = true;
      split_width = std::atoi(argv[++i]);
    } else if (arg == "--replay" && i + 2 < argc) {
      fake = true;
      replay_left = argv[++i];
      replay_right = argv[++i];
    } else if (arg == "--no-whole") {
      whole = false;
    } else {
      args.push_back(arg);
    }
  }
  if (args.size() < 3) {
    std::printf("사용법: hwlab_capture [--fake] [--replay <왼쪽 영상> <오른쪽 영상>] [--thermal] [--split] [--split-width <폭>] [--no-whole] <선언.json> <출력 폴더> <이름> [미리보기 장 수=90]\n");
    return 2;
  }
  std::ifstream file(args[0], std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  std::string error;
  const auto spec = flow::ParseCaptureSpec(text.str(), &error);
  if (!spec) {
    std::printf("선언 파일 오류: %s\n", error.c_str());
    return 2;
  }
#ifdef _WIN32
  const std::string base = args[1] + "\\" + args[2];
#else
  const std::string base = args[1] + "/" + args[2];
#endif
  const int preview_frames = args.size() > 3 ? std::atoi(args[3].c_str()) : 90;

  // 좌우 영역은 --split과 --split-width로 정합니다. 재생 화소도 이 영역에 넣습니다.
  const int32_t area_width = split_width > 0 ? split_width * 2 : spec->width;
  const pixel::Rect area{(spec->width - area_width) / 2, 0, area_width, spec->height};
  const auto [split_left, split_right] = pixel::SplitLeftRight(area, PixelFormat::kUyvy, 2);
  if (!replay_left.empty() && !split) {
    std::printf("--replay에는 --split이 필요합니다\n");
    return 2;
  }

  std::unique_ptr<platform::RgbCamera> camera;
  if (!replay_left.empty()) {
    platform::FakeCameraScript script;
    script.width = spec->width;
    script.height = spec->height;
    script.pc_clock = true;
    script.wall_period_us = 1'000'000 / spec->settings.sdk_frame_rate;
    auto frames = LoadReplayFrames(replay_left, replay_right, split_left, split_right, spec->width, spec->height, 60, &error);
    if (frames.empty()) {
      std::printf("재생 영상 읽기 실패: %s\n", error.c_str());
      return 1;
    }
    camera = std::make_unique<ReplayCamera>(script, std::move(frames));
  } else if (fake) {
    platform::FakeCameraScript script;
    script.width = spec->width;
    script.height = spec->height;
    script.pc_clock = true;
    script.wall_period_us = 1'000'000 / spec->settings.sdk_frame_rate;
    camera = std::make_unique<platform::FakeRgbCamera>(script);
  } else {
#ifdef _WIN32
    camera = platform::RgbCamera::Open(spec->device, spec->width, spec->height, spec->settings.sdk_frame_rate, &error);
    if (!camera && error.empty()) error = "장치를 찾지 못함: " + spec->device;
#else
    error = "이 OS에는 실제 RGB 카메라 구현이 없습니다. --fake로 실행합니다";
#endif
  }
  if (!camera) {
    std::printf("카메라 열기 실패: %s\n", error.c_str());
    return 1;
  }
  std::unique_ptr<platform::ThermalCamera> thermal;
  if (with_thermal) {
    if (fake) {
      platform::FakeThermalScript script;
      script.pc_clock = true;
      thermal = std::make_unique<platform::FakeThermalCamera>(script);
    } else {
      thermal = platform::MakeDeviceThermalCamera();
    }
    if (!thermal) {
      std::printf("열화상 카메라 없음: 이 OS에는 열화상 구현이 없습니다\n");
      return 1;
    }
  }

  platform::Capture capture(*camera, *spec, thermal.get());
  if (!capture.Start(&error)) {
    std::printf("시작 실패: %s\n", error.c_str());
    return 1;
  }
  while (capture.Status().received < preview_frames) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  platform::RecordingPaths paths;
  if (whole || !split) {
    paths.video = base + "." + flow::ExtensionOf(spec->codec);
    paths.sidecar = base + ".json";
  }
  if (split) {
    const pixel::Rect& left = split_left;
    const pixel::Rect& right = split_right;
    std::printf("split left=%d,%d,%dx%d right=%d,%d,%dx%d\n", left.x, left.y, left.width, left.height, right.x, right.y,
                right.width, right.height);
    if (!pixel::IsPortrait(left) || !pixel::IsPortrait(right)) {
      std::printf("좌우 영역이 세로로 길지 않습니다(%dx%d). 관류 분석은 세로로 긴 영상만 받습니다\n", left.width, left.height);
    }
    // 화면 왼쪽이 환자의 어느 발인지는 촬영 정보가 정하므로 이 도구는 발을 적지 않습니다.
    const std::string ext = std::string(".") + flow::ExtensionOf(spec->codec);
    paths.crops.push_back({left, base + "_left" + ext, base + "_left.json", std::nullopt});
    paths.crops.push_back({right, base + "_right" + ext, base + "_right.json", std::nullopt});
  }
  if (with_thermal) {
    paths.thermal_raw = base + "_thermal.raw16";
    paths.thermal_sidecar = base + "_thermal.json";
    paths.thermal_preview = base + "_thermal.avi";
  }
  capture.StartRecording(paths);
  const bool finished = capture.WaitFinished(std::chrono::seconds(60 + spec->target_frames / 10));
  const platform::CaptureStatus status = capture.Status();
  const platform::ReconcileResult restore = capture.Stop();

  std::printf("finished=%d written=%lld missing=%lld dropped=%lld reason=%d mismatches=%zu restore_failed=%zu restore_mismatches=%zu error=%s\n",
              finished ? 1 : 0, static_cast<long long>(status.written), static_cast<long long>(status.missing),
              static_cast<long long>(status.dropped), status.end_reason ? static_cast<int>(*status.end_reason) : -1,
              status.mismatches.size(), restore.failed.size(), restore.mismatches.size(), status.error.c_str());
  PrintRestore(restore);
  if (with_thermal) {
    const platform::ThermalStatus& t = status.thermal;
    std::printf("thermal connection=%s received=%lld written=%lld missing=%lld dropped=%lld reconnects=%d fps=%.2f error=%s\n",
                NameOf(t.connection), static_cast<long long>(t.received), static_cast<long long>(t.written),
                static_cast<long long>(t.missing), static_cast<long long>(t.dropped), t.reconnects, t.measured_fps,
                t.error.c_str());
  }
  return finished && restore.mismatches.empty() ? 0 : 1;
}
