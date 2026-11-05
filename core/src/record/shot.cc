#include "hwlab/record/shot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hwlab::record {

namespace {

bool IsDigits(const std::string& text, size_t count) {
  if (text.size() != count) return false;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

// 지금 배치의 마지막 단계는 날짜_시각(같은 초 재촬영이면 뒤에 _번호)이고, 이전 배치의 마지막 단계는 시각으로 시작합니다.
bool IsDateTimeStem(const std::string& stem) {
  return stem.size() >= 15 && IsDigits(stem.substr(0, 8), 8) && stem[8] == '_' && IsDigits(stem.substr(9, 6), 6) &&
         (stem.size() == 15 || stem[15] == '_');
}

bool IsValidDate(int32_t y, int32_t m, int32_t d) {
  if (y < 1900 || y > 2999 || m < 1 || m > 12 || d < 1) return false;
  static constexpr int32_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return d <= kDays[m - 1] + (m == 2 && leap ? 1 : 0);
}

// 이름은 폴더 이름이 되므로 Windows가 거부하는 문자를 막습니다. 한글 같은 UTF-8 바이트는 허용합니다.
bool IsSafeName(const std::string& name) {
  for (const char c : name) {
    const auto b = static_cast<unsigned char>(c);
    if (b < 0x20 || b == 0x7F) return false;
    switch (c) {
      case '<': case '>': case ':': case '"': case '/': case '\\': case '|': case '?': case '*':
        return false;
      default:
        break;
    }
  }
  return name.back() != '.' && name.back() != ' ' && name.front() != ' ';
}

bool IsValidPressure(const BloodPressure& pressure) {
  return pressure.diastolic_mmhg >= kMinBloodPressureMmhg && pressure.systolic_mmhg <= kMaxBloodPressureMmhg &&
         pressure.systolic_mmhg > pressure.diastolic_mmhg;
}

void AppendPressure(std::string& out, const BloodPressure& pressure) {
  out += "{\"systolic_mmhg\": " + std::to_string(pressure.systolic_mmhg) +
         ", \"diastolic_mmhg\": " + std::to_string(pressure.diastolic_mmhg) + "}";
}

// 발목 혈압 단계의 압력은 0.5 단위입니다. 로캘에 따라 소수점 글자가 바뀌는 printf 대신 정수로 씁니다.
std::string HalfMmhgText(double mmhg) {
  const long long halves = std::llround(mmhg * 2);
  return std::to_string(halves / 2) + (halves % 2 != 0 ? ".5" : "");
}

std::string Two(int32_t v) {
  char text[8];
  std::snprintf(text, sizeof(text), "%02d", v);
  return text;
}

const char* RoleName(FileRole role) {
  switch (role) {
    case FileRole::kLeftVideo: return "left_video";
    case FileRole::kLeftSidecar: return "left_sidecar";
    case FileRole::kRightVideo: return "right_video";
    case FileRole::kRightSidecar: return "right_sidecar";
    case FileRole::kThermalRaw: return "thermal_raw";
    case FileRole::kThermalSidecar: return "thermal_sidecar";
    case FileRole::kThermalPreview: return "thermal_preview";
    case FileRole::kManifest: return "manifest";
    case FileRole::kFullVideo: return "full_video";
    case FileRole::kFullSidecar: return "full_sidecar";
    case FileRole::kThermalLeftArray: return "thermal_left_array";
    case FileRole::kThermalRightArray: return "thermal_right_array";
  }
  return "";
}

const char* StreamName(StreamKind stream) {
  switch (stream) {
    case StreamKind::kRgb: return "rgb";
    case StreamKind::kThermal: return "thermal";
  }
  return "";
}

// 역할마다의 파일 이름 규칙입니다. 이름을 바꿀 때는 이 함수만 고칩니다. 영상은 확장자를 코덱에서 받습니다.
struct NameRule {
  bool stem_prefix = true;  // true면 이름이 stem(날짜_시각) + text, false면 text 그대로
  std::string text;
};

NameRule NameRuleOf(FileRole role, const std::string& video_extension) {
  switch (role) {
    case FileRole::kFullVideo: return {true, "_full." + video_extension};
    case FileRole::kFullSidecar: return {true, "_full.json"};
    case FileRole::kLeftVideo: return {true, "_left." + video_extension};
    case FileRole::kLeftSidecar: return {true, "_left.json"};
    case FileRole::kRightVideo: return {true, "_right." + video_extension};
    case FileRole::kRightSidecar: return {true, "_right.json"};
    case FileRole::kThermalRaw: return {true, "_thermal.raw16"};
    case FileRole::kThermalSidecar: return {true, "_thermal.json"};
    case FileRole::kThermalPreview: return {true, "_thermal.avi"};
    case FileRole::kManifest: return {true, "_shot.json"};
    // 관류 분석 코드가 이 두 이름을 고정해 읽습니다.
    case FileRole::kThermalLeftArray: return {false, "thermal_left.npy"};
    case FileRole::kThermalRightArray: return {false, "thermal_right.npy"};
  }
  return {};
}

const char* FootName(FootSide foot) { return foot == FootSide::kLeft ? "left" : "right"; }

const char* LightSourceName(LightSource source) {
  switch (source) {
    case LightSource::kUnknown: return "unknown";
    case LightSource::kExternal: return "external";
    case LightSource::kApp: return "app";
  }
  return "";
}

const char* AbsenceName(ThermalAbsence absence) {
  switch (absence) {
    case ThermalAbsence::kNone: return "unknown";
    case ThermalAbsence::kDisabled: return "disabled";
    case ThermalAbsence::kNoDevice: return "no_device";
    case ThermalAbsence::kNotReceiving: return "not_receiving";
    case ThermalAbsence::kPreparing: return "preparing";
  }
  return "";
}

void AppendQuoted(std::string& out, const std::string& text) {
  out.push_back('"');
  for (const char c : text) {
    // 장치 이름은 OS가 준 값이라 제어 문자가 섞여도 JSON이 깨지지 않게 합니다.
    if (static_cast<unsigned char>(c) < 0x20) {
      char escaped[8];
      std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
      out += escaped;
      continue;
    }
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
}

}  // namespace

bool UsesStemPrefix(FileRole role) { return NameRuleOf(role, "").stem_prefix; }

bool IsAnalysisInput(FileRole role) {
  return role == FileRole::kLeftVideo || role == FileRole::kRightVideo || role == FileRole::kThermalLeftArray ||
         role == FileRole::kThermalRightArray;
}

std::string ShotLayout::NameOf(FileRole role) const {
  for (const ShotFile& f : files) {
    if (f.role == role) return f.name;
  }
  return {};
}

std::optional<double> AnkleLevelMmhg(std::string_view level, const BloodPressure& ankle) {
  if (level == "0") return 0.0;
  if (level == "V") return ankle.diastolic_mmhg - 50.0;
  if (level == "D") return static_cast<double>(ankle.diastolic_mmhg);
  if (level == "M") return (ankle.systolic_mmhg + ankle.diastolic_mmhg) / 2.0;
  if (level == "S") return static_cast<double>(ankle.systolic_mmhg);
  return std::nullopt;
}

std::vector<ShotProblem> ValidateShot(const ShotInfo& info) {
  std::vector<ShotProblem> problems;
  if (info.patient_name.empty()) {
    problems.push_back(ShotProblem::kEmptyName);
  } else if (!IsSafeName(info.patient_name)) {
    problems.push_back(ShotProblem::kBadNameCharacter);
  }
  if (!IsDigits(info.birth_date, 8) ||
      !IsValidDate(std::stoi(info.birth_date.substr(0, 4)), std::stoi(info.birth_date.substr(4, 2)),
                   std::stoi(info.birth_date.substr(6, 2)))) {
    problems.push_back(ShotProblem::kBadBirthDate);
  }
  if (!IsValidPressure(info.arm_pressure)) problems.push_back(ShotProblem::kBadArmPressure);
  if (!IsValidPressure(info.ankle_pressure)) problems.push_back(ShotProblem::kBadAnklePressure);
  if (const auto level_mmhg = AnkleLevelMmhg(info.ankle_level, info.ankle_pressure); !level_mmhg) {
    problems.push_back(ShotProblem::kUnknownAnkleLevel);
  } else if (IsValidPressure(info.ankle_pressure) && *level_mmhg < 0) {
    problems.push_back(ShotProblem::kNegativeAnkleLevel);
  }
  if (!IsValidDate(info.year, info.month, info.day) || info.hour < 0 || info.hour > 23 || info.minute < 0 ||
      info.minute > 59 || info.second < 0 || info.second > 59) {
    problems.push_back(ShotProblem::kBadTime);
  }
  return problems;
}

std::optional<ShotLayout> LayoutOf(const ShotInfo& info, const std::string& video_extension,
                                   const std::vector<FileRole>& roles) {
  if (!ValidateShot(info).empty() || video_extension.empty()) return std::nullopt;
  ShotLayout layout;
  layout.stem = std::to_string(info.year) + Two(info.month) + Two(info.day) + "_" + Two(info.hour) + Two(info.minute) +
                Two(info.second);
  // NAS 서버에 올리는 구조와 같게 둡니다. 다르면 촬영마다 사람이 폴더를 옮겨야 합니다(표 11).
  layout.folder = info.patient_name + "_" + info.birth_date + "/" + info.ankle_level + "/" + layout.stem;
  // 파일 이름의 left, right는 환자 기준입니다. 영문으로 쓰는 이유는 압축이나 메일에서 한글 파일 이름이 깨지는 환경이 있기 때문입니다.
  bool has_manifest = false;
  for (const FileRole role : roles) {
    if (!layout.NameOf(role).empty()) continue;
    has_manifest = has_manifest || role == FileRole::kManifest;
    const NameRule rule = NameRuleOf(role, video_extension);
    layout.files.push_back({role, rule.stem_prefix ? layout.stem + rule.text : rule.text});
  }
  if (!has_manifest) layout.files.push_back({FileRole::kManifest, layout.stem + NameRuleOf(FileRole::kManifest, "").text});
  return layout;
}

ShotLayout NumberLayout(const ShotLayout& layout, int32_t number) {
  if (number < 2) return layout;
  ShotLayout numbered = layout;
  numbered.stem = layout.stem + "_" + std::to_string(number);
  numbered.folder = layout.folder.substr(0, layout.folder.size() - layout.stem.size()) + numbered.stem;
  for (ShotFile& f : numbered.files) {
    if (UsesStemPrefix(f.role)) f.name = numbered.stem + f.name.substr(layout.stem.size());
  }
  return numbered;
}

std::string BuildManifestJson(const ShotInfo& info, const ShotLayout& layout, const std::vector<ShotDevice>& devices) {
  // 2: pixel_left_foot, analysis_inputs 추가. 3: lighting, thermal_recorded, thermal_absent_reason, thermal_skip_confirmed 추가
  // 4: app(version, commit), spec(file, sha256) 추가. 5: attempt, foot_warmth 추가. 6: blood_pressure(arm, ankle, ankle_level) 추가
  // 7: criterion(검사 기준) 뺌. 커프 압력은 blood_pressure.ankle_level이 남깁니다
  std::string out = "{\n  \"schema_version\": 7,\n  \"patient_name\": ";
  AppendQuoted(out, info.patient_name);
  out += ",\n  \"birth_date\": ";
  AppendQuoted(out, info.birth_date);
  out += ",\n  \"blood_pressure\": {\"arm\": ";
  AppendPressure(out, info.arm_pressure);
  out += ", \"ankle\": ";
  AppendPressure(out, info.ankle_pressure);
  out += ", \"ankle_level\": {\"name\": ";
  AppendQuoted(out, info.ankle_level);
  const auto level_mmhg = AnkleLevelMmhg(info.ankle_level, info.ankle_pressure);
  out += ", \"pressure_mmhg\": " + (level_mmhg ? HalfMmhgText(*level_mmhg) : std::string("null")) + "}}";
  char time[32];
  std::snprintf(time, sizeof(time), "%04d-%02d-%02dT%02d:%02d:%02d", info.year, info.month, info.day, info.hour,
                info.minute, info.second);
  out += ",\n  \"shot_time\": ";
  AppendQuoted(out, time);
  out += ",\n  \"attempt\": ";
  out += info.attempt > 0 ? std::to_string(info.attempt) : "null";
  // 빈 문자열은 모르는 값이라 null로 씁니다.
  const auto quoted_or_null = [&](const std::string& text) {
    if (text.empty()) {
      out += "null";
    } else {
      AppendQuoted(out, text);
    }
  };
  out += ",\n  \"app\": {\"version\": ";
  quoted_or_null(info.app_version);
  out += ", \"commit\": ";
  quoted_or_null(info.app_commit);
  out += "},\n  \"spec\": {\"file\": ";
  quoted_or_null(info.spec_file);
  out += ", \"sha256\": ";
  quoted_or_null(info.spec_sha256);
  out += "}";
  out += ",\n  \"pixel_left_foot\": ";
  AppendQuoted(out, FootName(info.pixel_left_foot));
  out += ",\n  \"lighting\": {\"source\": ";
  AppendQuoted(out, LightSourceName(info.lighting.source));
  out += ", \"product\": ";
  AppendQuoted(out, info.lighting.product);
  out += ", \"brightness_percent\": ";
  out += info.lighting.brightness_percent ? std::to_string(*info.lighting.brightness_percent) : "null";
  // 열화상이 없는 촬영을 장치 목록만으로 알아보기 어렵기 때문에 녹화 여부와 이유를 따로 씁니다.
  const bool thermal_recorded = !layout.NameOf(FileRole::kThermalRaw).empty();
  out += "},\n  \"thermal_recorded\": ";
  out += thermal_recorded ? "true" : "false";
  if (thermal_recorded && info.foot_warmth) {
    const FootWarmthRecord& w = *info.foot_warmth;
    out += ",\n  \"foot_warmth\": {\"left\": " + std::to_string(w.left) + ", \"right\": " + std::to_string(w.right) +
           ", \"min_warm\": " + std::to_string(w.min_warm) + ", \"cold_confirmed\": " + (w.cold_confirmed ? "true" : "false") + "}";
  }
  if (!thermal_recorded) {
    out += ",\n  \"thermal_absent_reason\": ";
    AppendQuoted(out, AbsenceName(info.thermal_absence));
    out += ",\n  \"thermal_skip_confirmed\": ";
    out += info.thermal_skip_confirmed ? "true" : "false";
  }
  out += ",\n  \"devices\": [";
  for (size_t i = 0; i < devices.size(); ++i) {
    out += i == 0 ? "\n    {\"stream\": " : ",\n    {\"stream\": ";
    AppendQuoted(out, StreamName(devices[i].stream));
    out += ", \"name\": ";
    AppendQuoted(out, devices[i].name);
    out += "}";
  }
  out += devices.empty() ? "]" : "\n  ]";
  out += ",\n  \"folder\": ";
  AppendQuoted(out, layout.folder);
  out += ",\n  \"files\": {";
  for (size_t i = 0; i < layout.files.size(); ++i) {
    out += i == 0 ? "\n    " : ",\n    ";
    AppendQuoted(out, RoleName(layout.files[i].role));
    out += ": ";
    AppendQuoted(out, layout.files[i].name);
  }
  out += "\n  },\n  \"analysis_inputs\": [";
  bool first = true;
  for (const ShotFile& f : layout.files) {
    if (!IsAnalysisInput(f.role)) continue;
    out += first ? "" : ", ";
    first = false;
    AppendQuoted(out, RoleName(f.role));
  }
  out += "]\n}\n";
  return out;
}

std::optional<ShotFolderPath> SplitShotFolderPath(const std::string& path) {
  std::string trimmed = path;
  while (!trimmed.empty() && (trimmed.back() == '/' || trimmed.back() == '\\')) trimmed.pop_back();
  std::vector<std::string> parts;
  size_t levels = 4;
  size_t cut = trimmed.size();
  while (parts.size() < levels) {
    const size_t sep = cut == 0 ? std::string::npos : trimmed.find_last_of("/\\", cut - 1);
    const size_t begin = sep == std::string::npos ? 0 : sep + 1;
    if (begin >= cut) return std::nullopt;  // 빈 단계("a//b")이거나 단계가 모자람
    parts.push_back(trimmed.substr(begin, cut - begin));
    if (parts.size() == 1 && IsDateTimeStem(parts[0])) levels = 3;
    if (sep == std::string::npos) {
      cut = 0;
      break;
    }
    cut = sep;
  }
  if (parts.size() < levels) return std::nullopt;
  ShotFolderPath out;
  out.root = trimmed.substr(0, cut);
  out.layout.stem = parts[0];
  out.layout.folder = parts[levels - 1];
  for (size_t i = levels - 1; i > 0; --i) out.layout.folder += "/" + parts[i - 1];
  return out;
}

std::string CollectedResultPrefix(const ShotLayout& layout, int32_t reanalysis) {
  std::string prefix = layout.folder;
  std::replace(prefix.begin(), prefix.end(), '/', '_');
  if (!prefix.empty() && reanalysis >= 2) prefix += "_reanalysis" + std::to_string(reanalysis);
  return prefix;
}

}  // namespace hwlab::record
