#include "hwlab/flow/capture_spec.h"

#include <cctype>
#include <cstdint>
#include <map>
#include <memory>

namespace hwlab::flow {

namespace {

// 선언 파일에 필요한 만큼만 읽는 JSON 값입니다. 정수, 문자열, 참거짓, 객체, 배열을 다룹니다.
struct Json {
  enum class Kind : uint8_t { kNull, kBool, kInt, kString, kObject, kArray } kind = Kind::kNull;
  bool boolean = false;
  int64_t integer = 0;
  std::string text;
  std::map<std::string, std::unique_ptr<Json>> members;
  std::vector<std::unique_ptr<Json>> items;
};

class Reader {
 public:
  explicit Reader(const std::string& text) : text_(text) {}

  std::unique_ptr<Json> ReadDocument(std::string* error) {
    auto value = ReadValue();
    SkipSpace();
    if (value != nullptr && pos_ != text_.size()) {
      error_ = "문서 끝 뒤에 값이 더 있습니다";
      value.reset();
    }
    if (value == nullptr) {
      *error = error_ + " (" + std::to_string(pos_) + "번째 바이트)";
    }
    return value;
  }

 private:
  void SkipSpace() {
    while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
      ++pos_;
    }
  }

  bool Consume(const char* word) {
    const std::string w(word);
    if (text_.compare(pos_, w.size(), w) == 0) {
      pos_ += w.size();
      return true;
    }
    return false;
  }

  std::unique_ptr<Json> Fail(const char* message) {
    error_ = message;
    return nullptr;
  }

  std::unique_ptr<Json> ReadValue() {
    SkipSpace();
    if (pos_ >= text_.size()) return Fail("값이 없습니다");
    auto value = std::make_unique<Json>();
    const char c = text_[pos_];
    if (c == '{') return ReadObject();
    if (c == '[') return ReadArray();
    if (c == '"') {
      value->kind = Json::Kind::kString;
      if (!ReadString(&value->text)) return nullptr;
      return value;
    }
    if (Consume("true")) {
      value->kind = Json::Kind::kBool;
      value->boolean = true;
      return value;
    }
    if (Consume("false")) {
      value->kind = Json::Kind::kBool;
      return value;
    }
    if (Consume("null")) return value;
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
      const size_t start = pos_;
      if (c == '-') ++pos_;
      while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E')) {
        return Fail("선언 파일의 숫자는 정수만 씁니다");
      }
      const std::string digits = text_.substr(start, pos_ - start);
      if (digits == "-" || digits.size() > 18) return Fail("정수 형식이 아닙니다");
      value->kind = Json::Kind::kInt;
      value->integer = std::stoll(digits);
      return value;
    }
    return Fail("알 수 없는 값입니다");
  }

  bool ReadString(std::string* out) {
    ++pos_;  // 여는 따옴표
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') return true;
      if (c == '\\') {
        if (pos_ >= text_.size()) break;
        const char e = text_[pos_++];
        switch (e) {
          case '"': out->push_back('"'); break;
          case '\\': out->push_back('\\'); break;
          case '/': out->push_back('/'); break;
          case 'n': out->push_back('\n'); break;
          case 't': out->push_back('\t'); break;
          default:
            error_ = "지원하지 않는 이스케이프입니다";
            return false;
        }
        continue;
      }
      out->push_back(c);
    }
    error_ = "문자열이 닫히지 않았습니다";
    return false;
  }

  std::unique_ptr<Json> ReadArray() {
    ++pos_;  // [
    auto array = std::make_unique<Json>();
    array->kind = Json::Kind::kArray;
    SkipSpace();
    if (Consume("]")) return array;
    for (;;) {
      auto value = ReadValue();
      if (value == nullptr) return nullptr;
      array->items.push_back(std::move(value));
      SkipSpace();
      if (Consume(",")) continue;
      if (Consume("]")) return array;
      return Fail("배열이 ,나 ]로 이어지지 않습니다");
    }
  }

  std::unique_ptr<Json> ReadObject() {
    ++pos_;  // {
    auto object = std::make_unique<Json>();
    object->kind = Json::Kind::kObject;
    SkipSpace();
    if (pos_ < text_.size() && text_[pos_] == '}') {
      ++pos_;
      return object;
    }
    for (;;) {
      SkipSpace();
      if (pos_ >= text_.size() || text_[pos_] != '"') return Fail("키는 문자열이어야 합니다");
      std::string key;
      if (!ReadString(&key)) return nullptr;
      SkipSpace();
      if (!Consume(":")) return Fail("키 뒤에 :가 없습니다");
      auto value = ReadValue();
      if (value == nullptr) return nullptr;
      if (object->members.count(key) != 0) return Fail("같은 키가 두 번 나옵니다");
      object->members[key] = std::move(value);
      SkipSpace();
      if (Consume(",")) continue;
      if (Consume("}")) return object;
      return Fail("객체가 ,나 }로 이어지지 않습니다");
    }
  }

  const std::string& text_;
  size_t pos_ = 0;
  std::string error_;
};

