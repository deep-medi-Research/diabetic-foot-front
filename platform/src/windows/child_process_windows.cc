#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "../output_lines.h"
#include "hwlab/platform/child_process.h"

namespace hwlab::platform {

namespace {

void SetError(std::string* error, const std::string& text) {
  if (error != nullptr) *error = text;
}

void CloseHandleOnce(HANDLE* handle) {
  if (*handle != nullptr && *handle != INVALID_HANDLE_VALUE) CloseHandle(*handle);
  *handle = nullptr;
}

std::wstring Widen(const std::string& text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
  return wide;
}

// CommandLineToArgvW와 MSVC 런타임이 같은 인자로 되읽도록 따옴표와 역슬래시를 붙입니다.
void AppendQuoted(std::wstring& command, const std::wstring& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    command += arg;
    return;
  }
  command += L'"';
  for (auto it = arg.begin();; ++it) {
    size_t backslashes = 0;
    while (it != arg.end() && *it == L'\\') {
      ++it;
      ++backslashes;
    }
    if (it == arg.end()) {
      // 닫는 따옴표 앞의 역슬래시는 두 배로 써야 글자로 남습니다.
      command.append(backslashes * 2, L'\\');
      break;
    }
    if (*it == L'"') {
      command.append(backslashes * 2 + 1, L'\\');
      command += L'"';
    } else {
      command.append(backslashes, L'\\');
      command += *it;
    }
  }
  command += L'"';
}

std::wstring BuildCommandLine(const ProcessLaunch& launch) {
  // 첫 토큰(프로그램)은 역슬래시 규칙 없이 따옴표까지 읽히고, 경로에는 따옴표가 올 수 없습니다.
  std::wstring command = L"\"" + Widen(launch.program) + L"\"";
  for (const std::string& arg : launch.arguments) {
    command += L' ';
    AppendQuoted(command, Widen(arg));
  }
  return command;
}

bool SameName(const std::wstring& entry, const std::wstring& name) {
  if (entry.size() <= name.size() || entry[name.size()] != L'=') return false;
  return CompareStringOrdinal(entry.c_str(), static_cast<int>(name.size()), name.c_str(),
                              static_cast<int>(name.size()), TRUE) == CSTR_EQUAL;
}

std::wstring BuildEnvironmentBlock(const ProcessLaunch& launch) {
  std::vector<std::wstring> entries;
  if (wchar_t* block = GetEnvironmentStringsW(); block != nullptr) {
    for (const wchar_t* p = block; *p != L'\0'; p += wcslen(p) + 1) entries.emplace_back(p);
    FreeEnvironmentStringsW(block);
  }
  for (const auto& [name, value] : launch.extra_env) {
    const std::wstring wide_name = Widen(name);
    std::erase_if(entries, [&](const std::wstring& entry) { return SameName(entry, wide_name); });
    entries.push_back(wide_name + L"=" + Widen(value));
  }
  // 환경 블록은 이름을 대소문자 구분 없이 정렬해야 한다고 CreateProcess 문서가 정합니다.
  std::sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()),
                                TRUE) == CSTR_LESS_THAN;
  });
  std::wstring block;
  for (const std::wstring& entry : entries) {
    block += entry;
    block += L'\0';
  }
  block += L'\0';
  return block;
}

std::string DescribeError(DWORD code) {
  switch (code) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return "실행 파일을 찾지 못했습니다";
    case ERROR_DIRECTORY:
      return "작업 폴더가 없습니다";
    case ERROR_ACCESS_DENIED:
      return "실행 권한이 없습니다";
    default:
      return "실행하지 못했습니다(Windows 오류 " + std::to_string(code) + ")";
  }
}

}  // namespace

struct ChildProcess::Impl {
  HANDLE process = nullptr;
  HANDLE job = nullptr;
  HANDLE stdin_write = nullptr;
  HANDLE stdout_read = nullptr;
  DWORD pid = 0;

  std::mutex state_mutex;
  std::mutex write_mutex;
  OutputLines output;
  std::atomic<bool> stop{false};
  std::thread reader;

  void ReadLoop() {
    char buffer[65536];
    while (!stop.load()) {
      DWORD got = 0;
      if (!ReadFile(stdout_read, buffer, sizeof(buffer), &got, nullptr) || got == 0) break;
      output.Append(buffer, got);
    }
    output.Close();
  }
};

