// TE-EV1 열화상 카메라를 i3 SDK(i3system_TE_x64.dll)로 받는 ThermalCamera 구현입니다.
// SDK 이름과 타입은 이 파일 밖으로 내보내지 않습니다.
#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "hwlab/platform/shot_folder.h"
#include "hwlab/platform/thermal_camera.h"
#include "i3system_TE.h"

namespace hwlab::platform {
namespace {

// SDK DLL의 내보낸 이름은 장식이 없는 C 이름이라 GetProcAddress로 찾습니다(가져오기 라이브러리를 쓰지 않음).
using ScanFn = int (*)(i3::SCANINFO*);
using OpenAFn = i3::TE_A* (*)(int);

// RecvImage 반환값입니다. 1은 받음, 2는 아직 준비 안 됨, 3과 4는 크기 오류와 읽기 실패입니다.
constexpr int kRecvOk = 1;

// SDK 헤더에는 상수만 있고 문서에는 없는 셔터 모드입니다. 실행 중 자동 교정을 멈추는 데 씁니다.
constexpr unsigned short kManualShutter = I3_MANUAL_SHUTTER;
// GetSetting이 구조체에 쓰지 않으면 이 값이 남습니다(기존 진단 도구 실측). 원래 모드를 모르는 상태로 봅니다.
constexpr unsigned short kUnreadSetting = 0xABAB;

// 셔터 PT100 원시값을 °C로 옮기는 식입니다. 원시값 22981에서 GetShutterPt100Temp가 돌려준 36.628 °C와 같습니다.
// GetShutterPt100Temp는 한 번에 약 2초 걸려(TE-EV1 실측) 수신 스레드에서 부르지 않고 이 식으로 계산합니다.
constexpr double kShutterCelsiusPerCount = 0.005417;
constexpr double kShutterCelsiusOffset = 87.8602;

// 읽지 못했거나 모두 0(간격과 온도차가 SDK 허용 범위 밖)이면 되돌릴 원래 설정을 모르는 것으로 봅니다.
bool SettingKnown(const i3::TE_SETTING& s) {
  if (s.shutterMode == kUnreadSetting) return false;
  return !(s.shutterMode == 0 && s.shutterTime == 0 && s.shutterTemp == 0);
}

// SDK는 프레임을 보관하지 않으므로 다음 장이 나오기 전에 다시 물어야 합니다. 10ms는 24fps, 2ms는 30.1fps(기존 앱 실측).
constexpr int64_t kPollGapUs = 2'000;

int64_t HostMicros() {
  static const LARGE_INTEGER frequency = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f;
  }();
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return now.QuadPart / frequency.QuadPart * 1'000'000 +
         now.QuadPart % frequency.QuadPart * 1'000'000 / frequency.QuadPart;
}

// RGB 카메라 구현과 같은 QPC 시계를 써야 두 스트림의 수신 시각을 비교할 수 있습니다.
Timestamp HostNow() { return Timestamp{HostMicros()}; }

// SDK는 경로를 시스템 코드 페이지(char*)로 받습니다. 그 코드 페이지로 바꿀 수 없는 글자가 있으면 nullopt입니다.
// 코드 페이지가 UTF-8이면(Windows의 UTF-8 사용 설정) 플래그와 대체 문자 인자를 쓸 수 없고 바꿀 수 없는 글자도 없습니다.
std::optional<std::string> SdkPath(const std::filesystem::path& path) {
  const std::wstring wide = path.wstring();
  if (wide.empty()) return std::nullopt;
  const bool utf8 = GetACP() == CP_UTF8;
  const DWORD flags = utf8 ? 0 : WC_NO_BEST_FIT_CHARS;
  BOOL lossy = FALSE;
  const int n = WideCharToMultiByte(CP_ACP, flags, wide.c_str(), -1, nullptr, 0, nullptr, utf8 ? nullptr : &lossy);
  if (n <= 0 || lossy) return std::nullopt;
  std::string out(static_cast<size_t>(n), '\0');
  if (WideCharToMultiByte(CP_ACP, flags, wide.c_str(), -1, out.data(), n, nullptr, utf8 ? nullptr : &lossy) != n) {
    return std::nullopt;
  }
  out.resize(static_cast<size_t>(n - 1));
  return out;
}

// 보정 파일의 수정 시각(UTC 유닉스 초)입니다. 화면과 사이드카에 보정한 날짜로 보입니다.
// std::chrono::clock_cast는 MSVC에서 윤초 표를 읽으려고 icu.dll을 쓰고 없으면 예외를 던져 Windows API로 읽습니다.
std::optional<int64_t> FileUnixSeconds(const std::filesystem::path& path) {
  WIN32_FILE_ATTRIBUTE_DATA data;
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return std::nullopt;
  ULARGE_INTEGER ticks;  // 1601-01-01 UTC부터 100ns 단위
  ticks.LowPart = data.ftLastWriteTime.dwLowDateTime;
  ticks.HighPart = data.ftLastWriteTime.dwHighDateTime;
  constexpr int64_t kUnixEpochSeconds = 11'644'473'600;  // 1601-01-01부터 1970-01-01까지
  return static_cast<int64_t>(ticks.QuadPart / 10'000'000) - kUnixEpochSeconds;
}

bool IsRawModel(unsigned int prod_ver) {
  // 원시값 16비트를 주는 RecvImage(buf, false)는 타입 A(EQ1, EV1, EQ2, EV2)에만 있습니다.
  return prod_ver == I3_TE_EQ1 || prod_ver == I3_TE_EV1 || prod_ver == I3_TE_EQ2 || prod_ver == I3_TE_EV2;
}

std::string ModelName(unsigned int prod_ver, unsigned int core_id) {
  const char* model = "TE";
  switch (prod_ver) {
    case I3_TE_EQ1: model = "TE-EQ1"; break;
    case I3_TE_EV1: model = "TE-EV1"; break;
    case I3_TE_EQ2: model = "TE-EQ2"; break;
    case I3_TE_EV2: model = "TE-EV2"; break;
    default: break;
  }
  char text[64];
  std::snprintf(text, sizeof(text), "%s-%06u", model, core_id);
  return text;
}

std::wstring ExecutableFolder() {
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) return L"";
    if (length < buffer.size()) {
      std::wstring path(buffer.data(), length);
      const size_t slash = path.find_last_of(L"\\/");
      return slash == std::wstring::npos ? L"" : path.substr(0, slash + 1);
    }
    buffer.resize(buffer.size() * 2);
  }
}