// 객체 하나에서 키를 꺼내며, 꺼내지 않은 키가 남으면 모르는 키로 봅니다.
class Fields {
 public:
  Fields(const Json& object, std::string path, std::string* error)
      : object_(object), path_(std::move(path)), error_(error) {}

  const Json* Get(const char* key, Json::Kind kind) {
    used_.push_back(key);
    const auto it = object_.members.find(key);
    if (it == object_.members.end()) {
      Fail(std::string("키가 없습니다: ") + path_ + key);
      return nullptr;
    }
    if (it->second->kind != kind) {
      Fail(std::string("형식이 다릅니다: ") + path_ + key);
      return nullptr;
    }
    return it->second.get();
  }
  void Int(const char* key, int64_t* out) {
    if (const Json* v = Get(key, Json::Kind::kInt)) *out = v->integer;
  }
  void Int32(const char* key, int32_t* out) {
    const Json* v = Get(key, Json::Kind::kInt);
    if (v == nullptr) return;
    // 그대로 자르면 4294969216이 1920이 되어 검증을 통과하므로 범위 밖 값은 거부합니다.
    if (v->integer < INT32_MIN || v->integer > INT32_MAX) {
      Fail(std::string("int32 범위를 벗어났습니다: ") + path_ + key);
      return;
    }
    *out = static_cast<int32_t>(v->integer);
  }
  void String(const char* key, std::string* out) {
    if (const Json* v = Get(key, Json::Kind::kString)) *out = v->text;
  }
  bool Finish() {
    for (const auto& [key, value] : object_.members) {
      bool known = false;
      for (const std::string& u : used_) known = known || u == key;
      if (!known) Fail("모르는 키입니다: " + path_ + key);
    }
    return error_->empty();
  }

 private:
  void Fail(const std::string& message) {
    if (error_->empty()) *error_ = message;
  }

  const Json& object_;
  std::string path_;
  std::string* error_;
  std::vector<std::string> used_;
};

}  // namespace

const char* ExtensionOf(VideoCodec codec) { return codec == VideoCodec::kFfv1 ? "mkv" : "avi"; }

FlowRule RuleOf(const CaptureSpec& spec) {
  return {spec.settle, spec.target_frames, spec.settings.mode};
}

