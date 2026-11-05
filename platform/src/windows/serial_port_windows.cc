// Win32 직렬 포트입니다. 포트 목록은 SetupAPI의 Ports 장치 클래스에서, 읽기와 쓰기는 overlapped 입출력으로 합니다.
// overlapped로 열어 한 스레드가 Read로 기다리는 동안 다른 스레드가 Write를 할 수 있게 합니다.
#include <windows.h>
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/platform/serial_port.h"

namespace hwlab::platform {

namespace {

std::string ToUtf8(const wchar_t* text) {
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (bytes <= 1) return {};
  std::string out(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
  out.resize(static_cast<size_t>(bytes - 1));
  return out;
}

std::wstring ToWide(const std::string& text) {
  const int chars = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (chars <= 1) return {};
  std::wstring out(static_cast<size_t>(chars), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), chars);
  out.resize(static_cast<size_t>(chars - 1));
  return out;
}

std::string LastErrorText(DWORD code) {
  wchar_t* buffer = nullptr;
  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                 code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
  std::string text = buffer != nullptr ? ToUtf8(buffer) : std::string();
  if (buffer != nullptr) LocalFree(buffer);
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
  return text + " (" + std::to_string(code) + ")";
}

std::wstring RegistryText(HDEVINFO devices, SP_DEVINFO_DATA* device, DWORD property) {
  wchar_t buffer[512] = {};
  if (!SetupDiGetDeviceRegistryPropertyW(devices, device, property, nullptr, reinterpret_cast<BYTE*>(buffer),
                                         sizeof(buffer) - sizeof(wchar_t), nullptr)) {
    return {};
  }
  return buffer;
}

// 하드웨어 ID(예: USB\VID_10C4&PID_EA60&REV_0100)에서 key 뒤 16진수 4자리를 읽습니다.
std::optional<uint16_t> HexAfter(const std::wstring& id, const wchar_t* key) {
  const size_t at = id.find(key);
  if (at == std::wstring::npos || at + 8 > id.size()) return std::nullopt;
  const std::wstring digits = id.substr(at + 4, 4);
  wchar_t* end = nullptr;
  const unsigned long value = std::wcstoul(digits.c_str(), &end, 16);
  if (end != digits.c_str() + 4) return std::nullopt;
  return static_cast<uint16_t>(value);
}

std::string InstanceIdOf(DEVINST instance) {
  wchar_t id[MAX_DEVICE_ID_LEN + 1] = {};
  if (CM_Get_Device_IDW(instance, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) return {};
  return ToUtf8(id);
}

// 포트 장치의 인스턴스 ID에 일련번호가 없으면 부모(USB 장치)에서 찾습니다. 복합 장치의 인터페이스(&MI_xx)는 부모에 일련번호가 있습니다.
std::string SerialNumberOf(const SP_DEVINFO_DATA& device) {
  std::string serial = SerialNumberFromInstanceId(InstanceIdOf(device.DevInst));
  DEVINST parent = 0;
  if (serial.empty() && CM_Get_Parent(&parent, device.DevInst, 0) == CR_SUCCESS) {
    serial = SerialNumberFromInstanceId(InstanceIdOf(parent));
  }
  return serial;
}

int PortNumber(const std::string& name) { return name.size() > 3 ? std::atoi(name.c_str() + 3) : 0; }

class WindowsSerialPort final : public SerialPort {
 public:
  WindowsSerialPort(std::string name, HANDLE handle)
      : name_(std::move(name)),
        handle_(handle),
        read_event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
        write_event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
        wake_event_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}

  ~WindowsSerialPort() override {
    Close();
    CloseHandle(read_event_);
    CloseHandle(write_event_);
    CloseHandle(wake_event_);
  }

  std::string Name() const override { return name_; }

  bool IsOpen() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return open_;
  }

