// Media Foundation으로 프레임을 받고, DirectShow 컨트롤로 노출, 게인, 색온도를 바꾸고,
// See3CAM 제조사 SDK(eCAMFwSw.dll)로 잠금과 fps를 바꿉니다. 벤더 이름과 타입은 이 파일 밖으로 나가지 않습니다.
#include <windows.h>
#include <dshow.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "hwlab/platform/rgb_camera.h"

namespace hwlab::platform {

namespace {

const DWORD kVideo = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

template <typename T>
void SafeRelease(T*& p) {
  if (p != nullptr) {
    p->Release();
    p = nullptr;
  }
}

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

// Media Foundation과 DirectShow 객체를 부르는 스레드는 각자 COM을 초기화해야 합니다.
void EnsureComOnThisThread() {
  static thread_local const bool initialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
  (void)initialized;
}

std::string Narrow(const wchar_t* text) {
  if (text == nullptr) {
    return {};
  }
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(bytes > 0 ? bytes - 1 : 0), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
  return out;
}

// See3CAM_24CUG 확장 유닛입니다. 함수 이름은 벤더 헤더 eCAMFwSw.h와 DLL export 목록으로 확인했습니다.
class Extension {
 public:
  // 실험(UVC 단독): HWLAB_NO_VENDOR_SDK가 있으면 eCAMFwSw.dll을 불러오지 않아 카메라는 연 UVC 형식의 fps로 돕니다.
  // fps와 잠금 명령은 값만 기억해 다시 읽기에 돌려주고, 실제 fps는 녹화 전 간격 검사가 확인합니다.
  Extension() : bypass_(GetEnvironmentVariableW(L"HWLAB_NO_VENDOR_SDK", nullptr, 0) > 0) {}

  ~Extension() {
    if (ready_ && deinit_ != nullptr) {
      deinit_();
    }
    if (dll_ != nullptr) {
      FreeLibrary(dll_);
    }
  }

  bool Open() {
    if (ready_) {
      return true;
    }
    dll_ = LoadLibraryW(L"eCAMFwSw.dll");
    if (dll_ == nullptr) {
      return false;
    }
    using CountFn = BOOL (*)(UINT32*);
    using PathsFn = BOOL (*)(wchar_t**);
    using InitFn = BOOL (*)(wchar_t*);
    const auto count = reinterpret_cast<CountFn>(GetProcAddress(dll_, "GetDevicesCount"));
    const auto paths = reinterpret_cast<PathsFn>(GetProcAddress(dll_, "GetDevicePaths"));
    const auto init = reinterpret_cast<InitFn>(GetProcAddress(dll_, "InitExtensionUnit"));
    deinit_ = reinterpret_cast<DeinitFn>(GetProcAddress(dll_, "DeinitExtensionUnit"));
    get_mode_ = reinterpret_cast<GetModeFn>(GetProcAddress(dll_, "GetStreamMode24CUG"));
    set_mode_ = reinterpret_cast<SetModeFn>(GetProcAddress(dll_, "SetStreamMode24CUG"));
    get_fps_ = reinterpret_cast<GetByteFn>(GetProcAddress(dll_, "GetFrameRateValue24CUG"));
    set_fps_ = reinterpret_cast<SetByteFn>(GetProcAddress(dll_, "SetFrameRateValue24CUG"));
    if (count == nullptr || paths == nullptr || init == nullptr || deinit_ == nullptr ||
        get_mode_ == nullptr || set_mode_ == nullptr || get_fps_ == nullptr || set_fps_ == nullptr) {
      return false;
    }
    UINT32 n = 0;
    if (!count(&n) || n == 0) {
      return false;
    }
    std::vector<std::vector<wchar_t>> storage(n, std::vector<wchar_t>(MAX_PATH));
    std::vector<wchar_t*> list(n);
    for (UINT32 i = 0; i < n; ++i) {
      list[i] = storage[i].data();
    }
    // 실험 노트북에는 See3CAM이 한 대뿐이라 첫 장치를 씁니다.
    ready_ = paths(list.data()) && init(list[0]);
    return ready_;
  }

  std::optional<bool> Locked() {
    if (bypass_) return bypass_locked_;
    UINT8 mode = 0, current = 0;
    if (!Open() || !get_mode_(&mode, &current)) {
      return std::nullopt;
    }
    return current != 0;
  }

  bool SetLocked(bool locked) {
    if (bypass_) {
      bypass_locked_ = locked;
      return true;
    }
    UINT8 mode = 0, current = 0;
    // 스트림 모드는 읽은 값을 그대로 두고 잠금만 바꿉니다.
    return Open() && get_mode_(&mode, &current) && set_mode_(mode, locked ? 1 : 0);
  }