std::vector<SpecProblem> ValidateCaptureSpec(const CaptureSpec& spec) {
  std::vector<SpecProblem> problems;
  if (spec.device.empty()) problems.push_back(SpecProblem::kNoDevice);
  if (spec.width <= 0 || spec.height <= 0 || spec.width > kMaxFrameSide || spec.height > kMaxFrameSide ||
      spec.width % 2 != 0) {
    problems.push_back(SpecProblem::kBadSize);
  }
  const SettingsSpec& s = spec.settings;
  if (s.sdk_frame_rate <= 0 || s.sdk_frame_rate > 255) problems.push_back(SpecProblem::kBadFrameRate);
  // 자동 맞춤은 노출 단계를 카메라에 보내지 않으므로 직접 지정일 때만 봅니다.
  if (s.mode == SettingMode::kManual && (s.exposure_step < kMinExposureStep || s.exposure_step > kMaxExposureStep)) {
    problems.push_back(SpecProblem::kBadExposure);
  }
  if (s.gain < 0 || s.gain > kMaxGain) problems.push_back(SpecProblem::kBadGain);
  if (s.white_balance_k < 1 || s.white_balance_k > kMaxWhiteBalanceK) problems.push_back(SpecProblem::kBadWhiteBalance);
  if (spec.target_frames <= 0) problems.push_back(SpecProblem::kBadTargetFrames);
  const timing::SettleRule& r = spec.settle;
  if (r.exposure_applied_frame < 1 || r.gain_applied_frame < 1 || r.white_balance_applied_frame < 1 ||
      r.black_level_settled_frame < 1) {
    problems.push_back(SpecProblem::kBadSettleRule);
  }
  if (spec.clip_level < 1 || spec.clip_level > 255) problems.push_back(SpecProblem::kBadClipLevel);
  if (spec.buffer_frames < 1) problems.push_back(SpecProblem::kBadBufferFrames);
  if (spec.thermal_record_period_us < 0) problems.push_back(SpecProblem::kBadThermalRecordPeriod);
  return problems;
}

std::optional<CaptureSpec> ParseCaptureSpec(const std::string& json, std::string* error) {
  error->clear();
  const std::unique_ptr<Json> root = Reader(json).ReadDocument(error);
  if (root == nullptr) return std::nullopt;
  if (root->kind != Json::Kind::kObject) {
    *error = "최상위 값은 객체여야 합니다";
    return std::nullopt;
  }
  CaptureSpec spec;
  Fields top(*root, "", error);
  top.String("device", &spec.device);
  top.Int32("width", &spec.width);
  top.Int32("height", &spec.height);
  top.Int("target_frames", &spec.target_frames);
  top.Int32("clip_level", &spec.clip_level);
  top.Int32("buffer_frames", &spec.buffer_frames);
  top.Int("thermal_record_period_us", &spec.thermal_record_period_us);
  std::string codec;
  top.String("codec", &codec);
  if (error->empty() && codec != "ffv1" && codec != "uncompressed") {
    *error = "codec는 ffv1 또는 uncompressed입니다";
  }
  spec.codec = codec == "uncompressed" ? VideoCodec::kUncompressed : VideoCodec::kFfv1;
  if (const Json* s = top.Get("settings", Json::Kind::kObject)) {
    Fields f(*s, "settings.", error);
    std::string mode;
    f.String("mode", &mode);
    if (error->empty() && mode != "auto" && mode != "manual") {
      *error = "settings.mode는 auto 또는 manual입니다";
    }
    spec.settings.mode = mode == "auto" ? SettingMode::kAuto : SettingMode::kManual;
    // 자동 맞춤에서는 노출 단계를 쓰지 않아 빠져도 됩니다.
    if (mode == "manual" || s->members.count("exposure_step") != 0) {
      f.Int32("exposure_step", &spec.settings.exposure_step);
    }
    f.Int32("gain", &spec.settings.gain);
    f.Int32("white_balance_k", &spec.settings.white_balance_k);
    f.Int32("sdk_frame_rate", &spec.settings.sdk_frame_rate);
    f.Finish();
  }
  if (const Json* s = top.Get("settle", Json::Kind::kObject)) {
    Fields f(*s, "settle.", error);
    f.Int32("exposure_applied_frame", &spec.settle.exposure_applied_frame);
    f.Int32("gain_applied_frame", &spec.settle.gain_applied_frame);
    f.Int32("white_balance_applied_frame", &spec.settle.white_balance_applied_frame);
    f.Int32("black_level_settled_frame", &spec.settle.black_level_settled_frame);
    f.Finish();
  }
  top.Finish();
  if (!error->empty()) return std::nullopt;
  return spec;
}

}  // namespace hwlab::flow
