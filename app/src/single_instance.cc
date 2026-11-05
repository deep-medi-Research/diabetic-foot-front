#include "single_instance.h"

#ifdef _WIN32

#include <string>
#include <vector>

// windows.h는 매크로(ERROR, min, max 등)가 앞 헤더와 겹치지 않게 마지막에 넣습니다.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

namespace {

// 설치 프로그램이 쓰는 HWLabAppRunning(main.cc)과 따로 둡니다. 그쪽은 결과 창만 띄우는 실행도 만들기 때문입니다.
constexpr wchar_t kMutexName[] = L"HWLabAppDevices";
// 장치를 가진 실행의 PID를 담는 공유 메모리입니다. 결과 창만 띄운 실행(--result)도 이름이 같아 이름으로는 가리지 못합니다.
constexpr wchar_t kOwnerName[] = L"HWLabAppDevicesOwner";
constexpr DWORD kTakeOverWaitMs = 15000;
constexpr int kWindowWaitMs = 10000;

// 이 실행 말고 이름이 hwlab_app.exe인 프로세스들입니다.
std::vector<DWORD> OtherAppProcesses() {
  std::vector<DWORD> ids;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return ids;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  const DWORD self = GetCurrentProcessId();
  for (BOOL ok = Process32FirstW(snap, &entry); ok; ok = Process32NextW(snap, &entry)) {
    if (entry.th32ProcessID != self && _wcsicmp(entry.szExeFile, L"hwlab_app.exe") == 0) {
      ids.push_back(entry.th32ProcessID);
    }
  }
  CloseHandle(snap);
  return ids;
}

struct WindowSearch {
  std::vector<DWORD> ids;
  HWND found = nullptr;
};

// 다른 실행의 보이는 최상위 창을 찾습니다.
BOOL CALLBACK FindVisible(HWND hwnd, LPARAM param) {
  auto* search = reinterpret_cast<WindowSearch*>(param);
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
  for (DWORD id : search->ids) {
    if (id == pid) {
      search->found = hwnd;
      return FALSE;
    }
  }
  return TRUE;
}

// 장치를 얻은 실행이 자기 PID를 남깁니다. 핸들은 닫지 않아 프로세스가 끝날 때 Windows가 풉니다.
void PublishOwner() {
  HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(DWORD), kOwnerName);
  if (mapping == nullptr) return;
  auto* pid = static_cast<DWORD*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(DWORD)));
  if (pid != nullptr) *pid = GetCurrentProcessId();
}

// 장치를 가진 실행의 PID입니다. 남긴 값이 없으면(이 기능 전 판의 실행) 0입니다.
DWORD ReadOwner() {
  HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kOwnerName);
  if (mapping == nullptr) return 0;
  DWORD owner = 0;
  if (const auto* pid = static_cast<const DWORD*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(DWORD)))) {
    owner = *pid;
    UnmapViewOfFile(pid);
  }
  CloseHandle(mapping);
  return owner;
}

// 장치를 가진 실행이 알려져 있고 아직 살아 있으면 그것만, 아니면 이름이 같은 다른 실행 모두입니다.
std::vector<DWORD> HolderCandidates() {
  std::vector<DWORD> others = OtherAppProcesses();
  const DWORD owner = ReadOwner();
  for (DWORD id : others) {
    if (id == owner) return {owner};
  }
  return others;
}

InstanceClaim Claimed() {
  PublishOwner();
  return InstanceClaim::kAlone;
}

std::wstring IdList(const std::vector<DWORD>& ids) {
  std::wstring text;
  for (DWORD id : ids) {
    if (!text.empty()) text += L", ";
    text += std::to_wstring(id);
  }
  return text;
}

}  // namespace

InstanceClaim ClaimDevices() {
  // 처음 만든 실행이 소유합니다. 소유한 실행이 어떻게 끝나든(강제 종료 포함) Windows가 소유를 풀어 다음 실행이 넘겨받습니다.
  HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
  if (mutex == nullptr) return InstanceClaim::kAlone;
  if (GetLastError() != ERROR_ALREADY_EXISTS) return Claimed();
  // 앞 실행이 방금 끝나는 중일 수 있어 잠깐 기다려 봅니다.
  const DWORD quick = WaitForSingleObject(mutex, 1500);
  if (quick == WAIT_OBJECT_0 || quick == WAIT_ABANDONED) return Claimed();

  // 바로가기를 두 번 누른 경우처럼 앞 실행이 아직 창을 띄우기 전일 수 있어, 보이는 창을 잠시 더 찾습니다.
  // 이 시간 동안 창이 없어야 종료 중에 멈춘 실행으로 봅니다.
  std::vector<DWORD> others;
  WindowSearch search;
  for (int waited_ms = 0; waited_ms <= kWindowWaitMs; waited_ms += 500) {
    others = HolderCandidates();
    search = WindowSearch{others};
    EnumWindows(FindVisible, reinterpret_cast<LPARAM>(&search));
    if (search.found != nullptr || others.empty()) break;
    if (WaitForSingleObject(mutex, 500) != WAIT_TIMEOUT) return Claimed();
  }
  if (search.found != nullptr) {
    if (IsIconic(search.found)) ShowWindow(search.found, SW_RESTORE);
    SetForegroundWindow(search.found);
    return InstanceClaim::kShownOther;
  }

  const std::wstring ask =
      L"창을 닫았지만 종료 중에 멈춘 촬영 앱이 남아 있습니다(hwlab_app.exe, PID " + IdList(others) +
      L").\n이 앱이 카메라를 잡고 있어 새로 시작할 수 없습니다.\n\n"
      L"[예]를 누르면 남은 앱을 끝내고 새로 시작합니다.";
  if (MessageBoxW(nullptr, ask.c_str(), L"당뇨발 촬영", MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND) != IDYES) {
    return InstanceClaim::kStopped;
  }
  for (DWORD id : others) {
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, id);
    if (process == nullptr) continue;
    TerminateProcess(process, 4);
    CloseHandle(process);
  }
  // 드라이버 호출에 걸린 프로세스는 끝내도 1~2분 남은 적이 있어(2026-10-23 노트북) 정한 시간만 기다립니다.
  const DWORD taken = WaitForSingleObject(mutex, kTakeOverWaitMs);
  if (taken == WAIT_OBJECT_0 || taken == WAIT_ABANDONED) return Claimed();
  MessageBoxW(nullptr,
              L"남은 앱이 아직 끝나지 않았습니다. 장치 드라이버가 정리하는 데 1~2분 걸릴 수 있습니다.\n잠시 뒤 다시 켜 주십시오.",
              L"당뇨발 촬영", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
  return InstanceClaim::kStopped;
}

#else

InstanceClaim ClaimDevices() { return InstanceClaim::kAlone; }

#endif
