#include "hwlab/platform/shot_review.h"

#include <cstdint>
#include <cstdio>
#include <locale>
#include <sstream>

#include "hwlab/platform/shot_folder.h"
#include "json_reader.h"

namespace hwlab::platform {

namespace {

// 부호와 숫자만 있는 정수를 읽습니다. 공통 시각은 마이크로초 정수이므로 소수와 지수 표기는 받지 않습니다.
std::optional<int64_t> ParseInt64(const std::string& text) {
  if (text.empty()) return std::nullopt;
  size_t i = text[0] == '-' ? 1 : 0;
  if (i == text.size()) return std::nullopt;
  int64_t value = 0;
  for (; i < text.size(); ++i) {
    const char c = text[i];
    if (c < '0' || c > '9' || value > (INT64_MAX - (c - '0')) / 10) return std::nullopt;
    value = value * 10 + (c - '0');
  }
  return text[0] == '-' ? -value : value;
}

std::optional<int64_t> IntAt(const std::string& json, const std::vector<std::string>& path) {
  const auto scalar = JsonReader(json).FindScalar(path);
  if (!scalar || scalar->kind != JsonKind::kNumber) return std::nullopt;
  return ParseInt64(scalar->text);
}

std::optional<int32_t> Int32At(const std::string& json, const std::vector<std::string>& path) {
  const auto v = IntAt(json, path);
  if (!v || *v < INT32_MIN || *v > INT32_MAX) return std::nullopt;
  return static_cast<int32_t>(*v);
}

// 앱이 C 로캘을 바꿔도 소수점을 '.'로 읽도록 고전 로캘로 읽습니다.
std::optional<double> DoubleAt(const std::string& json, const std::vector<std::string>& path) {
  const auto scalar = JsonReader(json).FindScalar(path);
  if (!scalar || scalar->kind != JsonKind::kNumber) return std::nullopt;
  std::istringstream in(scalar->text);
  in.imbue(std::locale::classic());
  double value = 0.0;
  in >> value;
  if (in.fail() || !in.eof()) return std::nullopt;
  return value;
}

std::optional<bool> BoolAt(const std::string& json, const std::vector<std::string>& path) {
  const auto scalar = JsonReader(json).FindScalar(path);
  if (!scalar) return std::nullopt;
  if (scalar->kind == JsonKind::kTrue) return true;
  if (scalar->kind == JsonKind::kFalse) return false;
  return std::nullopt;
}

}  // namespace

std::optional<ShotManifest> ReadShotManifest(const std::string& manifest_json, std::string* error) {
  ShotManifest files;
  const auto take = [&](const char* key, std::string* out) { JsonReader(manifest_json).FindString({"files", key}, out); };
  take("left_video", &files.left_video);
  take("left_sidecar", &files.left_sidecar);
  take("right_video", &files.right_video);
  take("right_sidecar", &files.right_sidecar);
  take("thermal_raw", &files.thermal_raw);
  take("thermal_sidecar", &files.thermal_sidecar);
  std::string foot;
  JsonReader(manifest_json).FindString({"pixel_left_foot"}, &foot);
  if (foot == "left") files.pixel_left_foot = record::FootSide::kLeft;
  if (foot == "right") files.pixel_left_foot = record::FootSide::kRight;
  if (const auto criterion = Int32At(manifest_json, {"criterion"}); criterion && *criterion >= 0) {
    files.criterion = criterion;
  }
  JsonReader(manifest_json).FindString({"blood_pressure", "ankle_level", "name"}, &files.ankle_level);
  JsonReader(manifest_json).FindString({"lighting", "source"}, &files.light_source);
  JsonReader(manifest_json).FindString({"lighting", "product"}, &files.light_product);
  if (files.left_video.empty() || files.right_video.empty()) {
    if (error != nullptr) *error = "매니페스트에 files.left_video 또는 files.right_video가 없거나 JSON을 읽지 못했습니다";
    return std::nullopt;
  }
  return files;
}

std::optional<SidecarFrames> ReadSidecarFrames(const std::string& sidecar_json, std::string* error) {
  const auto fail = [&](const std::string& why) -> std::optional<SidecarFrames> {
    if (error != nullptr) *error = why;
    return std::nullopt;
  };
  SidecarFrames frames;
  const auto width = IntAt(sidecar_json, {"format", "width"});
  const auto height = IntAt(sidecar_json, {"format", "height"});
  if (!width || !height || *width <= 0 || *height <= 0 || *width > INT32_MAX || *height > INT32_MAX) {
    return fail("사이드카에 format.width 또는 format.height가 없거나 JSON을 읽지 못했습니다");
  }
  frames.width = static_cast<int32_t>(*width);
  frames.height = static_cast<int32_t>(*height);
  frames.frame_rate = Int32At(sidecar_json, {"format", "frame_rate"});
  RecordedCameraSettings& camera = frames.camera;
  JsonReader(sidecar_json).FindString({"settings", "mode"}, &camera.mode);
  camera.exposure_step = Int32At(sidecar_json, {"settings", "exposure_step"});
  camera.exposure_ms = DoubleAt(sidecar_json, {"settings", "exposure_ms"});
  camera.gain = Int32At(sidecar_json, {"settings", "gain"});
  camera.white_balance_auto = BoolAt(sidecar_json, {"settings", "white_balance_auto"});
  camera.white_balance_k = Int32At(sidecar_json, {"settings", "white_balance_k"});
  camera.locked = BoolAt(sidecar_json, {"settings", "locked"});
  const auto times = JsonReader(sidecar_json).FindArray({"per_frame", "common_time_us"});
  if (!times) return fail("사이드카에 per_frame.common_time_us가 없습니다");
  frames.common_time.reserve(times->size());
  for (const auto& t : *times) {
    const auto us = t.kind == JsonKind::kNumber ? ParseInt64(t.text) : std::nullopt;
    if (!us) return fail("사이드카 per_frame.common_time_us에 정수가 아닌 값이 있습니다");
    frames.common_time.push_back(Timestamp{*us});
  }
  const auto low = IntAt(sidecar_json, {"display_range", "low"});
  const auto high = IntAt(sidecar_json, {"display_range", "high"});
  if (low && high && *low >= 0 && *high >= *low && *high <= 65535) {
    frames.display_range = pixel::Y16Range{static_cast<uint16_t>(*low), static_cast<uint16_t>(*high)};
  }
  return frames;
}

bool ReadRaw16Frame(const std::string& path, int32_t width, int32_t height, int64_t index, std::vector<uint16_t>* out) {
  if (width <= 0 || height <= 0 || index < 0) return false;
  const int64_t count = static_cast<int64_t>(width) * height;
  std::FILE* file = OpenFileUtf8(path, "rb");
  if (file == nullptr) return false;
  std::vector<uint8_t> bytes(static_cast<size_t>(count * 2));
  bool ok = false;
#ifdef _WIN32
  ok = _fseeki64(file, index * count * 2, SEEK_SET) == 0;
#else
  ok = fseeko(file, static_cast<off_t>(index * count * 2), SEEK_SET) == 0;
#endif
  ok = ok && std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
  std::fclose(file);
  if (!ok) return false;
  out->resize(static_cast<size_t>(count));
  for (int64_t i = 0; i < count; ++i) {
    (*out)[static_cast<size_t>(i)] =
        static_cast<uint16_t>(bytes[static_cast<size_t>(2 * i)] | (bytes[static_cast<size_t>(2 * i + 1)] << 8));
  }
  return true;
}

}  // namespace hwlab::platform
