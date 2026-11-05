#include "console_output.h"

#include <cstdio>
#include <string>

#ifdef _WIN32
// windows.h는 매크로(ERROR, min, max 등)가 앞 헤더와 겹치지 않게 마지막에 넣습니다.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

bool Usable(HANDLE handle) {
  return handle != nullptr && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

void Write(DWORD which, const QString& text) {
  HANDLE handle = GetStdHandle(which);
  if (!Usable(handle) || text.isEmpty()) return;
  DWORD written = 0;
  DWORD mode = 0;
  if (GetConsoleMode(handle, &mode)) {
    const std::wstring wide = text.toStdWString();
    WriteConsoleW(handle, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr);
    return;
  }
  const QByteArray bytes = text.toUtf8();
  WriteFile(handle, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr);
}

}  // namespace

void AttachParentConsole() {
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
  for (const DWORD which : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
    if (Usable(GetStdHandle(which))) continue;
    HANDLE console = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, 0, nullptr);
    if (console != INVALID_HANDLE_VALUE) SetStdHandle(which, console);
  }
}

void WriteStdout(const QString& text) { Write(STD_OUTPUT_HANDLE, text); }

void WriteStderr(const QString& text) { Write(STD_ERROR_HANDLE, text); }

void WriteToDebugger(const QString& text) { OutputDebugStringW(reinterpret_cast<const wchar_t*>(text.utf16())); }

#else

namespace {

void Write(std::FILE* stream, const QString& text) {
  const QByteArray bytes = text.toUtf8();
  std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stream);
  std::fflush(stream);
}

}  // namespace

void AttachParentConsole() {}

void WriteStdout(const QString& text) { Write(stdout, text); }

void WriteStderr(const QString& text) { Write(stderr, text); }

void WriteToDebugger(const QString&) {}

#endif
