#include "hwlab/platform/shot_folder.h"

#include <algorithm>
#include <cmath>
#include <system_error>

#ifndef _WIN32
#include <sys/statvfs.h>
#endif

#include "hwlab/platform/thermal_writer.h"

namespace hwlab::platform {

namespace {

std::optional<record::SidecarSummary> ReadSummaryFile(const std::string& path, std::string* error) {
  std::FILE* file = path.empty() ? nullptr : OpenFileUtf8(path, "rb");
  if (file == nullptr) {
    *error = "사이드카를 열지 못함: " + path;
    return std::nullopt;
  }
  std::string json;
  char buffer[65536];
  for (size_t n; (n = std::fread(buffer, 1, sizeof(buffer), file)) > 0;) json.append(buffer, n);
  std::fclose(file);
  return record::ReadSidecarSummary(json, error);
}

std::string Utf8Of(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

}  // namespace

std::filesystem::path PathFromUtf8(const std::string& utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::FILE* OpenFileUtf8(const std::string& utf8_path, const char* mode) {
#ifdef _WIN32
  const std::wstring wide_mode(mode, mode + std::char_traits<char>::length(mode));
  return _wfopen(PathFromUtf8(utf8_path).c_str(), wide_mode.c_str());
#else
  return std::fopen(utf8_path.c_str(), mode);
#endif
}

std::string PathOfFile(const std::string& root, const record::ShotLayout& layout, record::FileRole role) {
  const std::string name = layout.NameOf(role);
  if (name.empty()) return {};
  return Utf8Of((PathFromUtf8(root) / PathFromUtf8(layout.folder) / PathFromUtf8(name)).make_preferred());
}

bool CreateShotFolder(const std::string& root, const record::ShotLayout& layout, std::string* error) {
  const std::filesystem::path folder = (PathFromUtf8(root) / PathFromUtf8(layout.folder)).make_preferred();
  std::error_code ec;
  std::filesystem::create_directories(folder.parent_path(), ec);
  if (ec) {
    *error = "상위 폴더를 만들지 못함: " + Utf8Of(folder.parent_path()) + " (" + ec.message() + ")";
    return false;
  }
  // create_directory는 이미 있으면 false를 돌려주므로, 있는지 보고 만드는 사이에 다른 촬영이 끼어들어도 덮어쓰지 않습니다.
  if (!std::filesystem::create_directory(folder, ec)) {
    *error = ec ? "촬영 폴더를 만들지 못함: " + Utf8Of(folder) + " (" + ec.message() + ")"
                : "촬영 폴더가 이미 있음: " + Utf8Of(folder);
    return false;
  }
  // 촬영 폴더는 맨 위 폴더의 두 단계 아래라 OS가 맨 위 폴더의 수정 시각을 바꾸지 않습니다. 맞추지 못해도 촬영은 이어 갑니다.
  const std::string top = layout.folder.substr(0, layout.folder.find('/'));
  std::filesystem::last_write_time(PathFromUtf8(root) / PathFromUtf8(top), std::filesystem::file_time_type::clock::now(),
                                   ec);
  return true;
}

std::optional<record::ShotLayout> CreateFreshShotFolder(const std::string& root, const record::ShotLayout& layout,
                                                        int32_t max_number, std::string* error) {
  for (int32_t number = 1; number <= max_number; ++number) {
    const record::ShotLayout candidate = record::NumberLayout(layout, number);
    const std::filesystem::path folder = PathFromUtf8(root) / PathFromUtf8(candidate.folder);
    std::error_code ec;
    if (std::filesystem::exists(folder, ec)) continue;
    if (CreateShotFolder(root, candidate, error)) return candidate;
    // 확인과 생성 사이에 다른 촬영이 같은 폴더를 만든 경우만 다음 번호로 넘어갑니다.
    if (!std::filesystem::exists(folder, ec)) return std::nullopt;
  }
  *error = "번호 " + std::to_string(max_number) + "까지 촬영 폴더가 모두 있음: " + layout.folder;
  return std::nullopt;
}

bool WriteManifest(const std::string& root, const record::ShotLayout& layout, const std::string& json,
                   std::string* error) {
  const std::string path = PathOfFile(root, layout, record::FileRole::kManifest);
  std::error_code ec;
  if (path.empty() || std::filesystem::exists(PathFromUtf8(path), ec)) {
    *error = "매니페스트를 덮어쓰지 않음: " + path;
    return false;
  }
  std::FILE* file = OpenFileUtf8(path, "wb");
  if (file == nullptr) {
    *error = "매니페스트를 열지 못함: " + path;
    return false;
  }
  bool saved = std::fwrite(json.data(), 1, json.size(), file) == json.size();
  saved = std::fclose(file) == 0 && saved;
  if (!saved) *error = "매니페스트를 쓰지 못함: " + path;
  return saved;
}

std::optional<record::ShotLayout> PlanShotLayout(const record::ShotInfo& info, flow::VideoCodec codec, SavedVideos videos,
                                                 const std::vector<record::ShotDevice>& devices) {
  std::vector<record::FileRole> roles;
  if (videos != SavedVideos::kFeet) roles.insert(roles.end(), {record::FileRole::kFullVideo, record::FileRole::kFullSidecar});
  if (videos != SavedVideos::kFull) {
    roles.insert(roles.end(), {record::FileRole::kLeftVideo, record::FileRole::kLeftSidecar,
                               record::FileRole::kRightVideo, record::FileRole::kRightSidecar});
  }
  bool has_thermal = false;
  for (const record::ShotDevice& device : devices) has_thermal = has_thermal || device.stream == StreamKind::kThermal;
  if (has_thermal) {
    roles.insert(roles.end(), {record::FileRole::kThermalRaw, record::FileRole::kThermalSidecar,
                               record::FileRole::kThermalPreview, record::FileRole::kThermalLeftArray,
                               record::FileRole::kThermalRightArray});
  }
  return record::LayoutOf(info, flow::ExtensionOf(codec), roles);
}

std::optional<PreparedShot> PrepareShot(const std::string& root, const record::ShotInfo& info, flow::VideoCodec codec,
                                        SavedVideos videos, const std::vector<record::ShotDevice>& devices,
                                        int32_t max_number, std::string* error) {
  const auto layout = PlanShotLayout(info, codec, videos, devices);
  if (!layout) {
    *error = "촬영 정보가 검증을 통과하지 않음";
    return std::nullopt;
  }
  const auto fresh = CreateFreshShotFolder(root, *layout, max_number, error);
  if (!fresh) return std::nullopt;

  PreparedShot shot;
  shot.layout = *fresh;
  shot.pixel_left_foot = info.pixel_left_foot;
  shot.folder = Utf8Of((PathFromUtf8(root) / PathFromUtf8(fresh->folder)).make_preferred());
  shot.manifest = PathOfFile(root, *fresh, record::FileRole::kManifest);
  shot.video = PathOfFile(root, *fresh, record::FileRole::kFullVideo);
  shot.sidecar = PathOfFile(root, *fresh, record::FileRole::kFullSidecar);
  shot.thermal_raw = PathOfFile(root, *fresh, record::FileRole::kThermalRaw);
  shot.thermal_sidecar = PathOfFile(root, *fresh, record::FileRole::kThermalSidecar);
  shot.thermal_preview = PathOfFile(root, *fresh, record::FileRole::kThermalPreview);
  shot.left_video = PathOfFile(root, *fresh, record::FileRole::kLeftVideo);
  shot.left_sidecar = PathOfFile(root, *fresh, record::FileRole::kLeftSidecar);
  shot.right_video = PathOfFile(root, *fresh, record::FileRole::kRightVideo);
  shot.right_sidecar = PathOfFile(root, *fresh, record::FileRole::kRightSidecar);
  shot.thermal_left_array = PathOfFile(root, *fresh, record::FileRole::kThermalLeftArray);
  shot.thermal_right_array = PathOfFile(root, *fresh, record::FileRole::kThermalRightArray);
  if (!WriteManifest(root, *fresh, record::BuildManifestJson(info, *fresh, devices), error)) {
    // 방금 만든 폴더라 매니페스트는 이번 호출이 쓰다 만 파일뿐입니다. 매니페스트 없는 촬영 폴더를 남기지 않습니다.
    std::error_code ec;
    std::filesystem::remove(PathFromUtf8(shot.manifest), ec);
    std::filesystem::remove(PathFromUtf8(shot.folder), ec);
    return std::nullopt;
  }
  return shot;
}

bool DiscardUnrecordedShot(const PreparedShot& shot) {
  if (shot.folder.empty()) return false;
  std::error_code ec;
  for (const std::string* video : {&shot.video, &shot.left_video, &shot.right_video}) {
    if (!video->empty() && std::filesystem::exists(PathFromUtf8(*video), ec)) return false;
  }
  const std::filesystem::path folder = PathFromUtf8(shot.folder);
  const std::filesystem::path manifest = PathFromUtf8(shot.manifest);
  for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->path().filename() != manifest.filename()) return false;
  }
  if (ec) return false;
  std::filesystem::remove(manifest, ec);
  if (ec) return false;
  // remove는 빈 폴더만 지우므로, 확인과 삭제 사이에 생긴 파일이 있으면 폴더가 남습니다.
  return std::filesystem::remove(folder, ec) && !ec;
}

bool WriteThermalArrays(const PreparedShot& shot,
                        const std::optional<std::pair<pixel::Rect, pixel::Rect>>& pixel_regions, std::string* error) {
  if (shot.thermal_raw.empty() || shot.thermal_left_array.empty() || shot.thermal_right_array.empty()) {
    *error = "열화상 장치 없이 준비한 촬영";
    return false;
  }
  const auto thermal = ReadSummaryFile(shot.thermal_sidecar, error);
  if (!thermal) return false;
  if (thermal->stream != StreamKind::kThermal) {
    *error = "열화상 사이드카가 아님: " + shot.thermal_sidecar;
    return false;
  }
  const auto [left, right] =
      record::ThermalFootRegions(thermal->frame_width, thermal->frame_height, pixel_regions, shot.pixel_left_foot);
  for (const pixel::Rect& rect : {left, right}) {
    if (rect.height <= rect.width) {
      *error = "열화상 발 영역이 세로로 길지 않음: " + std::to_string(rect.width) + "x" + std::to_string(rect.height);
      return false;
    }
  }
  const std::pair<pixel::Rect, const std::string*> outputs[] = {{left, &shot.thermal_left_array},
                                                                {right, &shot.thermal_right_array}};
  for (const auto& [rect, path] : outputs) {
    const auto frames = WriteNpy(shot.thermal_raw, thermal->frame_width, thermal->frame_height, rect, *path, error);
    // 사이드카의 장 수와 다르면 원시값 파일이 사이드카와 다른 녹화의 것이거나 잘린 파일입니다.
    if (frames && *frames != thermal->written_frames) {
      *error = "원시값 장 수 " + std::to_string(*frames) + "가 사이드카의 " + std::to_string(thermal->written_frames) +
               "와 다름";
    }
    if (!frames || *frames != thermal->written_frames) {
      std::error_code ec;
      std::filesystem::remove(PathFromUtf8(shot.thermal_left_array), ec);
      std::filesystem::remove(PathFromUtf8(shot.thermal_right_array), ec);
      return false;
    }
  }
  return true;
}

record::AnalysisFiles ReadAnalysisFiles(const PreparedShot& shot) {
  record::AnalysisFiles files;
  std::error_code ec;
  for (const record::ShotFile& f : shot.layout.files) {
    if (std::filesystem::is_regular_file(PathFromUtf8(shot.folder) / PathFromUtf8(f.name), ec)) files.present.push_back(f.role);
  }
  std::string ignored;
  const auto read_if_present = [&](const std::string& path) -> std::optional<record::SidecarSummary> {
    if (path.empty() || !std::filesystem::exists(PathFromUtf8(path), ec)) return std::nullopt;
    return ReadSummaryFile(path, &ignored);
  };
  files.left = read_if_present(shot.left_sidecar);
  files.right = read_if_present(shot.right_sidecar);
  files.thermal = read_if_present(shot.thermal_sidecar);
  return files;
}

std::optional<int32_t> MaxPathLength() {
#ifdef _WIN32
  return kWindowsMaxPathLength;
#else
  return std::nullopt;
#endif
}

std::optional<int64_t> FreeBytesAt(const std::string& root) {
  std::error_code ec;
  std::filesystem::path path = std::filesystem::absolute(PathFromUtf8(root), ec);
  if (ec) return std::nullopt;
  // 저장 위치는 첫 촬영에서 만들어지므로, 아직 없으면 있는 상위 폴더의 디스크를 읽습니다.
  while (!std::filesystem::exists(path, ec)) {
    if (!path.has_parent_path() || path.parent_path() == path) return std::nullopt;
    path = path.parent_path();
  }
#ifdef _WIN32
  const std::filesystem::space_info info = std::filesystem::space(path, ec);
  if (ec || info.available == static_cast<std::uintmax_t>(-1)) return std::nullopt;
  return static_cast<int64_t>(info.available);
#else
  // libc++의 std::filesystem::space는 남은 공간이 0이면 available을 "모름"(-1)으로 돌려줍니다(macOS HFS+ 실측).
  // 가득 찬 디스크를 모름으로 읽으면 부족 판정이 사라지므로 statvfs로 직접 읽습니다.
  struct statvfs stats {};
  if (statvfs(path.c_str(), &stats) != 0) return std::nullopt;
  return static_cast<int64_t>(stats.f_bavail) * static_cast<int64_t>(stats.f_frsize);
#endif
}

namespace {

std::string FormatBytes(int64_t bytes) {
  char text[32];
  if (bytes >= 1'000'000'000) {
    // 10 GB처럼 소수 자리가 0이면 뺍니다.
    const double gb = static_cast<double>(bytes) / 1e9;
    std::snprintf(text, sizeof(text), gb == std::floor(gb) ? "%.0f GB" : "%.1f GB", gb);
  } else {
    std::snprintf(text, sizeof(text), "%.0f MB", static_cast<double>(std::max<int64_t>(bytes, 0)) / 1e6);
  }
  return text;
}

}  // namespace

FreeSpaceNotice DescribeFreeSpace(std::optional<int64_t> free_bytes, int64_t estimated_bytes) {
  const record::FreeSpaceJudgement space =
      record::JudgeFreeSpace(free_bytes, estimated_bytes, kFreeSpaceReserveBytes, kFreeSpaceLowBytes);
  FreeSpaceNotice notice;
  notice.verdict = space.verdict;
  if (space.verdict == record::FreeSpace::kShort) {
    notice.text = "디스크 여유 부족: 남은 " + FormatBytes(*free_bytes) + ", 필요 " + FormatBytes(space.required_bytes) +
                  "(촬영 추정 " + FormatBytes(estimated_bytes) + ", 예비 " + FormatBytes(kFreeSpaceReserveBytes) +
                  "). 다른 저장 위치를 고르거나 공간을 비워 주세요.";
  } else if (space.verdict == record::FreeSpace::kLow) {
    notice.text = "저장 공간이 " + FormatBytes(*free_bytes) + " 남았습니다. " + FormatBytes(kFreeSpaceLowBytes) +
                  " 미만이면 촬영 중 공간이 부족할 수 있습니다. 다른 위치로 옮기거나 공간을 비워 주세요.";
  }
  return notice;
}

std::string DescribeSaveFailure(std::optional<int64_t> free_bytes, const std::string& folder, const std::string& detail) {
  std::string text = "저장 실패: ";
  if (free_bytes && *free_bytes < kFreeSpaceReserveBytes) {
    text += "저장 공간이 부족해 이 촬영을 끝까지 저장하지 못했습니다(남은 " + FormatBytes(*free_bytes) + ").";
  } else {
    text += "파일을 끝까지 쓰지 못해 이 촬영을 끝까지 저장하지 못했습니다";
    text += detail.empty() ? "." : "(" + detail + ").";
  }
  return text + " 저장된 부분은 " + folder + "에 그대로 있습니다.";
}

std::string CheckShotLimits(const std::string& root, const record::ShotLayout& layout, int64_t estimated_bytes,
                            std::optional<int64_t> free_bytes, std::optional<int32_t> max_path_length) {
  if (max_path_length) {
    const int32_t longest = record::LongestShotPathLength(root, layout, layout.stem);
    if (longest > *max_path_length) {
      return "저장 경로가 너무 김: 가장 긴 파일 경로 " + std::to_string(longest) + "자(상한 " +
             std::to_string(*max_path_length) + "자). 저장 위치를 짧은 폴더로 바꾸십시오";
    }
  }
  const FreeSpaceNotice space = DescribeFreeSpace(free_bytes, estimated_bytes);
  return space.verdict == record::FreeSpace::kShort ? space.text : std::string();
}

}  // namespace hwlab::platform