// 2ms를 지키는 대기입니다. sleep_for는 Windows 타이머 단위(15.6ms)로 깨어나 30Hz 한 주기를 넘깁니다.
class PollWait {
 public:
  PollWait()
      : timer_(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS)) {}
  ~PollWait() {
    if (timer_ != nullptr) CloseHandle(timer_);
  }
  PollWait(const PollWait&) = delete;
  PollWait& operator=(const PollWait&) = delete;

  void Wait(int64_t us) {
    if (timer_ != nullptr) {
      LARGE_INTEGER due;
      due.QuadPart = -us * 10;  // 상대 시간, 100ns 단위
      if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE) != 0) {
        WaitForSingleObject(timer_, INFINITE);
        return;
      }
    }
    std::this_thread::sleep_for(std::chrono::microseconds(us));
  }

 private:
  HANDLE timer_;
};

class WindowsThermalCamera final : public ThermalCamera {
 public:
  explicit WindowsThermalCamera(const pixel::CalibrationRule& rule) : calibration_rule_(rule) {}
  ~WindowsThermalCamera() override {
    Close();
    if (module_ != nullptr) FreeLibrary(module_);
  }
  WindowsThermalCamera(const WindowsThermalCamera&) = delete;
  WindowsThermalCamera& operator=(const WindowsThermalCamera&) = delete;

