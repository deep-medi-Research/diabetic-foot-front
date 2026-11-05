#include "hwlab/record/analysis_inputs.h"

#include <algorithm>
#include <iterator>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>

namespace hwlab::record {

namespace {

// 사이드카에는 소수(clock.rate)가 있어 정수만 받는 선언 파일 읽기와 따로 둡니다. 소수는 건너뛰고 정수만 값으로 남깁니다.
struct Value {
  enum class Kind : uint8_t { kNull, kBool, kNumber, kString, kObject, kArray } kind = Kind::kNull;
  std::optional<int64_t> integer;
  std::string text;
  std::map<std::string, std::unique_ptr<Value>> members;
};

class Reader {
 public:
  explicit Reader(const std::string& json) : json_(json) {}

  std::unique_ptr<Value> ReadDocument() {
    auto value = ReadValue(0);
    SkipSpace();
    if (value && pos_ != json_.size()) return nullptr;
    return value;
  }

 private:
  static constexpr int kMaxDepth = 32;

  void SkipSpace() {
    while (pos_ < json_.size() && (json_[pos_] == ' ' || json_[pos_] == '\n' || json_[pos_] == '\r' || json_[pos_] == '\t')) {
      ++pos_;
    }
  }
  bool Take(char c) {
    SkipSpace();
    if (pos_ < json_.size() && json_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }
  bool TakeWord(const char* word) {
    const std::string w(word);
    if (json_.compare(pos_, w.size(), w) != 0) return false;
    pos_ += w.size();
    return true;
  }

  std::unique_ptr<Value> ReadValue(int depth) {
    if (depth > kMaxDepth) return nullptr;
    SkipSpace();
    if (pos_ >= json_.size()) return nullptr;
    const char c = json_[pos_];
    if (c == '{') return ReadObject(depth);
    auto value = std::make_unique<Value>();
    if (c == '[') {
      value->kind = Value::Kind::kArray;
      return ReadArray(depth) ? std::move(value) : nullptr;
    }
    if (c == '"') {
      value->kind = Value::Kind::kString;
      return ReadString(&value->text) ? std::move(value) : nullptr;
    }
    if (TakeWord("null")) return value;
    if (TakeWord("true") || TakeWord("false")) {
      value->kind = Value::Kind::kBool;
      return value;
    }
    return ReadNumber(value.get()) ? std::move(value) : nullptr;
  }

  bool ReadNumber(Value* value) {
    const size_t start = pos_;
    if (pos_ < json_.size() && json_[pos_] == '-') ++pos_;
    bool fraction = false;
    while (pos_ < json_.size()) {
      const char c = json_[pos_];
      if (c >= '0' && c <= '9') {
        ++pos_;
      } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || (c == '-' && pos_ > start)) {
        fraction = true;
        ++pos_;
      } else {
        break;
      }
    }
    const std::string text = json_.substr(start, pos_ - start);
    if (text.empty() || text == "-") return false;
    value->kind = Value::Kind::kNumber;
    if (!fraction) {
      char* end = nullptr;
      const long long parsed = std::strtoll(text.c_str(), &end, 10);
      if (end != text.c_str() + text.size()) return false;
      value->integer = parsed;
    }
    return true;
  }

  bool ReadString(std::string* out) {
    ++pos_;  // 여는 따옴표
    while (pos_ < json_.size()) {
      const char c = json_[pos_++];
      if (c == '"') return true;
      if (c != '\\') {
        out->push_back(c);
        continue;
      }
      if (pos_ >= json_.size()) return false;
      const char e = json_[pos_++];
      switch (e) {
        case '"': case '\\': case '/': out->push_back(e); break;
        case 'n': out->push_back('\n'); break;
        case 'r': out->push_back('\r'); break;
        case 't': out->push_back('\t'); break;
        case 'b': out->push_back('\b'); break;
        case 'f': out->push_back('\f'); break;
        case 'u':
          // 사이드카는 제어 문자만 이 형식으로 씁니다. 필요한 키의 값에는 나오지 않으므로 자리만 건너뜁니다.
          if (pos_ + 4 > json_.size()) return false;
          pos_ += 4;
          out->push_back('?');
          break;
        default: return false;
      }
    }
    return false;
  }