  bool Write(std::span<const uint8_t> bytes, int32_t timeout_ms) override {
    if (!Begin()) return false;
    OVERLAPPED overlapped{};
    overlapped.hEvent = write_event_;
    ResetEvent(write_event_);
    DWORD written = 0;
    bool ok = false;
    bool failed = false;
    if (WriteFile(handle_, bytes.data(), static_cast<DWORD>(bytes.size()), &written, &overlapped)) {
      ok = true;
    } else if (GetLastError() != ERROR_IO_PENDING) {
      failed = true;
    } else {
      ok = Finish(&overlapped, timeout_ms, &written, &failed);
    }
    End(failed);
    return ok && !failed && written == bytes.size();
  }

  std::optional<size_t> Read(std::span<uint8_t> buffer, int32_t timeout_ms) override {
    if (!Begin()) return std::nullopt;
    OVERLAPPED overlapped{};
    overlapped.hEvent = read_event_;
    ResetEvent(read_event_);
    DWORD received = 0;
    bool failed = false;
    if (!ReadFile(handle_, buffer.data(), static_cast<DWORD>(buffer.size()), &received, &overlapped)) {
      if (GetLastError() != ERROR_IO_PENDING) {
        failed = true;
      } else {
        Finish(&overlapped, timeout_ms, &received, &failed, wake_event_);
      }
    }
    End(failed);
    if (failed) return std::nullopt;
    return static_cast<size_t>(received);
  }

  void WakeRead() override { SetEvent(wake_event_); }

  void Close() override {
    std::unique_lock<std::mutex> lock(mutex_);
    if (handle_ == INVALID_HANDLE_VALUE) return;
    open_ = false;
    // 다른 스레드가 기다리는 입출력을 깨운 뒤, 그 스레드가 핸들을 다 쓸 때까지 기다렸다가 닫습니다.
    CancelIoEx(handle_, nullptr);
    idle_.wait(lock, [this] { return in_flight_ == 0; });
    CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
  }

 private:
  bool Begin() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) return false;
    ++in_flight_;
    return true;
  }

  void End(bool failed) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (failed) open_ = false;
      --in_flight_;
    }
    idle_.notify_all();
  }

  // 시간이 지나거나 WakeRead로 깨우면 입출력을 취소합니다. 취소 직전에 끝난 바이트도 GetOverlappedResult로 받습니다.
  // OVERLAPPED와 버퍼가 부른 쪽 스택에 있어 입출력이 끝나기 전에 돌아가지 않도록 GetOverlappedResult는 끝까지 기다립니다.
  bool Finish(OVERLAPPED* overlapped, int32_t timeout_ms, DWORD* transferred, bool* failed, HANDLE wake = nullptr) {
    const HANDLE events[2] = {overlapped->hEvent, wake};
    const DWORD wait = WaitForMultipleObjects(wake != nullptr ? 2 : 1, events, FALSE,
                                              static_cast<DWORD>(std::max(timeout_ms, 0)));
    if (wait != WAIT_OBJECT_0) CancelIoEx(handle_, overlapped);
    if (GetOverlappedResult(handle_, overlapped, transferred, TRUE)) return true;
    const DWORD error = GetLastError();
    // 취소로 끝난 것은 시간 초과이고, 그 밖의 오류는 장치가 빠진 것으로 봅니다.
    if (error != ERROR_OPERATION_ABORTED) *failed = true;
    return false;
  }

  const std::string name_;
  HANDLE handle_;
  HANDLE read_event_;
  HANDLE write_event_;
  HANDLE wake_event_;  // 자동 재설정. WakeRead가 켜고 기다리던 Read 하나가 끔
  mutable std::mutex mutex_;
  std::condition_variable idle_;
  int32_t in_flight_ = 0;
  bool open_ = true;
};

}  // namespace