  bool Open(std::string* error) override {
    Close();
    std::lock_guard<std::mutex> lock(device_mutex_);
    if (!LoadModule(error)) return false;

    std::vector<i3::SCANINFO> devices(MAX_USB_NUM);
    if (scan_(devices.data()) != 1) return Fail(error, "열화상 카메라를 찾지 못했습니다(장치 탐색 실패)");
    int handle = -1;
    unsigned int prod_ver = 0;
    unsigned int core_id = 0;
    for (int i = 0; i < MAX_USB_NUM; ++i) {
      const i3::SCANINFO& slot = devices[static_cast<size_t>(i)];
      if (slot.bDevCon == 0) continue;
      handle = i;
      prod_ver = slot.nProdVer;
      core_id = slot.nCoreID;
      break;
    }
    if (handle < 0) return Fail(error, "열화상 카메라가 연결되어 있지 않습니다");
    if (!IsRawModel(prod_ver)) {
      return Fail(error, "원시값을 주지 않는 열화상 모델입니다(제품 번호 " + std::to_string(prod_ver) + ")");
    }
    device_ = open_a_(handle);
    if (device_ == nullptr) return Fail(error, "열화상 카메라를 열지 못했습니다. 다른 프로그램이 쓰고 있을 수 있습니다");

    const int width = device_->GetImageWidth();
    const int height = device_->GetImageHeight();
    if (width <= 0 || height <= 0) {
      CloseDevice();
      return Fail(error, "열화상 카메라가 잘못된 해상도를 보고했습니다");
    }
    // 기존 앱은 열자마자 장치 설정(플래시)을 읽은 뒤에 영상을 받았습니다. 같은 순서를 지킵니다.
    i3::TE_SETTING setting;
    std::memset(&setting, 0xAB, sizeof(setting));
    device_->GetSetting(&setting);
    setting_ = setting;
    held_ = false;
    watching_ = false;
    last_calibration_.reset();
    last_temperature_.reset();
    device_width_ = width;
    device_height_ = height;
    pixels_.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    // 연 직후 한 번 교정하고, 교정 구간이 끝날 때까지 받은 프레임의 내용으로 판정한 뒤 반환합니다.
    // 연 직후 교정 뒤에는 값 변화가 교정 없는 구간 수준이라(TE-EV1 실측 5회) 새 프레임을 버리지 않습니다.
    if (device_->ShutterCalibrationOn()) {
      pixel::CalibrationRule open_rule = calibration_rule_;
      open_rule.discard_frames = 0;
      pixel::CalibrationWatch watch;
      const int64_t began_us = HostMicros();
      int64_t last_us = began_us;
      while (!pixel::IsCalibrationFinished(watch)) {
        if (device_->RecvImage(pixels_.data(), false) == kRecvOk) {
          last_us = HostMicros();
          watch = pixel::AfterCalibrationFrame(watch, pixel::FrameSignature(PixelView(width, height)), open_rule);
          continue;
        }
        // 프레임이 오지 않는 경우만 시간으로 판정합니다(끊김 판정과 같은 기준).
        const int64_t limit_us = watch.received == 0 ? rule_.first_frame_us : rule_.stall_us;
        if (HostMicros() - last_us >= limit_us) {
          CloseDevice();
          return Fail(error, "열화상 카메라가 연 뒤 셔터 교정 중에 프레임을 주지 않습니다");
        }
        poll_wait_.Wait(kPollGapUs);
      }
      last_calibration_ = watch;
    }
    // 보정 파일은 장면과 상관없이 불러올 수 있어 다시 열 때도 불러옵니다. 불러온 보정은 셔터 교정 뒤에도 남습니다(실측).
    const std::string name = ModelName(prod_ver, core_id);
    std::optional<LensShadingStatus> shading;
    if (!lens_shading_folder_.empty()) shading = LoadLensShading(name);

    {
      std::lock_guard<std::mutex> state_lock(state_mutex_);
      name_ = name;
      lens_shading_ = shading;
      width_ = width;
      height_ = height;
    }
    opened_at_us_.store(HostMicros());
    last_frame_us_.store(kNoFrame);
    open_.store(true);
    return true;
  }

