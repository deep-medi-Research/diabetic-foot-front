// 시험용 가짜 관류 분석 프로그램입니다. dfr_worker.py와 같은 꼴의 줄을 찍고, 첫 인자로 동작을 고릅니다.
//
//   progress        진행 줄을 찍고 0으로 끝남
//   worker [버전]   [dfr] ready(버전을 주면 뒤에 JSON) 뒤 표준 입력 요청마다 답함. 입력이 닫히면 0으로 끝남
//                   요청 이름이 fail이면 실패, hang이면 진행 줄 뒤 60초 출력 없음, crash이면 진행 줄 뒤 종료 코드 3,
//                   nosummary이면 요약 파일 없이 성공, slow로 시작하면 단계마다 0.4초 대기 뒤 성공.
//                   presegment 요청에는 발마다 [dfr] presegmented 한 줄로 답함.
//                   defer_figures가 true인 요청은 성공한 done 뒤에 [dfr] figures 한 줄을 더 찍음.
//                   그 밖의 이름은 <output>/<name>_summary.json을 쓰고(쓰지 못해도) 성공을 알립니다
//   <파일>.py ...   첫 인자가 .py로 끝나면 worker와 같습니다(AnalyzerLaunch가 만든 명령줄로 띄울 때)
//   exit <코드>     한 줄을 찍고 표준 오류에도 한 줄을 쓴 뒤 그 코드로 끝남
//   hang            한 줄을 찍고 입력을 읽지 않은 채 60초 기다림
//   args <인자...>  인자마다 arg:<값> 한 줄
//   cwd             작업 폴더
//   env <이름>      환경 변수 값
//   flood <바이트>  입력을 읽지 않고 그만큼 출력한 뒤 0으로 끝남
//   stderr          표준 출력과 표준 오류에 한 줄씩
//   priority        Windows는 우선순위 클래스(normal, below_normal 등), 그 밖은 nice 값

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/resource.h>

#include <cerrno>
#endif

namespace {

#ifdef _WIN32
std::string Narrow(const std::wstring& wide) {
  if (wide.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr,
                                       nullptr);
  std::string text(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), text.data(), size, nullptr, nullptr);
  return text;
}

std::wstring Widen(const std::string& text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
  return wide;
}
#endif

