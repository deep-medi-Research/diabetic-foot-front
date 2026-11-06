#include "hwlab/platform/perfusion_analysis.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <vector>

#include "hwlab/platform/shot_folder.h"
#include "hwlab/record/analysis_inputs.h"
#include "json_reader.h"

namespace hwlab::platform {

namespace {

constexpr const char kMark[] = "[dfr] ";

void AppendQuoted(std::string& out, const std::string& text) {
  out.push_back('"');
  for (const char c : text) {
    const auto byte = static_cast<unsigned char>(c);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (byte < 0x20) {
          char code[8];
          std::snprintf(code, sizeof(code), "\\u%04x", byte);
          out += code;
        } else {
          out.push_back(c);
        }
    }
  }
  out.push_back('"');
}

std::string Trim(const std::string& text) {
  const size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const size_t last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

bool ParseInt(const std::string& text, int32_t* value) {
  if (text.empty() || text.size() > 6) return false;
  int32_t v = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  *value = v;
  return true;
}

// 관류 분석 프로그램이 줄 끝에 출력하는 경과 시간(`[12 s]`, 하위 단계는 `(0 s)`)은 앱이 따로 재므로 제거합니다.
std::string StripElapsed(std::string message) {
  if (message.empty()) return message;
  const char open = message.back() == ']' ? '[' : message.back() == ')' ? '(' : '\0';
  if (open == '\0') return message;
  const size_t start = message.rfind(std::string(" ") + open);
  if (start == std::string::npos) return message;
  // 괄호 안이 숫자와 단위뿐일 때만 뗍니다. 문구 안의 괄호(예: "(started in the background)")는 둡니다.
  const std::string inside = message.substr(start + 2, message.size() - start - 3);
  if (inside.empty() || inside.find_first_not_of("0123456789. s") != std::string::npos) return message;
  return Trim(message.substr(0, start));
}

bool ParseStage(const std::string& line, AnalyzerLine* out) {
  // 줄 맨 앞의 `[n/m]`은 진행 단계, 들여 쓴 `  [k/n]`은 그 단계 안의 하위 단계입니다.
  const size_t open = line.find_first_not_of(" \t");
  if (open == std::string::npos || line[open] != '[') return false;
  const size_t slash = line.find('/', open);
  const size_t close = line.find(']', open);
  if (slash == std::string::npos || close == std::string::npos || slash > close) return false;
  int32_t step = 0;
  int32_t total = 0;
  if (!ParseInt(line.substr(open + 1, slash - open - 1), &step) ||
      !ParseInt(line.substr(slash + 1, close - slash - 1), &total)) {
    return false;
  }
  if (total <= 0 || step > total) return false;
  out->kind = open == 0 ? AnalyzerLineKind::kStage : AnalyzerLineKind::kSubStage;
  out->step = step;
  out->total = total;
  out->message = StripElapsed(Trim(line.substr(close + 1)));
  return true;
}

void ParseDone(const std::string& json, AnalyzerLine* out) {
  out->kind = AnalyzerLineKind::kDone;
  const auto members = JsonReader(Trim(json)).Read();
  if (!members) {
    out->ok = false;
    out->error = "관류 분석 프로그램의 완료 줄을 읽지 못했습니다: " + json;
    return;
  }
  const auto ok = members->find("ok");
  const auto error = members->find("error");
  out->ok = ok != members->end() && ok->second == "true";
  out->error = error != members->end() ? error->second : std::string();
  if (!out->ok && out->error.empty() && ok == members->end()) out->error = "완료 줄에 ok가 없습니다: " + json;
}

std::optional<std::string> ReadWholeFile(const std::string& path) {
  std::FILE* file = OpenFileUtf8(path, "rb");
  if (file == nullptr) return std::nullopt;
  std::string text;
  char buffer[8192];
  size_t n = 0;
  while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) text.append(buffer, n);
  std::fclose(file);
  return text;
}

std::string JoinPath(const std::string& dir, const std::string& name) {
  if (dir.empty() || dir.back() == '/' || dir.back() == '\\') return dir + name;
  return dir + "/" + name;
}

}  // namespace