  void Close() override {
    // 먼저 내려 두어 Connection이 닫는 동안에도 kClosed를 돌려줍니다.
    open_.store(false);
    std::lock_guard<std::mutex> lock(device_mutex_);
    CloseDevice();
    std::lock_guard<std::mutex> state_lock(state_mutex_);
    name_.clear();
    width_ = 0;
    height_ = 0;
    lens_shading_.reset();
  }

  std::string DeviceName() const override {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return name_;
  }

  int32_t Width() const override {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return width_;
  }

  int32_t Height() const override {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return height_;
  }

  void SetLensShadingFolder(const std::string& folder) override {
    std::lock_guard<std::mutex> lock(device_mutex_);
    lens_shading_folder_ = folder;
  }

  std::optional<LensShadingStatus> LensShading() const override {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return lens_shading_;
  }

  bool CorrectLensShading(const std::optional<timing::LensShadingConditions>& made, std::string* error) override {
    std::lock_guard<std::mutex> lock(device_mutex_);
    if (device_ == nullptr) return Fail(error, "열화상 카메라가 열려 있지 않습니다");
    if (lens_shading_folder_.empty()) return Fail(error, "렌즈 셰이딩 보정 파일 폴더가 정해지지 않았습니다");
    std::string name;
    {
      std::lock_guard<std::mutex> state_lock(state_mutex_);
      name = name_;
    }
    // device_mutex_를 잡은 동안은 RecvImage가 불리지 않습니다. 실기에서 이 상태로 부른 보정이 성공했습니다.
    const bool corrected = device_->LensShadingCorrection();
    // 보정하는 1.5~1.9초 동안 프레임을 받지 못하므로, 끊김 기준(stall_us)에 걸려 닫고 다시 열지 않게 지금 받은 것으로 둡니다.
    last_frame_us_.store(HostMicros());
    if (!corrected) {
      SetLensShading({{record::LensShadingSource::kFailed, std::nullopt}, "렌즈 셰이딩 보정이 실패했습니다"});
      return Fail(error, "렌즈 셰이딩 보정이 실패했습니다");
    }
    LensShadingStatus status{{record::LensShadingSource::kCorrected, std::nullopt}, std::string(), made};
    const std::filesystem::path path = LensShadingPath(name);
    // 저장이 중간에 실패해도 앞의 보정 파일이 남도록 임시 파일에 쓴 뒤 바꿔 넣습니다.
    std::filesystem::path temp = path;
    temp += L".tmp";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::optional<std::string> sdk_path = SdkPath(temp);
    if (!sdk_path) {
      status.error = "보정은 적용했지만 보정 파일 경로에 SDK가 쓸 수 없는 글자가 있어 저장하지 못했습니다";
    } else if (!device_->SaveCalibration(sdk_path->c_str())) {
      status.error = "보정은 적용했지만 보정 파일을 저장하지 못했습니다";
    } else if (std::filesystem::rename(temp, path, ec); ec) {
      status.error = "보정은 적용했지만 저장한 보정 파일을 바꿔 넣지 못했습니다";
    } else {
      status.record.file_unix_s = FileUnixSeconds(path);
      if (made && status.record.file_unix_s && !WriteLensShadingConditions(path, *status.record.file_unix_s, *made)) {
        status.error = "보정 파일은 저장했지만 보정 때의 카메라 온도를 저장하지 못했습니다";
      }
    }
    if (!status.error.empty()) std::filesystem::remove(temp, ec);
    SetLensShading(status);
    if (error != nullptr) *error = status.error;
    return true;
  }