void Out(const std::string& line) {
  std::fwrite(line.data(), 1, line.size(), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
}

bool ReadInputLine(std::string* line) {
  line->clear();
  int c = 0;
  while ((c = std::fgetc(stdin)) != EOF) {
    if (c == '\n') return true;
    line->push_back(static_cast<char>(c));
  }
  return !line->empty();
}

std::string GetEnv(const std::string& name) {
#ifdef _WIN32
  const wchar_t* value = _wgetenv(Widen(name).c_str());
  return value == nullptr ? "(none)" : Narrow(value);
#else
  const char* value = std::getenv(name.c_str());
  return value == nullptr ? "(none)" : value;
#endif
}

// 요청 JSON에서 문자열 값 하나를 꺼냅니다. 시험 요청만 읽으므로 \\와 \"만 풉니다.
std::string RequestString(const std::string& line, const std::string& key) {
  const std::string mark = "\"" + key + "\":\"";
  const size_t begin = line.find(mark);
  if (begin == std::string::npos) return {};
  std::string value;
  for (size_t i = begin + mark.size(); i < line.size(); ++i) {
    char c = line[i];
    if (c == '"') break;
    if (c == '\\' && i + 1 < line.size()) c = line[++i];
    value.push_back(c);
  }
  return value;
}

void WriteSummary(const std::string& output, const std::string& name) {
  const std::string path = output + "/" + name + "_summary.json";
#ifdef _WIN32
  std::FILE* file = _wfopen(Widen(path).c_str(), L"wb");
#else
  std::FILE* file = std::fopen(path.c_str(), "wb");
#endif
  if (file == nullptr) return;
  // 결과 창을 시험할 수 있게 실제 요약과 같은 키를 쓰고, 모델 이름으로 가짜 관류 분석 프로그램 결과임을 밝힙니다.
  const std::string text =
      "{\"fps\": 30.0, \"n_frames\": 900, \"window_s\": 10, \"window_stride_s\": 2, \"n_windows\": 11, "
      "\"pi_left\": 0.08, \"pi_right\": 0.1, \"pi_ratio_left_right\": 0.8, "
      "\"classification\": {\"kind\": \"fake analyzer\", \"p_pad\": 0.2, \"threshold\": 0.45, "
      "\"decision\": \"normal\"}, "
      "\"dfr\": {\"upstream_commit\": \"5e5178d\", \"worker_version\": \"fake-1\"}}\n";
  std::fwrite(text.data(), 1, text.size(), file);
  std::fclose(file);
}

void PauseMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

int Worker(const std::string& version) {
  Out(version.empty() ? "[dfr] ready"
                      : "[dfr] ready {\"worker_version\": \"" + version + "\", \"upstream_commit\": \"5e5178d\"}");
  std::string line;
  while (ReadInputLine(&line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    if (line.find("\"preload\"") != std::string::npos) {
      Out("[dfr] preloaded");
      continue;
    }
    if (line.find("\"presegment\"") != std::string::npos) {
      Out("[dfr] presegmented {\"video\": \"left\", \"ok\": true}");
      Out("[dfr] presegmented {\"video\": \"right\", \"ok\": true}");
      continue;
    }
    const std::string name = RequestString(line, "name");
    const int pause_ms = name.rfind("slow", 0) == 0 ? 400 : 0;
    Out("[fake] request " + line);
    Out("[0/5] Loading segmentation model");
    PauseMs(pause_ms);
    Out("[1/5] Reading videos [2 s]");
    PauseMs(pause_ms);
    Out("  [1/7] thermal sub-step");
    if (name == "hang") {
      PauseMs(60'000);
      continue;
    }
    if (name == "crash") {
      std::fputs("Traceback (most recent call last)\nMemoryError\n", stderr);
      std::fflush(stderr);
      return 3;
    }
    PauseMs(pause_ms);
    Out("[5/5] Writing outputs");
    if (name == "fail") {
      Out("[dfr] done {\"ok\": false, \"error\": \"left.avi 가 없습니다: C:\\\\촬영\\\\left.avi\\n두 번째 줄\"}");
    } else {
      // 결과 폴더가 없으면 요약을 쓰지 못한 채 성공을 알립니다. 앱 쪽이 요약 파일로 성공을 다시 확인하는지 시험합니다.
      if (name != "nosummary") WriteSummary(RequestString(line, "output"), name);
      Out("[dfr] done {\"ok\": true, \"error\": \"\"}");
      if (line.find("\"defer_figures\":true") != std::string::npos) Out("[dfr] figures {\"ok\": true, \"seconds\": 1.5}");
    }
  }
  return 0;
}

int Run(const std::vector<std::string>& args) {
  const std::string mode = args.size() > 1 ? args[1] : "";
  if (mode == "progress") {
    Out("[0/5] Loading segmentation model [0 s]");
    Out("[1/5] Reading videos [1 s]");
    Out("  [1/7] thermal sub-step");
    Out("[2/5] Pulse template [3 s]");
    Out("[5/5] Done [9 s]");
    return 0;
  }
  if (mode == "worker") return Worker(args.size() > 2 ? args[2] : "");
  if (mode.size() > 3 && mode.compare(mode.size() - 3, 3, ".py") == 0) return Worker("");
  if (mode == "priority") {
#ifdef _WIN32
    const DWORD priority = GetPriorityClass(GetCurrentProcess());
    Out(priority == BELOW_NORMAL_PRIORITY_CLASS ? "below_normal"
        : priority == NORMAL_PRIORITY_CLASS     ? "normal"
        : priority == IDLE_PRIORITY_CLASS       ? "idle"
                                                : "other");
#else
    errno = 0;
    Out(std::to_string(getpriority(PRIO_PROCESS, 0)));
#endif
    return 0;
  }
  if (mode == "exit" && args.size() > 2) {
    Out("exiting");
    std::fputs("Traceback (most recent call last)\n", stderr);
    std::fflush(stderr);
    return std::atoi(args[2].c_str());
  }
  if (mode == "hang") {
    Out("[0/5] Loading segmentation model");
    std::this_thread::sleep_for(std::chrono::seconds(60));
    return 0;
  }
  if (mode == "args") {
    for (size_t i = 2; i < args.size(); ++i) Out("arg:" + args[i]);
    return 0;
  }
  if (mode == "cwd") {
#ifdef _WIN32
    Out(Narrow(std::filesystem::current_path().wstring()));
#else
    Out(std::filesystem::current_path().string());
#endif
    return 0;
  }
  if (mode == "env" && args.size() > 2) {
    Out(GetEnv(args[2]));
    return 0;
  }
  if (mode == "flood" && args.size() > 2) {
    const long long total = std::atoll(args[2].c_str());
    const std::string row(99, 'x');
    for (long long sent = 0; sent < total; sent += 100) {
      std::fwrite(row.data(), 1, row.size(), stdout);
      std::fputc('\n', stdout);
    }
    Out("flood-end");
    return 0;
  }
  if (mode == "stderr") {
    Out("to-stdout");
    std::fputs("to-stderr\n", stderr);
    std::fflush(stderr);
    return 0;
  }
  std::fputs("unknown mode\n", stderr);
  return 2;
}

}  // namespace

#ifdef _WIN32
// 명령줄을 UTF-16으로 받아야 한글 인자가 코드 페이지 변환으로 깨지지 않습니다.
int wmain(int argc, wchar_t** argv) {
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stdin), _O_BINARY);
  std::vector<std::string> args;
  for (int i = 0; i < argc; ++i) args.push_back(Narrow(argv[i]));
  return Run(args);
}
#else
int main(int argc, char** argv) {
  std::vector<std::string> args(argv, argv + argc);
  return Run(args);
}
#endif