  bool ReadArray(int depth) {
    ++pos_;
    if (Take(']')) return true;
    do {
      if (!ReadValue(depth + 1)) return false;
    } while (Take(','));
    return Take(']');
  }

  std::unique_ptr<Value> ReadObject(int depth) {
    auto object = std::make_unique<Value>();
    object->kind = Value::Kind::kObject;
    ++pos_;
    if (!Take('}')) {
      do {
        SkipSpace();
        std::string key;
        if (pos_ >= json_.size() || json_[pos_] != '"' || !ReadString(&key) || !Take(':')) return nullptr;
        auto member = ReadValue(depth + 1);
        if (!member) return nullptr;
        object->members[key] = std::move(member);
      } while (Take(','));
      if (!Take('}')) return nullptr;
    }
    return object;
  }

  const std::string& json_;
  size_t pos_ = 0;
};

const Value* Member(const Value* object, const char* key) {
  if (object == nullptr || object->kind != Value::Kind::kObject) return nullptr;
  const auto it = object->members.find(key);
  return it == object->members.end() ? nullptr : it->second.get();
}

bool ReadInt(const Value* object, const char* key, int64_t* out) {
  const Value* v = Member(object, key);
  if (v == nullptr || !v->integer) return false;
  *out = *v->integer;
  return true;
}

bool ReadInt32(const Value* object, const char* key, int32_t* out) {
  int64_t v = 0;
  if (!ReadInt(object, key, &v) || v < INT32_MIN || v > INT32_MAX) return false;
  *out = static_cast<int32_t>(v);
  return true;
}

bool Has(const std::vector<FileRole>& roles, FileRole role) {
  return std::find(roles.begin(), roles.end(), role) != roles.end();
}

}  // namespace

std::optional<SidecarSummary> ReadSidecarSummary(const std::string& json, std::string* error) {
  const auto fail = [error](const std::string& why) -> std::optional<SidecarSummary> {
    if (error != nullptr) *error = why;
    return std::nullopt;
  };
  const std::unique_ptr<Value> root = Reader(json).ReadDocument();
  if (!root || root->kind != Value::Kind::kObject) return fail("사이드카 JSON을 읽지 못함");
  int64_t version = 0;
  // written_in_rgb_window는 판 3부터 있습니다.
  if (!ReadInt(root.get(), "schema_version", &version) || version < 3) return fail("사이드카 schema_version이 3보다 작음");

  SidecarSummary s;
  const Value* stream = Member(root.get(), "stream");
  if (stream == nullptr || stream->kind != Value::Kind::kString) return fail("사이드카에 stream이 없음");
  if (stream->text == "rgb") {
    s.stream = StreamKind::kRgb;
  } else if (stream->text == "thermal") {
    s.stream = StreamKind::kThermal;
  } else {
    return fail("사이드카의 stream을 모름: " + stream->text);
  }
  const Value* foot = Member(root.get(), "foot");
  if (foot == nullptr) return fail("사이드카에 foot이 없음");
  if (foot->kind == Value::Kind::kString && foot->text == "left") {
    s.foot = FootSide::kLeft;
  } else if (foot->kind == Value::Kind::kString && foot->text == "right") {
    s.foot = FootSide::kRight;
  } else if (foot->kind != Value::Kind::kNull) {
    return fail("사이드카의 foot을 모름");
  }
  const Value* format = Member(root.get(), "format");
  if (!ReadInt32(format, "width", &s.frame_width) || !ReadInt32(format, "height", &s.frame_height)) {
    return fail("사이드카에 format.width, format.height가 없음");
  }
  const Value* crop = Member(root.get(), "crop");
  if (!ReadInt32(crop, "x", &s.crop.x) || !ReadInt32(crop, "y", &s.crop.y) || !ReadInt32(crop, "width", &s.crop.width) ||
      !ReadInt32(crop, "height", &s.crop.height)) {
    return fail("사이드카에 crop이 없음");
  }
  const Value* frames = Member(root.get(), "frames");
  if (!ReadInt(frames, "written", &s.written_frames) ||
      !ReadInt(frames, "written_in_rgb_window", &s.written_in_rgb_window) ||
      !ReadInt(frames, "missing", &s.missing_frames) || !ReadInt(frames, "dropped", &s.dropped_frames)) {
    return fail("사이드카에 frames의 장 수가 없음");
  }
  return s;
}

std::pair<pixel::Rect, pixel::Rect> ThermalFootRegions(int32_t frame_width, int32_t frame_height,
                                                       const std::optional<std::pair<pixel::Rect, pixel::Rect>>& pixel_regions,
                                                       FootSide pixel_left_foot) {
  std::pair<pixel::Rect, pixel::Rect> by_pixel;
  if (pixel_regions) {
    by_pixel = *pixel_regions;
  } else if (frame_width >= 2 && frame_height > 0) {
    const int32_t half = frame_width / 2;
    by_pixel = {{0, 0, half, frame_height}, {frame_width - half, 0, half, frame_height}};
  }
  if (pixel_left_foot == FootSide::kLeft) return by_pixel;
  return {by_pixel.second, by_pixel.first};
}

std::vector<InputFinding> CheckAnalysisInputs(const AnalysisFiles& files, int64_t min_thermal_frames) {
  std::vector<InputFinding> findings;
  // 좌우 영상이 없으면 분석할 수 없습니다. 커프 단계 추정은 RGB 영상만 쓰므로 열화상 문제는 막지 않는 경고로 남깁니다.
  // 이진 PAD 판정을 켠 분석에서는 열화상을 빼고 RGB만으로 판정합니다.
  static constexpr FileRole kRgbRoles[] = {FileRole::kLeftVideo, FileRole::kLeftSidecar, FileRole::kRightVideo,
                                           FileRole::kRightSidecar};
  static constexpr FileRole kThermalRoles[] = {FileRole::kThermalSidecar, FileRole::kThermalLeftArray,
                                               FileRole::kThermalRightArray};
  for (const FileRole role : kRgbRoles) {
    if (!Has(files.present, role)) findings.push_back({InputProblem::kMissingFile, role, true, 0, 0});
  }
  const bool any_thermal = std::any_of(std::begin(kThermalRoles), std::end(kThermalRoles),
                                       [&](FileRole role) { return Has(files.present, role); });
  if (!any_thermal) {
    findings.push_back({InputProblem::kThermalAbsent, FileRole::kThermalSidecar, false, 0, 0});
  } else {
    for (const FileRole role : kThermalRoles) {
      if (!Has(files.present, role)) findings.push_back({InputProblem::kMissingFile, role, false, 0, 0});
    }
  }
  const std::pair<FileRole, const std::optional<SidecarSummary>*> sidecars[] = {
      {FileRole::kLeftSidecar, &files.left},
      {FileRole::kRightSidecar, &files.right},
      {FileRole::kThermalSidecar, &files.thermal}};
  for (const auto& [role, summary] : sidecars) {
    if (Has(files.present, role) && !summary->has_value()) {
      findings.push_back({InputProblem::kUnreadableSidecar, role, role != FileRole::kThermalSidecar, 0, 0});
    }
  }
  // 분석은 한 발씩 세로로 긴 영상만 받습니다. 저장 영역(crop)이 영상의 크기입니다.
  const std::pair<FileRole, const std::optional<SidecarSummary>*> videos[] = {
      {FileRole::kLeftVideo, &files.left}, {FileRole::kRightVideo, &files.right}};
  for (const auto& [role, summary] : videos) {
    if (summary->has_value() && (*summary)->crop.height <= (*summary)->crop.width) {
      findings.push_back({InputProblem::kNotPortrait, role, true, (*summary)->crop.width, (*summary)->crop.height});
    }
  }
  if (files.thermal) {
    if (files.thermal->written_in_rgb_window < min_thermal_frames) {
      findings.push_back({InputProblem::kFewThermalFrames, FileRole::kThermalSidecar, false,
                          files.thermal->written_in_rgb_window, min_thermal_frames});
    }
    const int64_t gaps = files.thermal->missing_frames + files.thermal->dropped_frames;
    if (gaps > 0) findings.push_back({InputProblem::kThermalGap, FileRole::kThermalSidecar, false, gaps, 0});
  }
  return findings;
}

bool HasBlockingFinding(const std::vector<InputFinding>& findings) {
  return std::any_of(findings.begin(), findings.end(), [](const InputFinding& f) { return f.blocking; });
}

}  // namespace hwlab::record