std::optional<std::string> BuildPerfusionRequestLine(const PerfusionRequest& request, std::string* error) {
  if (request.left_video.empty() != request.right_video.empty()) {
    if (error != nullptr) *error = "좌우 영상 경로는 둘 다 주거나 둘 다 비워야 합니다";
    return std::nullopt;
  }
  if ((request.left_frames && *request.left_frames < 0) || (request.right_frames && *request.right_frames < 0)) {
    if (error != nullptr) *error = "영상 장 수가 음수입니다";
    return std::nullopt;
  }
  std::string out = "{\"rec_dir\":";
  AppendQuoted(out, request.recording_dir);
  // 비어 있으면 키를 넣지 않아 관류 분석 프로그램이 촬영 폴더의 left.avi, right.avi를 찾는 기존 동작을 유지합니다.
  if (!request.left_video.empty()) {
    out += ",\"left_video\":";
    AppendQuoted(out, request.left_video);
  }
  if (!request.right_video.empty()) {
    out += ",\"right_video\":";
    AppendQuoted(out, request.right_video);
  }
  if (request.left_frames) out += ",\"left_frames\":" + std::to_string(*request.left_frames);
  if (request.right_frames) out += ",\"right_frames\":" + std::to_string(*request.right_frames);
  out += ",\"output\":";
  AppendQuoted(out, request.output_dir);
  out += ",\"name\":";
  AppendQuoted(out, request.name);
  out += ",\"cuff_side\":";
  AppendQuoted(out, request.cuff_side);
  out += ",\"template\":";
  AppendQuoted(out, request.pulse_template);
  out += ",\"classify\":";
  out += request.classify ? "true" : "false";
  out += ",\"pad_classifier\":";
  out += request.pad_classifier ? "true" : "false";
  out += ",\"gif\":";
  out += request.gif ? "true" : "false";
  out += ",\"defer_figures\":";
  out += request.defer_figures ? "true" : "false";
  out += "}";
  return out;
}

std::string BuildPreloadLine() { return "{\"preload\":true}"; }

std::string BuildPresegmentLine(const std::string& left_video, const std::string& right_video) {
  std::string out = "{\"presegment\":true,\"left_video\":";
  AppendQuoted(out, left_video);
  out += ",\"right_video\":";
  AppendQuoted(out, right_video);
  out += "}";
  return out;
}

AnalyzerLine ParseAnalyzerLine(const std::string& line) {
  AnalyzerLine out;
  const std::string mark(kMark);
  if (line.compare(0, mark.size(), mark) == 0) {
    const std::string rest = line.substr(mark.size());
    if (rest == "ready" || rest.compare(0, 6, "ready ") == 0) {
      out.kind = AnalyzerLineKind::kReady;
      // 버전 JSON은 덧붙인 정보라 읽지 못해도 준비 완료로 봅니다.
      if (rest.size() > 6) {
        if (const auto members = JsonReader(Trim(rest.substr(6))).Read()) {
          if (const auto it = members->find("api_version"); it != members->end()) out.api_version = it->second;
          if (const auto it = members->find("worker_version"); it != members->end()) out.worker_version = it->second;
          if (const auto it = members->find("upstream_commit"); it != members->end()) out.upstream_commit = it->second;
        }
      }
    } else if (rest == "preloaded") {
      out.kind = AnalyzerLineKind::kPreloaded;
    } else if (rest.compare(0, 14, "preload-failed") == 0) {
      out.kind = AnalyzerLineKind::kPreloadFailed;
      out.message = Trim(rest.substr(14));
    } else if (rest.compare(0, 13, "presegmented ") == 0) {
      out.kind = AnalyzerLineKind::kPresegmented;
      out.message = Trim(rest.substr(13));
    } else if (rest.compare(0, 8, "figures ") == 0) {
      out.kind = AnalyzerLineKind::kFigures;
      out.message = Trim(rest.substr(8));
    } else if (rest == "idle-exit") {
      out.kind = AnalyzerLineKind::kIdleExit;
    } else if (rest.compare(0, 5, "done ") == 0) {
      ParseDone(rest.substr(5), &out);
    } else {
      out.message = line;
    }
    return out;
  }
  if (ParseStage(line, &out)) return out;
  out.message = line;
  return out;
}