  std::optional<int32_t> FrameRate() {
    if (bypass_) return bypass_fps_;
    UINT8 fps = 0;
    if (!Open() || !get_fps_(&fps)) {
      return std::nullopt;
    }
    return fps;
  }

  bool SetFrameRate(int32_t fps) {
    if (bypass_) {
      bypass_fps_ = fps;
      return true;
    }
    return Open() && fps > 0 && fps < 256 && set_fps_(static_cast<UINT8>(fps));
  }

 private:
  using DeinitFn = BOOL (*)();
  using GetModeFn = BOOL (*)(UINT8*, UINT8*);
  using SetModeFn = BOOL (*)(UINT8, UINT8);
  using GetByteFn = BOOL (*)(UINT8*);
  using SetByteFn = BOOL (*)(UINT8);

  const bool bypass_;
  std::optional<bool> bypass_locked_;
  std::optional<int32_t> bypass_fps_;
  HMODULE dll_ = nullptr;
  DeinitFn deinit_ = nullptr;
  GetModeFn get_mode_ = nullptr;
  SetModeFn set_mode_ = nullptr;
  GetByteFn get_fps_ = nullptr;
  SetByteFn set_fps_ = nullptr;
  bool ready_ = false;
};

class WindowsRgbCamera final : public RgbCamera {
 public:
  ~WindowsRgbCamera() override {
    CloseDevice();
    MFShutdown();
    if (com_initialized_) CoUninitialize();
  }

  bool Init(const std::string& name_contains, int32_t width, int32_t height, int32_t frame_rate,
            std::string* error) {
    // Qt 화면 스레드는 이미 STA로 초기화되어 RPC_E_CHANGED_MODE를 받습니다. 초기화된 상태라 그대로 씁니다.
    // 이것을 실패로 보면 화면 세션에서만 카메라가 열리지 않습니다(SSH 세션의 offscreen 실행에서는 드러나지 않음).
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
      *error = "COM 초기화 실패";
      return false;
    }
    com_initialized_ = SUCCEEDED(com);
    if (FAILED(MFStartup(MF_VERSION))) {
      *error = "Media Foundation 초기화 실패";
      return false;
    }
    name_contains_ = name_contains;
    width_ = width;
    height_ = height;
    frame_rate_ = frame_rate;
    return OpenDevice(error);
  }

  bool Reopen() override {
    if (interrupted_) return false;
    EnsureComOnThisThread();
    // 설정 스레드가 컨트롤 인터페이스를 쓰는 중에 해제하면 접근 위반이 납니다(2026-10-19 실기, 다른 앱이 카메라를 쓰는 중).
    std::lock_guard<std::mutex> lock(control_mutex_);
    CloseDevice();
    std::string error;
    const bool opened = OpenDevice(&error);
    std::lock_guard<std::mutex> problem_lock(problem_mutex_);
    problem_ = error;
    return opened;
  }

  std::string OpenProblem() const override {
    std::lock_guard<std::mutex> lock(problem_mutex_);
    return problem_;
  }

