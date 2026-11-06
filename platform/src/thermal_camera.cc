#include "hwlab/platform/thermal_camera.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <locale>
#include <map>
#include <sstream>
#include <utility>

#include "json_reader.h"

namespace hwlab::platform {

namespace {

constexpr std::array<std::pair<timing::WarmupPhase, const char*>, 4> kWarmupNames{{
    {timing::WarmupPhase::kMeasuring, "measuring"},
    {timing::WarmupPhase::kWarming, "warming"},
    {timing::WarmupPhase::kSettling, "settling"},
    {timing::WarmupPhase::kReady, "ready"},
}};

std::filesystem::path ConditionsPath(const std::filesystem::path& cal_path) {
  std::filesystem::path path = cal_path;
  path += ".json";
  return path;
}

std::optional<double> ParseDouble(const std::string& text) {
  std::istringstream in(text);
  in.imbue(std::locale::classic());
  double value = 0.0;
  in >> value;
  if (in.fail() || !in.eof()) return std::nullopt;
  return value;
}

}  // namespace

bool WriteLensShadingConditions(const std::filesystem::path& cal_path, int64_t file_unix_s,
                                const timing::LensShadingConditions& made) {
  const char* warmup = "";
  for (const auto& [phase, name] : kWarmupNames) {
    if (phase == made.warmup) warmup = name;
  }
  std::ostringstream text;
  text.imbue(std::locale::classic());
  text << std::fixed << std::setprecision(3) << "{\n  \"file_unix_s\": " << file_unix_s << ",\n  \"shutter_c\": "
       << made.shutter_c << ",\n  \"fpa\": " << made.fpa << ",\n  \"warmup\": \"" << warmup << "\"\n}\n";
  std::ofstream file(ConditionsPath(cal_path), std::ios::binary | std::ios::trunc);
  file << text.str();
  file.close();
  return static_cast<bool>(file);
}

std::optional<timing::LensShadingConditions> ReadLensShadingConditions(const std::filesystem::path& cal_path,
                                                                       int64_t file_unix_s) {
  std::ifstream file(ConditionsPath(cal_path), std::ios::binary);
  if (!file) return std::nullopt;
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const std::optional<std::map<std::string, std::string>> members = JsonReader(text).Read();
  if (!members || !members->contains("file_unix_s") || !members->contains("shutter_c") || !members->contains("fpa") ||
      !members->contains("warmup")) {
    return std::nullopt;
  }
  const std::optional<double> saved_s = ParseDouble(members->at("file_unix_s"));
  const std::optional<double> shutter_c = ParseDouble(members->at("shutter_c"));
  const std::optional<double> fpa = ParseDouble(members->at("fpa"));
  if (!saved_s || !shutter_c || !fpa || static_cast<int64_t>(*saved_s) != file_unix_s) return std::nullopt;
  for (const auto& [phase, name] : kWarmupNames) {
    if (members->at("warmup") == name) return timing::LensShadingConditions{*shutter_c, *fpa, phase};
  }
  return std::nullopt;
}

ThermalConnection JudgeThermalConnection(bool open, Timestamp opened_at, std::optional<Timestamp> last_frame,
                                         Timestamp now, const ThermalLossRule& rule) {
  if (!open) return ThermalConnection::kClosed;
  if (!last_frame) {
    // 연 직후의 셔터 교정 동안 프레임이 없으므로 받던 중의 끊김보다 길게 기다립니다.
    return IntervalUs(opened_at, now) < rule.first_frame_us ? ThermalConnection::kWaiting : ThermalConnection::kLost;
  }
  return IntervalUs(*last_frame, now) < rule.stall_us ? ThermalConnection::kReceiving : ThermalConnection::kLost;
}

}  // namespace hwlab::platform

// SDK 구현(windows/thermal_camera_windows.cc)을 빌드하지 않을 때는 실제 열화상 카메라가 없음을 nullptr로 알립니다.
#ifndef HWLAB_HAS_THERMAL_SDK
namespace hwlab::platform {

std::unique_ptr<ThermalCamera> MakeDeviceThermalCamera() { return nullptr; }
std::unique_ptr<ThermalCamera> MakeDeviceThermalCamera(const pixel::CalibrationRule&) { return nullptr; }

}  // namespace hwlab::platform
#endif