ChildProcess::ChildProcess(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<ChildProcess> ChildProcess::Start(const ProcessLaunch& launch, std::string* error) {
  SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE in_read = nullptr, in_write = nullptr, out_read = nullptr, out_write = nullptr;
  if (!CreatePipe(&in_read, &in_write, &inherit, 0) || !CreatePipe(&out_read, &out_write, &inherit, 0)) {
    SetError(error, "파이프를 만들지 못했습니다");
    for (HANDLE* h : {&in_read, &in_write, &out_read, &out_write}) CloseHandleOnce(h);
    return nullptr;
  }
  // 부모 쪽 파이프 핸들이 자식에 상속되면 자식이 끝나도 출력 EOF가 오지 않습니다.
  SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);

  // 다른 스레드가 동시에 띄우는 프로세스와 핸들이 섞이지 않게 상속할 핸들을 둘로 제한합니다.
  HANDLE inherited[2] = {in_read, out_write};
  SIZE_T list_size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &list_size);
  std::vector<unsigned char> list_bytes(list_size);
  auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(list_bytes.data());
  const bool list_ok = InitializeProcThreadAttributeList(list, 1, 0, &list_size) &&
                       UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                                 sizeof(inherited), nullptr, nullptr);

  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = in_read;
  startup.StartupInfo.hStdOutput = out_write;
  startup.StartupInfo.hStdError = out_write;
  startup.lpAttributeList = list_ok ? list : nullptr;

  std::wstring command = BuildCommandLine(launch);
  std::wstring env_block;
  if (!launch.extra_env.empty()) env_block = BuildEnvironmentBlock(launch);
  const std::wstring cwd = Widen(launch.working_dir);

  // 작업 개체에 넣기 전에 하위 프로세스를 띄우지 못하게 멈춘 채로 만듭니다.
  DWORD flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
  if (list_ok) flags |= EXTENDED_STARTUPINFO_PRESENT;
  if (launch.low_priority) flags |= BELOW_NORMAL_PRIORITY_CLASS;
  PROCESS_INFORMATION info{};
  const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, flags,
                                      env_block.empty() ? nullptr : env_block.data(),
                                      cwd.empty() ? nullptr : cwd.c_str(), &startup.StartupInfo, &info);
  const DWORD create_error = GetLastError();
  if (list_ok) DeleteProcThreadAttributeList(list);
  CloseHandleOnce(&in_read);
  CloseHandleOnce(&out_write);
  if (!created) {
    CloseHandleOnce(&in_write);
    CloseHandleOnce(&out_read);
    SetError(error, DescribeError(create_error) + ": " + launch.program);
    return nullptr;
  }

  // 작업 개체로 묶어 Kill이 하위 프로세스까지 끝내고, 앱이 비정상 종료되어도 관류 분석 프로그램이 남지 않게 합니다.
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (job != nullptr) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(job, info.hProcess)) {
      CloseHandleOnce(&job);
    }
  }
  ResumeThread(info.hThread);
  CloseHandle(info.hThread);

  auto impl = std::make_unique<Impl>();
  impl->process = info.hProcess;
  impl->job = job;
  impl->pid = info.dwProcessId;
  impl->stdin_write = in_write;
  impl->stdout_read = out_read;
  Impl* raw = impl.get();
  impl->reader = std::thread([raw] { raw->ReadLoop(); });
  return std::unique_ptr<ChildProcess>(new ChildProcess(std::move(impl)));
}

ChildProcess::~ChildProcess() {
  CloseInput();
  Kill();
  WaitForSingleObject(impl_->process, INFINITE);
  impl_->stop.store(true);
  // 작업 개체에 넣지 못한 하위 프로세스가 출력 파이프를 쥐고 있으면 ReadFile이 끝나지 않아 취소합니다.
  for (int i = 0; i < 100 && !impl_->output.closed(); ++i) Sleep(10);
  while (!impl_->output.closed()) {
    CancelSynchronousIo(impl_->reader.native_handle());
    Sleep(10);
  }
  if (impl_->reader.joinable()) impl_->reader.join();
  CloseHandleOnce(&impl_->stdout_read);
  CloseHandleOnce(&impl_->job);
  CloseHandleOnce(&impl_->process);
}

bool ChildProcess::WriteLine(const std::string& line) {
  std::lock_guard<std::mutex> lock(impl_->write_mutex);
  if (impl_->stdin_write == nullptr) return false;
  const std::string data = line + "\n";
  size_t sent = 0;
  while (sent < data.size()) {
    DWORD wrote = 0;
    if (!WriteFile(impl_->stdin_write, data.data() + sent, static_cast<DWORD>(data.size() - sent), &wrote,
                   nullptr) ||
        wrote == 0) {
      return false;
    }
    sent += wrote;
  }
  return true;
}

void ChildProcess::CloseInput() {
  std::lock_guard<std::mutex> lock(impl_->write_mutex);
  CloseHandleOnce(&impl_->stdin_write);
}

LineStatus ChildProcess::ReadLine(std::string* line, int64_t timeout_ms) {
  return impl_->output.Read(line, timeout_ms);
}

std::optional<int32_t> ChildProcess::Wait(int64_t timeout_ms) {
  const DWORD wait_ms = timeout_ms <= 0 ? 0 : static_cast<DWORD>(std::min<int64_t>(timeout_ms, INFINITE - 1));
  if (WaitForSingleObject(impl_->process, wait_ms) != WAIT_OBJECT_0) return std::nullopt;
  DWORD code = 0;
  GetExitCodeProcess(impl_->process, &code);
  return static_cast<int32_t>(code);
}

void ChildProcess::Kill() {
  std::lock_guard<std::mutex> lock(impl_->state_mutex);
  // 프로세스 핸들을 쥐고 있는 동안 번호가 재사용되지 않으므로 다른 프로세스에 닿지 않습니다.
  if (WaitForSingleObject(impl_->process, 0) == WAIT_OBJECT_0) return;
  const UINT code = static_cast<UINT>(kKilledExitCode);
  if (impl_->job != nullptr) {
    TerminateJobObject(impl_->job, code);
  } else {
    TerminateProcess(impl_->process, code);
  }
}

int64_t ChildProcess::Id() const { return impl_->pid; }

}  // namespace hwlab::platform