WorkerStamp ReadWorkerStamp(const std::string& summary_json) {
  WorkerStamp stamp;
  std::string value;
  if (JsonReader(summary_json).FindString({"dfr", "api_version"}, &value)) stamp.api_version = value;
  value.clear();
  if (JsonReader(summary_json).FindString({"dfr", "worker_version"}, &value)) stamp.worker_version = value;
  value.clear();
  if (JsonReader(summary_json).FindString({"dfr", "upstream_commit"}, &value)) stamp.upstream_commit = value;
  return stamp;
}

std::string PerfusionSummaryPath(const std::string& output_dir, const std::string& name) {
  std::string path = output_dir;
  if (!path.empty() && path.back() != '/' && path.back() != '\\') path.push_back('/');
  return path + name + "_summary.json";
}

std::string PerfusionOutputDir(const std::string& shot_folder, const std::string& variant) {
  std::string dir = shot_folder;
  while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
  const std::string out = JoinPath(dir, kPerfusionFolderName);
  return variant.empty() ? out : JoinPath(out, variant);
}

std::optional<PerfusionRequest> PerfusionRequestForShot(const std::string& folder, std::string* error) {
  const auto fail = [&](const std::string& why) -> std::optional<PerfusionRequest> {
    if (error != nullptr) *error = why;
    return std::nullopt;
  };
  std::string dir = folder;
  while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
  const size_t cut = dir.find_last_of("/\\");
  const std::string stem = cut == std::string::npos ? dir : dir.substr(cut + 1);
  if (stem.empty()) return fail("촬영 폴더 이름이 비어 있습니다: " + folder);
  const std::string manifest_path = JoinPath(dir, stem + "_shot.json");
  const auto manifest = ReadWholeFile(manifest_path);
  if (!manifest) return fail("매니페스트를 열지 못했습니다: " + manifest_path);
  const auto file_name = [&](const char* role, std::string* out) {
    return JsonReader(*manifest).FindString({"files", role}, out) && !out->empty();
  };
  PerfusionRequest request;
  std::string left_name, right_name, left_sidecar, right_sidecar;
  if (!file_name("left_video", &left_name) || !file_name("right_video", &right_name)) {
    return fail("매니페스트에 좌우 영상이 없습니다: " + manifest_path);
  }
  if (!file_name("left_sidecar", &left_sidecar) || !file_name("right_sidecar", &right_sidecar)) {
    return fail("매니페스트에 좌우 사이드카가 없습니다: " + manifest_path);
  }
  const auto written = [&](const std::string& name) -> std::optional<int64_t> {
    const std::string path = JoinPath(dir, name);
    const auto json = ReadWholeFile(path);
    std::string why;
    const auto summary = json ? record::ReadSidecarSummary(*json, &why) : std::nullopt;
    if (!summary) {
      fail("사이드카를 읽지 못했습니다: " + path + (why.empty() ? std::string() : " (" + why + ")"));
      return std::nullopt;
    }
    return summary->written_frames;
  };
  request.left_frames = written(left_sidecar);
  if (!request.left_frames) return std::nullopt;
  request.right_frames = written(right_sidecar);
  if (!request.right_frames) return std::nullopt;
  request.recording_dir = dir;
  request.left_video = JoinPath(dir, left_name);
  request.right_video = JoinPath(dir, right_name);
  request.output_dir = JoinPath(dir, kPerfusionFolderName);
  request.name = stem;
  return request;
}

}  // namespace hwlab::platform
