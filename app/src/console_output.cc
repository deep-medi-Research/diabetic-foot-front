#include "console_output.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>

#ifdef _WIN32
// windows.h는 매크로(ERROR, min, max 등)가 앞 헤더와 겹치지 않게 마지막에 넣습니다.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

std::mutex g_write_mutex;  // 콘솔에 붙고 떼는 동안 다른 스레드의 쓰기가 끼어들지 않게 합니다

bool Usable(HANDLE handle) {
  return handle != nullptr && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

// 콘솔에는 UTF-16으로 씁니다. 한 번에 쓰는 글자 수에 상한이 있어(약 64KB) 나눠 씁니다.
void WriteToConsole(HANDLE console, const QString& text) {
  constexpr size_t kChunk = 8192;
  const std::wstring wide = text.toStdWString();
  for (size_t at = 0; at < wide.size(); at += kChunk) {
    DWORD written = 0;
    WriteConsoleW(console, wide.data() + at, static_cast<DWORD>(std::min(kChunk, wide.size() - at)), &written, nullptr);
  }
}

void WriteTo(HANDLE handle, const QString& text) {
  DWORD mode = 0;
  if (GetConsoleMode(handle, &mode)) {
    WriteToConsole(handle, text);
    return;
  }
  DWORD written = 0;
  const QByteArray bytes = text.toUtf8();
  WriteFile(handle, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr);
}

void Write(DWORD which, const QString& text) {
  if (text.isEmpty()) return;
  std::lock_guard<std::mutex> lock(g_write_mutex);
  HANDLE handle = GetStdHandle(which);
  if (Usable(handle)) {
    WriteTo(handle, text);
    return;
  }
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
  HANDLE console = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (console != INVALID_HANDLE_VALUE) {
    WriteToConsole(console, text);
    CloseHandle(console);
  }
  FreeConsole();
}

}  // namespace

void WriteStdout(const QString& text) { Write(STD_OUTPUT_HANDLE, text); }

void WriteStderr(const QString& text) { Write(STD_ERROR_HANDLE, text); }

void WriteToDebugger(const QString& text) { OutputDebugStringW(reinterpret_cast<const wchar_t*>(text.utf16())); }

#else

namespace {

std::mutex g_write_mutex;

void Write(std::FILE* stream, const QString& text) {
  const QByteArray bytes = text.toUtf8();
  std::lock_guard<std::mutex> lock(g_write_mutex);
  std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stream);
  std::fflush(stream);
}

}  // namespace

void WriteStdout(const QString& text) { Write(stdout, text); }

void WriteStderr(const QString& text) { Write(stderr, text); }

void WriteToDebugger(const QString&) {}

#endif