 private:
  bool OpenDevice(std::string* error) {
    const std::string& name_contains = name_contains_;
    const int32_t width = width_;
    const int32_t height = height_;
    extension_ = std::make_unique<Extension>();
    IMFAttributes* query = nullptr;
    MFCreateAttributes(&query, 1);
    query->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    MFEnumDeviceSources(query, &devices, &count);
    SafeRelease(query);
    for (UINT32 i = 0; i < count; ++i) {
      wchar_t* name = nullptr;
      UINT32 length = 0;
      if (source_ == nullptr &&
          SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length))) {
        const std::string text = Narrow(name);
        if (text.find(name_contains) != std::string::npos &&
            SUCCEEDED(devices[i]->ActivateObject(IID_PPV_ARGS(&source_)))) {
          name_ = text;
        }
        CoTaskMemFree(name);
      }
      devices[i]->Release();
    }
    CoTaskMemFree(devices);
    // 장치가 없으면 아직 연결 전일 수 있으므로 이유를 비워 둡니다. 화면은 "찾는 중"을 보입니다.
    if (source_ == nullptr) {
      error->clear();
      return false;
    }
    if (FAILED(MFCreateSourceReaderFromMediaSource(source_, nullptr, &reader_))) {
      *error = "카메라 영상 읽기를 시작하지 못함";
      return false;
    }
    if (!SelectUyvy(width, height, frame_rate_, error)) {
      return false;
    }
    source_->QueryInterface(IID_PPV_ARGS(&camera_control_));
    source_->QueryInterface(IID_PPV_ARGS(&proc_amp_));
    if (camera_control_ == nullptr || proc_amp_ == nullptr) {
      *error = "DirectShow 컨트롤 인터페이스 없음";
      return false;
    }
    std::lock_guard<std::mutex> lock(interrupt_mutex_);
    // 다시 여는 중에 Interrupt가 불렸으면 연 스트림도 바로 닫아 ReadFrame이 기다리지 않게 합니다.
    if (interrupted_) source_->Shutdown();
    interrupt_source_ = source_;
    interrupt_source_->AddRef();
    return true;
  }

  void CloseDevice() {
    {
      std::lock_guard<std::mutex> lock(interrupt_mutex_);
      SafeRelease(interrupt_source_);
    }
    UnlockCurrent();
    SafeRelease(camera_control_);
    SafeRelease(proc_amp_);
    SafeRelease(reader_);
    if (source_ != nullptr) source_->Shutdown();
    SafeRelease(source_);
    extension_.reset();
  }

 public:

  std::string DeviceName() const override {
    std::lock_guard<std::mutex> lock(control_mutex_);
    return name_;
  }

  std::optional<ReceivedFrame> ReadFrame() override {
    EnsureComOnThisThread();
    UnlockCurrent();
    if (reader_ == nullptr || interrupted_) return std::nullopt;
    for (;;) {
      DWORD stream = 0, flags = 0;
      LONGLONG timestamp = 0;
      IMFSample* sample = nullptr;
      const HRESULT hr = reader_->ReadSample(kVideo, 0, &stream, &flags, &timestamp, &sample);
      const int64_t host = HostMicros();
      if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM) || (flags & MF_SOURCE_READERF_ERROR)) {
        SafeRelease(sample);
        return std::nullopt;
      }
      const bool gap = (flags & MF_SOURCE_READERF_STREAMTICK) != 0;
      if (sample == nullptr) {
        pending_gap_ = pending_gap_ || gap;
        continue;
      }
      if (FAILED(sample->ConvertToContiguousBuffer(&buffer_))) {
        SafeRelease(sample);
        return std::nullopt;
      }
      SafeRelease(sample);
      BYTE* data = nullptr;
      LONG stride = 0;
      if (SUCCEEDED(buffer_->QueryInterface(IID_PPV_ARGS(&buffer2d_))) &&
          SUCCEEDED(buffer2d_->Lock2D(&data, &stride))) {
        locked2d_ = true;
      } else {
        SafeRelease(buffer2d_);
        DWORD length = 0;
        if (FAILED(buffer_->Lock(&data, nullptr, &length))) {
          SafeRelease(buffer_);
          return std::nullopt;
        }
        stride = width_ * 2;
      }
      ReceivedFrame frame;
      frame.view.format = PixelFormat::kUyvy;
      frame.view.width = width_;
      frame.view.height = height_;
      frame.view.plane[0] = data;
      frame.view.row_stride[0] = stride;
      frame.view.stream = StreamKind::kRgb;
      frame.view.index = next_index_++;
      // Media Foundation 샘플 시각은 100ns 단위입니다.
      frame.view.time = Timestamp{timestamp / 10};
      frame.host_time = Timestamp{host};
      frame.stream_gap = pending_gap_ || gap;
      pending_gap_ = false;
      return frame;
    }
  }

  void Interrupt() override {
    std::lock_guard<std::mutex> lock(interrupt_mutex_);
    interrupted_ = true;
    // 동기 ReadSample에는 시간 제한이 없습니다. 미디어 소스를 닫으면 기다리던 ReadSample이 실패로 돌아옵니다.
    if (interrupt_source_ != nullptr) interrupt_source_->Shutdown();
  }

  CameraState ReadState() override { return Read(false); }

  CameraState ReadStoredState() override { return Read(true); }

  bool Execute(const flow::SettingCommand& command) override {
    EnsureComOnThisThread();
    std::lock_guard<std::mutex> lock(control_mutex_);
    if (camera_control_ == nullptr || proc_amp_ == nullptr) return false;
    long value = 0, flags = 0;
    switch (command.kind) {
      case flow::CommandKind::kSdkFrameRate:
        return extension_->SetFrameRate(command.value);
      case flow::CommandKind::kUnlock:
        return extension_->SetLocked(false);
      case flow::CommandKind::kLock:
        return extension_->SetLocked(true);
      case flow::CommandKind::kExposureAuto:
        camera_control_->Get(CameraControl_Exposure, &value, &flags);
        return SUCCEEDED(camera_control_->Set(CameraControl_Exposure, value, CameraControl_Flags_Auto));
      case flow::CommandKind::kExposureManual:
        return SUCCEEDED(
            camera_control_->Set(CameraControl_Exposure, command.value, CameraControl_Flags_Manual));
      case flow::CommandKind::kGain:
        return SUCCEEDED(proc_amp_->Set(VideoProcAmp_Gain, command.value, VideoProcAmp_Flags_Manual));
      case flow::CommandKind::kWhiteBalanceAuto:
        proc_amp_->Get(VideoProcAmp_WhiteBalance, &value, &flags);
        return SUCCEEDED(proc_amp_->Set(VideoProcAmp_WhiteBalance, value, VideoProcAmp_Flags_Auto));
      case flow::CommandKind::kWhiteBalanceManual:
        return SUCCEEDED(
            proc_amp_->Set(VideoProcAmp_WhiteBalance, command.value, VideoProcAmp_Flags_Manual));
    }
    return false;
  }

 private:
  CameraState Read(bool stored_exposure) {
    EnsureComOnThisThread();
    std::lock_guard<std::mutex> lock(control_mutex_);
    CameraState state;
    if (camera_control_ == nullptr || proc_amp_ == nullptr) return state;
    long value = 0, flags = 0;
    if (SUCCEEDED(camera_control_->Get(CameraControl_Exposure, &value, &flags))) {
      state.exposure_auto = (flags & CameraControl_Flags_Auto) != 0;
      // 펌웨어 자동 노출 중에는 마지막 수동 값만 읽혀(2026-10-16 실측) 실제 값으로 남기지 않습니다.
      // 복원은 이 저장값을 되돌려야 하므로 stored_exposure일 때만 채웁니다.
      if (!*state.exposure_auto || stored_exposure) {
        state.exposure_step = static_cast<int32_t>(value);
      }
    }
    if (SUCCEEDED(proc_amp_->Get(VideoProcAmp_Gain, &value, &flags))) {
      state.gain = static_cast<int32_t>(value);
    }
    if (SUCCEEDED(proc_amp_->Get(VideoProcAmp_WhiteBalance, &value, &flags))) {
      state.white_balance_auto = (flags & VideoProcAmp_Flags_Auto) != 0;
      state.white_balance_k = static_cast<int32_t>(value);
    }
    state.sdk_frame_rate = extension_->FrameRate();
    state.locked = extension_->Locked();
    return state;
  }

  // 크기가 맞는 UYVY 형식 중 frame_rate를 낼 수 있고 최대 fps가 가장 낮은 형식을 엽니다. MJPEG나 다른 크기로 대신 열지 않습니다.
  // 첫 형식을 열면 1280x720은 60~120fps 형식이 120fps로 돌았습니다(2026-11-03 실측 119.2fps, SDK fps 60으로도 그대로).
  // fps 범위를 알려 주는 형식은 frame_rate로 맞춰 엽니다.
  // 같은 카메라도 USB 2 속도로 연결되면 형식마다 최대 fps가 낮게 나옵니다(데이터시트 UYVY 1920x1200: USB 3 55fps, USB 2 6fps).
  // USB 3 포트에 꽂아도 케이블, 허브, Type-C 방향 때문에 USB 2 속도가 될 수 있습니다(2026-11-04 현장 보고, UYVY 1280x720 최대 15fps).
  bool SelectUyvy(int32_t width, int32_t height, int32_t frame_rate, std::string* error) {
    const std::string size = "UYVY " + std::to_string(width) + "x" + std::to_string(height);
    bool sized = false;     // 크기가 맞는 UYVY 형식이 있음
    double max_fps = 0.0;   // 크기가 맞는 UYVY 형식들의 최대 fps 중 가장 큰 값
    std::optional<DWORD> best;
    double best_fps = 0.0;  // best의 최대 fps. 0이면 알려 주지 않는 형식
    for (DWORD i = 0;; ++i) {
      IMFMediaType* type = nullptr;
      if (FAILED(reader_->GetNativeMediaType(kVideo, i, &type))) {
        break;
      }
      GUID subtype{};
      UINT32 w = 0, h = 0;
      type->GetGUID(MF_MT_SUBTYPE, &subtype);
      MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
      const double fps = MaxFrameRate(type);
      SafeRelease(type);
      if (subtype != MFVideoFormat_UYVY || static_cast<int32_t>(w) != width || static_cast<int32_t>(h) != height) {
        continue;
      }
      sized = true;
      if (fps > max_fps) max_fps = fps;
      // 최대 fps를 알려 주지 않는 형식은 막지 않지만 알려 주는 형식보다 뒤로 둡니다. 녹화 직전 실제 프레임 간격 검사가 남아 있습니다.
      if (fps > 0.0 && fps + 0.5 < frame_rate) continue;
      const bool better = !best || (best_fps <= 0.0 && fps > 0.0) || (fps > 0.0 && fps < best_fps);
      if (better) {
        best = i;
        best_fps = fps;
      }
    }
    if (best) {
      IMFMediaType* type = nullptr;
      bool ok = false;
      if (SUCCEEDED(reader_->GetNativeMediaType(kVideo, *best, &type))) {
        UINT32 min_n = 0, min_d = 0;
        if (SUCCEEDED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE_RANGE_MIN, &min_n, &min_d)) && min_d != 0 &&
            static_cast<double>(min_n) / min_d <= frame_rate + 0.5) {
          MFSetAttributeRatio(type, MF_MT_FRAME_RATE, static_cast<UINT32>(frame_rate), 1);
        }
        ok = SUCCEEDED(reader_->SetCurrentMediaType(kVideo, nullptr, type));
      }
      SafeRelease(type);
      if (ok) {
        return true;
      }
    }
    if (!sized) {
      *error = "카메라에 " + size + " 형식이 없습니다";
    } else if (max_fps > 0.0 && max_fps + 0.5 < frame_rate) {
      *error = LowFrameRateMessage(size, max_fps, frame_rate);
    } else {
      *error = size + " 형식을 열지 못했습니다";
    }
    return false;
  }

  // 형식이 낼 수 있는 최대 fps입니다. 알려 주지 않으면 0입니다.
  static double MaxFrameRate(IMFMediaType* type) {
    UINT32 numerator = 0, denominator = 0;
    if (SUCCEEDED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE_RANGE_MAX, &numerator, &denominator)) &&
        denominator != 0) {
      return static_cast<double>(numerator) / denominator;
    }
    if (SUCCEEDED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &numerator, &denominator)) && denominator != 0) {
      return static_cast<double>(numerator) / denominator;
    }
    return 0.0;
  }

  void UnlockCurrent() {
    if (buffer2d_ != nullptr && locked2d_) {
      buffer2d_->Unlock2D();
    } else if (buffer_ != nullptr) {
      buffer_->Unlock();
    }
    locked2d_ = false;
    SafeRelease(buffer2d_);
    SafeRelease(buffer_);
  }

  std::string name_;
  std::string name_contains_;
  int32_t width_ = 0;
  int32_t height_ = 0;
  int32_t frame_rate_ = 0;
  IMFMediaSource* source_ = nullptr;
  IMFSourceReader* reader_ = nullptr;
  IAMCameraControl* camera_control_ = nullptr;
  IAMVideoProcAmp* proc_amp_ = nullptr;
  IMFMediaBuffer* buffer_ = nullptr;
  IMF2DBuffer* buffer2d_ = nullptr;
  bool locked2d_ = false;
  bool pending_gap_ = false;
  FrameIndex next_index_ = 0;
  bool com_initialized_ = false;
  std::unique_ptr<Extension> extension_;
  // Reopen(수신 스레드)과 설정 읽기, 쓰기(설정 스레드, 화면 스레드)가 장치 인터페이스를 함께 쓰지 않게 합니다.
  mutable std::mutex control_mutex_;
  // Interrupt가 다른 스레드에서 닫을 미디어 소스입니다. control_mutex_와 따로 두어 설정 명령이 멈춰 있어도 닫을 수 있습니다.
  std::mutex interrupt_mutex_;
  IMFMediaSource* interrupt_source_ = nullptr;
  std::atomic<bool> interrupted_{false};
  // Reopen(수신 스레드)이 쓰고 화면 스레드가 읽습니다. 다시 여는 동안 기다리지 않도록 control_mutex_와 따로 둡니다.
  mutable std::mutex problem_mutex_;
  std::string problem_;
};

}  // namespace

std::unique_ptr<RgbCamera> RgbCamera::Open(const std::string& name_contains, int32_t width,
                                           int32_t height, int32_t frame_rate, std::string* error) {
  auto camera = std::make_unique<WindowsRgbCamera>();
  if (!camera->Init(name_contains, width, height, frame_rate, error)) {
    return nullptr;
  }
  return camera;
}

}  // namespace hwlab::platform