  std::optional<ReceivedFrame> ReadFrame(int64_t timeout_us) override {
    const int64_t deadline_us = HostMicros() + timeout_us;
    for (;;) {
      int code = 0;
      int64_t received_us = 0;
      int32_t width = 0;
      int32_t height = 0;
      {
        std::lock_guard<std::mutex> lock(device_mutex_);
        if (device_ == nullptr) return std::nullopt;
        code = device_->RecvImage(pixels_.data(), false);
        received_us = HostMicros();
        width = device_width_;
        height = device_height_;
        if (code == kRecvOk) {
          // GetShutterPt100Temp와 달리 두 함수는 셔터를 닫지 않아 프레임마다 부릅니다.
          const unsigned short shutter_raw = device_->GetShutterPt100RawValue();
          last_temperature_ = CameraTemperature{device_->GetFpaTemp(), shutter_raw,
                                                shutter_raw * kShutterCelsiusPerCount - kShutterCelsiusOffset};
        }
        if (code == kRecvOk && watching_) {
          watch_ = pixel::AfterCalibrationFrame(watch_, pixel::FrameSignature(PixelView(width, height)), calibration_rule_);
          if (pixel::IsCalibrationFinished(watch_)) {
            watching_ = false;
            watch_cv_.notify_all();
          }
        }
      }
      if (code == kRecvOk) {
        last_frame_us_.store(received_us);
        ReceivedFrame frame;
        frame.view.format = PixelFormat::kY16;
        frame.view.width = width;
        frame.view.height = height;
        // x64는 리틀 엔디언이라 SDK의 unsigned short 배열이 그대로 Y16 바이트 순서입니다.
        frame.view.plane[0] = reinterpret_cast<const uint8_t*>(pixels_.data());
        frame.view.row_stride[0] = width * 2;
        frame.view.stream = StreamKind::kThermal;
        frame.view.index = static_cast<FrameIndex>(next_index_++);
        // 장치가 프레임 시각을 주지 않으므로 RecvImage가 돌아온 PC 시각을 씁니다.
        frame.view.time = Timestamp{received_us};
        frame.host_time = frame.view.time;
        return frame;
      }
      if (HostMicros() >= deadline_us) return std::nullopt;
      poll_wait_.Wait(kPollGapUs);
    }
  }

  std::optional<CameraTemperature> LastCameraTemperature() const override { return last_temperature_; }

  bool CalibrateNow(std::string* error) override {
    std::unique_lock<std::mutex> lock(device_mutex_);
    if (device_ == nullptr) return Fail(error, "열화상 카메라가 열려 있지 않습니다");
    // SDK 문서: 교정은 수신을 멈춘 상태에서만 됩니다. device_mutex_를 잡은 동안은 RecvImage가 불리지 않습니다.
    if (!device_->ShutterCalibrationOn()) return Fail(error, "열화상 셔터 교정 명령이 실패했습니다");
    watch_ = {};
    watching_ = true;
    // 끝은 수신 스레드의 ReadFrame이 프레임 내용으로 판정합니다. 0.1초는 닫힘과 끊김을 확인하는 간격입니다.
    while (watching_ && device_ != nullptr) {
      watch_cv_.wait_for(lock, std::chrono::milliseconds(100));
      if (Connection() == ThermalConnection::kLost) break;
    }
    if (watching_ || device_ == nullptr) {
      watching_ = false;
      return Fail(error, "셔터 교정을 판정하기 전에 열화상 카메라가 닫히거나 끊겼습니다");
    }
    last_calibration_ = watch_;
    if (watch_.phase == pixel::CalibrationPhase::kNotSeen) {
      return Fail(error, "셔터 교정 명령 뒤 교정 구간(같은 프레임 반복)이 보이지 않았습니다");
    }
    if (watch_.phase == pixel::CalibrationPhase::kStuck) return Fail(error, "셔터 교정 구간이 끝나지 않았습니다");
    return true;
  }

  std::optional<pixel::CalibrationWatch> LastCalibration() const override {
    std::lock_guard<std::mutex> lock(device_mutex_);
    return last_calibration_;
  }

  int64_t CalibrationUs() const override { return kTeEv1CalibrationUs; }

