#include "hwlab/platform/perfusion_detail.h"

#include <limits>

#include <cmath>
#include <string_view>

namespace hwlab::platform {

namespace {

const NpyArray* Find(const NpzContents& contents, const std::string& name) {
  const auto it = contents.arrays.find(name);
  return it == contents.arrays.end() ? nullptr : &it->second;
}

bool EndsWith(const std::string& text, std::string_view tail) {
  return text.size() > tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

}  // namespace

std::string PerfusionDetailPath(const std::string& output_dir, const std::string& name) {
  std::string path = output_dir;
  if (!path.empty() && path.back() != '/' && path.back() != '\\') path.push_back('/');
  return path + name + "_perfusion.npz";
}

std::optional<PerfusionDetail> PerfusionDetailOf(const NpzContents& contents, std::string* error) {
  const auto fail = [&](const std::string& why) -> std::optional<PerfusionDetail> {
    if (error != nullptr) *error = why;
    return std::nullopt;
  };
  const auto vector_of = [&](const char* name, std::vector<double>* out) {
    const NpyArray* array = Find(contents, name);
    if (array == nullptr || array->shape.size() != 1 || array->values.empty()) return false;
    *out = array->values;
    return true;
  };
  PerfusionDetail d;
  if (!vector_of("time_s", &d.window_centers_s)) return fail("결과 배열 파일에 time_s가 없거나 1차원 배열이 아닙니다");
  if (!vector_of("template_left", &d.template_left) || !vector_of("template_right", &d.template_right)) {
    return fail("결과 배열 파일에 template_left 또는 template_right가 없거나 1차원 배열이 아닙니다");
  }
  if (d.template_left.size() != d.template_right.size()) {
    return fail("좌우 맥파 템플릿 길이가 다릅니다: 왼발 " + std::to_string(d.template_left.size()) + ", 오른발 " +
                std::to_string(d.template_right.size()));
  }
  const size_t windows = d.window_centers_s.size();
  const auto window_series = [&](const std::string& name, std::vector<double>* out) -> bool {
    const NpyArray* array = Find(contents, name);
    if (array == nullptr) return true;
    if (array->shape.size() != 1 || array->values.size() != windows) {
      fail(name + "의 길이가 시간 창 수(" + std::to_string(windows) + ")와 다릅니다");
      return false;
    }
    *out = array->values;
    return true;
  };

  if (const NpyArray* fps = Find(contents, "fps"); fps != nullptr && fps->values.size() == 1) {
    d.fps = std::isfinite(fps->values[0]) && fps->values[0] > 0.0 ? fps->values[0] : kNoNumber;
  }
  if (!window_series("template_corr", &d.template_corr)) return std::nullopt;
  std::vector<double> light_ref;
  if (!window_series("n_light_ref", &light_ref)) return std::nullopt;
  for (const double n : light_ref) d.light_ref_count.push_back(static_cast<int32_t>(n));
  // 관류 지도는 있으면 씁니다. 모양이 (2, 창 수, 행, 열)이 아니면 다른 판의 관류 분석 프로그램으로 보고 비워 둡니다.
  if (const NpyArray* maps = Find(contents, "maps"); maps != nullptr && maps->shape.size() == 4 && maps->shape[0] == 2 &&
                                                       maps->shape[1] == static_cast<int64_t>(windows)) {
    d.maps = maps->values;
    d.map_rows = static_cast<int32_t>(maps->shape[2]);
    d.map_cols = static_cast<int32_t>(maps->shape[3]);
  }
  // 표준 발 틀(v8)의 칸 표시와 윤곽입니다. 모양이 지도 격자와 맞지 않으면 틀 지도를 그리지 않도록 비워 둡니다.
  if (const NpyArray* valid = Find(contents, "map_valid"); valid != nullptr && valid->shape.size() == 2 &&
                                                            valid->shape[0] == d.map_rows && valid->shape[1] == d.map_cols) {
    for (const double v : valid->values) d.map_valid.push_back(v != 0.0 ? 1 : 0);
  }
  if (const NpyArray* outline = Find(contents, "outline"); outline != nullptr && outline->shape.size() == 2 &&
                                                            outline->shape[0] == 3 && outline->shape[1] >= 2) {
    const auto n = static_cast<size_t>(outline->shape[1]);
    d.outline_u.assign(outline->values.begin(), outline->values.begin() + n);
    d.outline_left.assign(outline->values.begin() + n, outline->values.begin() + 2 * n);
    d.outline_right.assign(outline->values.begin() + 2 * n, outline->values.begin() + 3 * n);
  }

  for (const auto& [name, array] : contents.arrays) {
    if (name == "template_left" || name == "template_right") continue;
    for (const auto& [tail, series] : {std::pair{std::string_view("_left"), &d.left_series},
                                       std::pair{std::string_view("_right"), &d.right_series}}) {
      if (!EndsWith(name, tail)) continue;
      std::vector<double> values;
      if (!window_series(name, &values)) return std::nullopt;
      (*series)[name.substr(0, name.size() - tail.size())] = std::move(values);
    }
  }
  return d;
}

double PerfusionDetail::MapAt(int32_t foot, int32_t window, int32_t row, int32_t col) const {
  const int64_t windows = static_cast<int64_t>(window_centers_s.size());
  if (foot < 0 || foot > 1 || window < 0 || window >= windows || row < 0 || row >= map_rows || col < 0 || col >= map_cols) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const size_t at = ((static_cast<size_t>(foot) * windows + window) * map_rows + row) * map_cols + col;
  return at < maps.size() ? maps[at] : std::numeric_limits<double>::quiet_NaN();
}

bool PerfusionDetail::HasTemplateMap() const {
  return !maps.empty() && !map_valid.empty() && outline_u.size() >= 2 && outline_left.size() == outline_u.size() &&
         outline_right.size() == outline_u.size();
}

std::optional<PerfusionDetail> ReadPerfusionDetail(const std::string& path, std::string* error) {
  const auto contents = ReadNpz(path, error);
  if (!contents) return std::nullopt;
  return PerfusionDetailOf(*contents, error);
}

}  // namespace hwlab::platform