std::vector<SerialPortInfo> ListSerialPorts() {
  std::vector<SerialPortInfo> ports;
  HDEVINFO devices = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
  if (devices == INVALID_HANDLE_VALUE) return ports;
  SP_DEVINFO_DATA device{};
  device.cbSize = sizeof(device);
  for (DWORD i = 0; SetupDiEnumDeviceInfo(devices, i, &device); ++i) {
    HKEY key = SetupDiOpenDevRegKey(devices, &device, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE) continue;
    wchar_t name[64] = {};
    DWORD size = sizeof(name) - sizeof(wchar_t);
    const LSTATUS status = RegQueryValueExW(key, L"PortName", nullptr, nullptr, reinterpret_cast<BYTE*>(name), &size);
    RegCloseKey(key);
    // Ports 클래스에는 프린터 포트(LPT)도 있어 COM만 남깁니다.
    if (status != ERROR_SUCCESS || std::wstring(name).rfind(L"COM", 0) != 0) continue;
    SerialPortInfo info;
    info.name = ToUtf8(name);
    std::wstring description = RegistryText(devices, &device, SPDRP_FRIENDLYNAME);
    if (description.empty()) description = RegistryText(devices, &device, SPDRP_DEVICEDESC);
    info.description = ToUtf8(description.c_str());
    const std::wstring hardware_id = RegistryText(devices, &device, SPDRP_HARDWAREID);
    info.usb_vendor_id = HexAfter(hardware_id, L"VID_");
    info.usb_product_id = HexAfter(hardware_id, L"PID_");
    info.serial_number = SerialNumberOf(device);
    ports.push_back(std::move(info));
  }
  SetupDiDestroyDeviceInfoList(devices);
  std::sort(ports.begin(), ports.end(),
            [](const SerialPortInfo& a, const SerialPortInfo& b) { return PortNumber(a.name) < PortNumber(b.name); });
  return ports;
}

std::unique_ptr<SerialPort> OpenSerialPort(const std::string& name, const SerialConfig& config, std::string* error) {
  auto fail = [&](const std::string& what) -> std::unique_ptr<SerialPort> {
    if (error != nullptr) *error = name + " " + what;
    return nullptr;
  };
  // COM10 이상은 \\.\ 접두어 없이 열리지 않습니다.
  const std::wstring path = L"\\\\.\\" + ToWide(name);
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return fail("포트를 열지 못했습니다: " + LastErrorText(GetLastError()));

  DCB dcb{};
  dcb.DCBlength = sizeof(dcb);
  if (!GetCommState(handle, &dcb)) {
    const DWORD code = GetLastError();
    CloseHandle(handle);
    return fail("포트 상태를 읽지 못했습니다: " + LastErrorText(code));
  }
  dcb.BaudRate = static_cast<DWORD>(config.baud_rate);
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fBinary = TRUE;
  dcb.fParity = FALSE;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDsrSensitivity = FALSE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;
  dcb.fNull = FALSE;
  dcb.fErrorChar = FALSE;
  dcb.fAbortOnError = FALSE;
  // 흐름 제어가 없는 장치라 DTR과 RTS의 핸드셰이크만 끄고 나머지는 드라이버 기본값을 둡니다.
  if (dcb.fDtrControl == DTR_CONTROL_HANDSHAKE) dcb.fDtrControl = DTR_CONTROL_DISABLE;
  if (dcb.fRtsControl == RTS_CONTROL_HANDSHAKE || dcb.fRtsControl == RTS_CONTROL_TOGGLE) {
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
  }
  if (!SetCommState(handle, &dcb)) {
    const DWORD code = GetLastError();
    CloseHandle(handle);
    return fail("포트 속도를 설정하지 못했습니다: " + LastErrorText(code));
  }
  // MAXDWORD, MAXDWORD, 상수 조합은 받은 바이트가 있으면 바로, 없으면 첫 바이트가 오는 즉시 돌아옵니다.
  // 호출마다의 시간 제한은 WaitForSingleObject와 취소로 지키므로 상수는 가장 크게 둡니다.
  COMMTIMEOUTS timeouts{};
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
  timeouts.ReadTotalTimeoutConstant = MAXDWORD - 1;
  timeouts.WriteTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 0;
  if (!SetCommTimeouts(handle, &timeouts)) {
    const DWORD code = GetLastError();
    CloseHandle(handle);
    return fail("시간 제한을 설정하지 못했습니다: " + LastErrorText(code));
  }
  // 열기 전에 장치가 보낸 바이트가 남아 있으면 첫 패킷이 어긋나므로 버립니다.
  PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
  return std::make_unique<WindowsSerialPort>(name, handle);
}

}  // namespace hwlab::platform