  bool HoldAutoCalibration(bool hold, std::string* error) override {
    std::lock_guard<std::mutex> lock(device_mutex_);
    if (device_ == nullptr) return Fail(error, "열화상 카메라가 열려 있지 않습니다");
    if (!SettingKnown(setting_)) {
      return Fail(error, "장치의 원래 셔터 설정을 읽지 못해(읽지 못함 또는 모두 0) 되돌릴 수 없으므로 바꾸지 않습니다");
    }
    // SetShutterMode는 실행 중 값만 바꿉니다. 플래시에 쓰는 SaveSetting은 부르지 않습니다.
    if (!device_->SetShutterMode(hold ? kManualShutter : setting_.shutterMode)) {
      return Fail(error, "열화상 셔터 모드 명령이 실패했습니다");
    }
    held_ = hold;
    return true;
  }

  std::string AutoCalibrationSetting() const override {
    std::lock_guard<std::mutex> lock(device_mutex_);
    if (device_ == nullptr) return {};
    if (setting_.shutterMode == kUnreadSetting) return held_ ? "unread, held" : "unread";
    const char* mode = "other";
    if (setting_.shutterMode == I3_TEMP_SHUTTER) mode = "temperature";
    if (setting_.shutterMode == I3_TIME_SHUTTER) mode = "time";
    if (setting_.shutterMode == kManualShutter) mode = "manual";
    char text[96];
    std::snprintf(text, sizeof(text), "%s(%u), time=%u, temp=%u%s", mode, static_cast<unsigned>(setting_.shutterMode),
                  static_cast<unsigned>(setting_.shutterTime), static_cast<unsigned>(setting_.shutterTemp),
                  held_ ? ", held" : "");
    return text;
  }

  ThermalConnection Connection() const override {
    // 수신 스레드가 SDK 호출 중이어도 기다리지 않도록 잠금 없이 원자 값만 읽습니다.
    const int64_t last = last_frame_us_.load();
    return JudgeThermalConnection(open_.load(), Timestamp{opened_at_us_.load()},
                                  last == kNoFrame ? std::nullopt : std::optional<Timestamp>(Timestamp{last}),
                                  HostNow(), rule_);
  }

 private:
  static constexpr int64_t kNoFrame = std::numeric_limits<int64_t>::min();

  // pixels_를 가리키는 Y16 프레임입니다. device_mutex_를 잡고 부릅니다.
  FrameView PixelView(int32_t width, int32_t height) const {
    FrameView view;
    view.format = PixelFormat::kY16;
    view.width = width;
    view.height = height;
    view.plane[0] = reinterpret_cast<const uint8_t*>(pixels_.data());
    view.row_stride[0] = width * 2;
    return view;
  }

  static bool Fail(std::string* error, const std::string& text) {
    if (error != nullptr) *error = text;
    return false;
  }

  bool LoadModule(std::string* error) {
    if (module_ != nullptr) return true;
    // 이름만 주면 검색 순서를 OS가 정하므로 실행 파일 옆의 DLL을 경로로 지정합니다.
    const std::wstring path = ExecutableFolder() + L"i3system_TE_x64.dll";
    module_ = LoadLibraryW(path.c_str());
    if (module_ == nullptr) {
      return Fail(error, "열화상 SDK(i3system_TE_x64.dll)를 불러오지 못했습니다. 오류 " +
                             std::to_string(GetLastError()) + ". DLL 셋과 VC++ 2010 x64 런타임이 필요합니다");
    }
    scan_ = reinterpret_cast<ScanFn>(reinterpret_cast<void*>(GetProcAddress(module_, "ScanTE")));
    open_a_ = reinterpret_cast<OpenAFn>(reinterpret_cast<void*>(GetProcAddress(module_, "OpenTE_A")));
    if (scan_ == nullptr || open_a_ == nullptr) {
      FreeLibrary(module_);
      module_ = nullptr;
      return Fail(error, "열화상 SDK에서 필요한 함수를 찾지 못했습니다");
    }
    return true;
  }

  // 저장과 불러오기가 같은 경로를 쓰도록 한 곳에서 만듭니다.
  std::filesystem::path LensShadingPath(const std::string& name) const {
    return PathFromUtf8(lens_shading_folder_) / PathFromUtf8(LensShadingFileName(name));
  }

  // 이 카메라의 보정 파일이 있으면 불러옵니다. device_mutex_를 잡고 부릅니다.
  LensShadingStatus LoadLensShading(const std::string& name) {
    const std::filesystem::path path = LensShadingPath(name);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return {};
    const std::optional<std::string> sdk_path = SdkPath(path);
    if (!sdk_path) {
      return {{record::LensShadingSource::kFailed, std::nullopt},
              "보정 파일 경로에 SDK가 읽을 수 없는 글자가 있어 불러오지 못했습니다"};
    }
    if (!device_->LoadCalibration(sdk_path->c_str())) {
      return {{record::LensShadingSource::kFailed, std::nullopt}, "보정 파일을 불러오지 못했습니다"};
    }
    const std::optional<int64_t> saved_s = FileUnixSeconds(path);
    std::optional<timing::LensShadingConditions> made;
    if (saved_s) made = ReadLensShadingConditions(path, *saved_s);
    return {{record::LensShadingSource::kLoaded, saved_s}, std::string(), made};
  }

  void SetLensShading(const LensShadingStatus& status) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    lens_shading_ = status;
  }

  void CloseDevice() {
    if (device_ == nullptr) return;
    // 자동 교정을 멈춘 채로 닫으면 다음에 여는 프로그램도 교정 없이 받으므로 닫기 전에 되돌립니다.
    if (held_) device_->SetShutterMode(setting_.shutterMode);
    held_ = false;
    device_->CloseTE();
    watch_cv_.notify_all();
    device_ = nullptr;
  }

  HMODULE module_ = nullptr;
  ScanFn scan_ = nullptr;
  OpenAFn open_a_ = nullptr;

  mutable std::mutex device_mutex_;  // device_, pixels_, setting_, held_를 SDK 호출 하나씩만 쓰게 합니다
  i3::TE_A* device_ = nullptr;
  i3::TE_SETTING setting_{};  // 연 때 읽은 설정. 자동 교정을 되돌릴 값입니다
  bool held_ = false;         // 자동 교정을 멈춤
  std::string lens_shading_folder_;  // 렌즈 셰이딩 보정 파일 폴더(UTF-8). 비면 보정 기능이 없습니다
  const pixel::CalibrationRule calibration_rule_;
  bool watching_ = false;     // CalibrateNow가 판정을 기다림
  pixel::CalibrationWatch watch_;
  std::optional<pixel::CalibrationWatch> last_calibration_;
  std::condition_variable watch_cv_;
  std::vector<unsigned short> pixels_;
  std::optional<CameraTemperature> last_temperature_;  // 마지막으로 받은 프레임과 함께 읽은 값. 수신 스레드(Open, ReadFrame)만 씁니다
  int32_t device_width_ = 0;
  int32_t device_height_ = 0;
  int64_t next_index_ = 0;
  PollWait poll_wait_;

  mutable std::mutex state_mutex_;
  std::string name_;
  int32_t width_ = 0;
  int32_t height_ = 0;
  std::optional<LensShadingStatus> lens_shading_;  // 지금 연결의 렌즈 셰이딩 보정 상태

  std::atomic<bool> open_{false};
  std::atomic<int64_t> opened_at_us_{0};
  std::atomic<int64_t> last_frame_us_{kNoFrame};
  ThermalLossRule rule_;
};

}  // namespace

std::unique_ptr<ThermalCamera> MakeDeviceThermalCamera() {
  return std::make_unique<WindowsThermalCamera>(kTeEv1CalibrationRule);
}

std::unique_ptr<ThermalCamera> MakeDeviceThermalCamera(const pixel::CalibrationRule& rule) {
  return std::make_unique<WindowsThermalCamera>(rule);
}

}  // namespace hwlab::platform
