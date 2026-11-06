#include "hwlab/platform/perfusion_summary.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <system_error>

#include "hwlab/platform/shot_folder.h"
#include "json_reader.h"

namespace hwlab::platform {

namespace {

// 앱이 C 로캘을 바꿔도 소수점을 '.'로 읽도록 고전 로캘로 읽습니다.
std::optional<double> ParseDouble(const std::string& text) {
  std::istringstream in(text);
  in.imbue(std::locale::classic());
  double value = 0.0;
  in >> value;
  if (in.fail() || !in.eof()) return std::nullopt;
  return value;
}

// 숫자이면 그 값, NaN이나 무한대이면 kNoNumber, 없거나 숫자가 아니면 nullopt입니다.
std::optional<double> NumberAt(const std::string& json, const std::vector<std::string>& path) {
  const auto scalar = JsonReader(json).FindScalar(path);
  if (!scalar) return std::nullopt;
  if (scalar->kind == JsonKind::kNonFinite) return kNoNumber;
  if (scalar->kind != JsonKind::kNumber) return std::nullopt;
  return ParseDouble(scalar->text);
}

// 숫자 배열입니다. 숫자가 아닌 칸은 kNoNumber로 두어 순서를 지킵니다. 키가 없으면 빈 배열입니다.
std::vector<double> Numbers(const std::string& json, const std::vector<std::string>& path) {
  std::vector<double> out;
  if (const auto items = JsonReader(json).FindArray(path)) {
    for (const auto& item : *items) {
      const auto v = item.kind == JsonKind::kNumber ? ParseDouble(item.text) : std::optional<double>(kNoNumber);
      out.push_back(v.value_or(kNoNumber));
    }
  }
  return out;
}

std::string StringAt(const std::string& json, const std::vector<std::string>& path) {
  std::string value;
  return JsonReader(json).FindString(path, &value) ? value : std::string();
}

std::optional<record::FootSide> FootOf(const std::string& word) {
  if (word == "left") return record::FootSide::kLeft;
  if (word == "right") return record::FootSide::kRight;
  return std::nullopt;
}

int64_t SecondsToUs(const std::optional<double>& seconds) {
  if (!seconds || !std::isfinite(*seconds) || *seconds <= 0.0) return 0;
  return static_cast<int64_t>(std::llround(*seconds * 1e6));
}

std::string Utf8Of(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

// 두 파일의 바이트가 같은지. 읽지 못하면 nullopt입니다.
std::optional<bool> SameBytes(const std::filesystem::path& a, const std::filesystem::path& b) {
  std::ifstream fa(a, std::ios::binary);
  std::ifstream fb(b, std::ios::binary);
  if (!fa || !fb) return std::nullopt;
  std::vector<char> ba(1 << 16);
  std::vector<char> bb(1 << 16);
  for (;;) {
    fa.read(ba.data(), static_cast<std::streamsize>(ba.size()));
    fb.read(bb.data(), static_cast<std::streamsize>(bb.size()));
    const auto na = fa.gcount();
    const auto nb = fb.gcount();
    if (na != nb || !std::equal(ba.begin(), ba.begin() + na, bb.begin())) return false;
    if (na == 0 || fa.eof() || fb.eof()) return fa.eof() == fb.eof();
  }
}

}  // namespace

std::optional<PerfusionSummary> ReadPerfusionSummary(const std::string& json, std::string* error) {
  const auto fail = [&](const std::string& why) -> std::optional<PerfusionSummary> {
    if (error != nullptr) *error = why;
    return std::nullopt;
  };
  PerfusionSummary s;
  const auto left = NumberAt(json, {"pi_left"});
  const auto right = NumberAt(json, {"pi_right"});
  if (!left || !right) return fail("요약에 pi_left 또는 pi_right가 없거나 JSON을 읽지 못했습니다");
  s.pi_left = *left;
  s.pi_right = *right;
  const bool both = std::isfinite(s.pi_left) && std::isfinite(s.pi_right);
  if (const auto ratio = NumberAt(json, {"pi_ratio_left_right"})) {
    s.pi_ratio = *ratio;
  } else if (both && s.pi_right > 0.0) {
    s.pi_ratio = s.pi_left / s.pi_right;
  }
  if (both && s.pi_left < s.pi_right) s.lower_foot = record::FootSide::kLeft;
  if (both && s.pi_right < s.pi_left) s.lower_foot = record::FootSide::kRight;

  // classification 안에서만 찾습니다. thermal_mask, foot_mask에도 left, method 같은 키가 있습니다.
  const auto p_pad = NumberAt(json, {"classification", "p_pad"});
  const auto threshold = NumberAt(json, {"classification", "threshold"});
  if (p_pad && threshold) {
    s.classified = true;
    s.p_pad = *p_pad;
    s.threshold = *threshold;
    s.decision = StringAt(json, {"classification", "decision"});
    s.model = StringAt(json, {"classification", "kind"});
    s.affected_foot = FootOf(StringAt(json, {"classification", "affected_side"}));
    s.window_probs = Numbers(json, {"classification", "window_probs"});
    s.thermal_in_verdict = s.model.find("multimodal") != std::string::npos;
    s.window_centres_s = Numbers(json, {"classification", "window_centres_s"});
  }
  // 규칙 판정은 맥박 품질이 부족하면 level_pct_sbp를 null로 둡니다. 그래도 특징 값과 이유를 보이도록 항목을 읽습니다.
  if (const auto applicable = JsonReader(json).FindScalar({"cuff_level", "applicable"})) {
    CuffLevel c;
    const auto level = NumberAt(json, {"cuff_level", "level_pct_sbp"});
    c.has_level = level && std::isfinite(*level);
    if (c.has_level) c.level_pct = static_cast<int32_t>(std::lround(*level));
    const auto number = [&](std::vector<std::string> path) { return NumberAt(json, path).value_or(kNoNumber); };
    c.e = number({"cuff_level", "E"});
    c.p_ge40 = number({"cuff_level", "p_ge40"});
    c.p_ge80 = number({"cuff_level", "p_ge80"});
    c.p_120 = number({"cuff_level", "p_120"});
    c.cuts = Numbers(json, {"cuff_level", "cuts"});
    c.applicable = applicable->kind == JsonKind::kTrue;
    c.setting_mode = StringAt(json, {"cuff_level", "setting", "mode"});
    c.exposure_step = number({"cuff_level", "setting", "exposure_step"});
    c.gain = number({"cuff_level", "setting", "gain"});
    c.window_e = Numbers(json, {"cuff_level", "window_E"});
    c.window_centres_s = Numbers(json, {"cuff_level", "window_centres_s"});
    c.rule = StringAt(json, {"cuff_level", "rule"});
    if (const auto ok = JsonReader(json).FindScalar({"cuff_level", "quality_ok"})) c.quality_ok = ok->kind != JsonKind::kFalse;
    if (const auto ignored = NumberAt(json, {"cuff_level", "level_if_quality_ignored"}); ignored && std::isfinite(*ignored)) {
      c.level_if_quality_ignored = static_cast<int32_t>(std::lround(*ignored));
    }
    // 커프 상태 형식은 상태를 문자로 내고 level_pct_sbp가 없어, 단계가 있는지를 level 문자로 정합니다.
    c.level_code = StringAt(json, {"cuff_level", "level"});
    c.level_ko = StringAt(json, {"cuff_level", "level_ko"});
    c.level_code_if_quality_ignored = StringAt(json, {"cuff_level", "level_if_quality_ignored"});
    c.states = !c.level_code_if_quality_ignored.empty();
    if (c.states) c.has_level = !c.level_code.empty();
    c.pad_probability = number({"cuff_level", "pad_probability"});
    if (const auto pad = JsonReader(json).FindScalar({"cuff_level", "pad"});
        pad && (pad->kind == JsonKind::kTrue || pad->kind == JsonKind::kFalse)) {
      c.pad = pad->kind == JsonKind::kTrue;
    }
    c.p_state_0 = number({"cuff_level", "state_probabilities", "0"});
    c.p_state_v = number({"cuff_level", "state_probabilities", "V"});
    c.p_state_d = number({"cuff_level", "state_probabilities", "D"});
    c.p_state_o = number({"cuff_level", "state_probabilities", "O"});
    c.h1re_30 = number({"cuff_level", "features", "h1re_30"});
    c.delay2_30_ms = number({"cuff_level", "features", "delay2_30"});
    c.pi_ratio = number({"cuff_level", "features", "pi_ratio"});
    c.snr_db = number({"cuff_level", "features", "hi_snr1_30"});
    c.lower_side = StringAt(json, {"cuff_level", "features", "lower_side"});
    c.h1re_30_below = number({"cuff_level", "thresholds", "h1re_30"});
    c.delay2_30_above_ms = number({"cuff_level", "thresholds", "delay2_30"});
    c.pi_ratio_below = number({"cuff_level", "thresholds", "pi_ratio"});
    c.quality_snr_db = number({"cuff_level", "quality_snr_db"});
    s.cuff_level = c;
  }
  for (const char* side : {"left", "right"}) {
    PerfusionSummary::Foot& foot = std::string(side) == "left" ? s.left : s.right;
    const auto number = [&](std::vector<std::string> path) { return NumberAt(json, path).value_or(kNoNumber); };
    foot.pi_toes = number({"per_side", side, "pi_toes"});
    foot.pi_fore = number({"per_side", side, "pi_fore"});
    foot.pi_mid = number({"per_side", side, "pi_mid"});
    foot.pi_heel = number({"per_side", side, "pi_heel"});
    foot.mask_method = StringAt(json, {"foot_mask", side, "method"});
    foot.detection_score = number({"foot_mask", side, "det_score"});
    foot.mask_iou = number({"foot_mask", side, "sam_iou"});
    foot.blocks = number({"foot_mask", side, "blocks"});
    foot.cells = number({"foot_mask", side, "cells"});
    foot.thermal_mask_dice = number({"classification", "thermal_mask", side, "top_dice_vs_thermal_sam"});
  }
  s.template_corr = NumberAt(json, {"template_corr_lr"}).value_or(kNoNumber);
  s.light_ref_min = NumberAt(json, {"n_light_ref_min"}).value_or(kNoNumber);
  s.cells_used = NumberAt(json, {"foot_cells", "used"}).value_or(kNoNumber);
  s.cells_common = NumberAt(json, {"foot_cells", "common"}).value_or(kNoNumber);
  // 측정 품질 안내(qc)는 status가 있을 때만 읽습니다. items는 객체 배열이라 번호로 하나씩 꺼냅니다.
  if (const std::string status = StringAt(json, {"qc", "status"}); !status.empty()) {
    MeasurementQuality q;
    q.status = status;
    q.status_ko = StringAt(json, {"qc", "status_ko"});
    q.action_ko = StringAt(json, {"qc", "action_ko"});
    for (int i = 0;; ++i) {
      const std::string at = std::to_string(i);
      const auto code = JsonReader(json).FindScalar({"qc", "items", at, "code"});
      if (!code || code->kind != JsonKind::kString) break;
      const auto text = [&](const char* key) {
        const auto v = JsonReader(json).FindScalar({"qc", "items", at, key});
        return v && (v->kind == JsonKind::kString || v->kind == JsonKind::kNumber) ? v->text : std::string();
      };
      q.items.push_back({code->text, text("name_ko"), text("severity"), text("value"), text("unit"), text("threshold"),
                         text("message_ko"), text("action_ko"), text("reason")});
    }
    q.thermal_contrast_left = NumberAt(json, {"qc", "thermal_contrast", "left"}).value_or(kNoNumber);
    q.thermal_contrast_right = NumberAt(json, {"qc", "thermal_contrast", "right"}).value_or(kNoNumber);
    s.quality = std::move(q);
  }
  s.thermal_skipped = StringAt(json, {"dfr", "thermal_skipped"});
  s.presegment_skipped = StringAt(json, {"dfr", "presegment", "reason"});
  s.prestream_skipped = StringAt(json, {"dfr", "prestream", "reason"});
  s.upstream_commit = StringAt(json, {"dfr", "upstream_commit"});
  s.worker_version = StringAt(json, {"dfr", "worker_version"});
  s.api_version = StringAt(json, {"dfr", "api_version"});
  s.variant = StringAt(json, {"dfr", "variant"});

  if (const auto count = NumberAt(json, {"n_windows"}); count && std::isfinite(*count) && *count > 0) {
    s.window_count = static_cast<int32_t>(*count);
  }
  s.window_us = SecondsToUs(NumberAt(json, {"window_s"}));
  s.window_stride_us = SecondsToUs(NumberAt(json, {"window_stride_s"}));
  s.fps = NumberAt(json, {"fps"}).value_or(kNoNumber);
  if (const auto frames = NumberAt(json, {"n_frames"}); frames && std::isfinite(*frames) && *frames > 0) {
    s.frames = static_cast<int64_t>(*frames);
  }
  return s;
}

PadVerdict PadVerdictOf(const PerfusionSummary& summary) {
  if (!summary.classified) return PadVerdict::kNone;
  if (summary.decision == "PAD-like") return PadVerdict::kPadLike;
  if (summary.decision == "normal") return PadVerdict::kNormal;
  return PadVerdict::kNone;
}

std::string DescribeThermalSkip(const std::string& code) {
  if (code.empty()) return {};
  if (code == "model_missing") return "열화상 판정 모델 파일이 없습니다.";
  if (code == "rgb_segmentation_failed") return "RGB 영상에서 발 분할에 실패한 발이 있습니다.";
  if (code == "too_short") return "RGB 영상이 30fps 900장보다 짧습니다.";
  if (code == "no_thermal_files") return "열화상 분석 파일(thermal_left.npy, thermal_right.npy)이 없습니다.";
  if (code == "thermal_foot_not_found") return "열화상에서 발을 찾지 못했습니다.";
  if (code == "thermal_not_portrait") return "열화상 발 영역이 세로보다 가로가 길어 발 검출기 입력 조건에 맞지 않습니다.";
  if (code == "thermal_too_short") return "열화상 장 수가 30초 동안 7.5Hz 구간을 모두 채우지 못합니다.";
  return "관류 분석 프로그램이 적은 이유 코드: " + code;
}

std::string DescribeSpeedupSkip(const std::string& code) {
  if (code.empty()) return {};
  if (code == "model_not_loaded") return "녹화가 시작될 때 발 분할 모델이 아직 올라 있지 않았습니다.";
  if (code == "first_frame_unreadable") return "녹화 중인 영상에서 첫 장을 읽지 못했습니다.";
  if (code == "not_requested" || code == "not_started") return "이번 녹화의 미리 계산 요청이 관류 분석 프로그램에 닿지 않았습니다.";
  if (code == "first_frame_mismatch") return "녹화 중에 읽은 첫 장과 저장된 영상의 첫 장이 다릅니다.";
  if (code == "file_not_created") return "녹화 시작 뒤 20초 안에 영상 파일이 생기지 않았습니다.";
  if (code == "open_failed") return "녹화 중인 영상을 열지 못했습니다.";
  if (code == "not_30fps") return "영상이 30fps가 아닙니다.";
  if (code == "no_presegment") return "첫 장의 발 분할 결과가 없어 미리 읽지 않았습니다.";
  if (code == "cancelled") return "다음 녹화가 시작되어 미리 읽기를 멈췄습니다.";
  if (code == "frame_count_mismatch") return "미리 읽은 장 수가 분석할 장 수와 다릅니다.";
  if (code == "verify_failed") return "미리 읽은 값이 저장된 영상에서 다시 읽은 값과 다릅니다.";
  return "관류 분석 프로그램이 적은 이유 코드: " + code;
}

std::string PerfusionGifPath(const std::string& output_dir, const std::string& name) {
  std::string path = output_dir;
  if (!path.empty() && path.back() != '/' && path.back() != '\\') path.push_back('/');
  return path + name + "_perfusion.gif";
}

bool IsCollectedResultFile(const std::string& file_name, const std::string& name) {
  const std::string head = name + "_";
  if (name.empty() || file_name.size() <= head.size() || file_name.compare(0, head.size(), head) != 0) return false;
  const std::string rest = file_name.substr(head.size());
  const auto ends_with = [&](const std::string& tail) {
    return rest.size() >= tail.size() && rest.compare(rest.size() - tail.size(), tail.size(), tail) == 0;
  };
  return rest == "summary.json" || rest == "report.txt" || rest == "perfusion.gif" || ends_with(".png") ||
         rest == "frame_timing.csv" || rest == "frame_windows.csv";
}

std::optional<CollectedResults> CollectPerfusionResults(const std::string& output_dir, const std::string& name,
                                                       const std::string& root, const record::ShotLayout& layout,
                                                       const std::string& tag, std::string* error) {
  const auto fail = [&](const std::string& why) -> std::optional<CollectedResults> {
    if (error != nullptr) *error = why;
    return std::nullopt;
  };
  namespace fs = std::filesystem;
  if (record::CollectedResultPrefix(layout).empty()) return fail("분석결과 이름 앞부분이 비었습니다");
  std::error_code ec;
  const fs::path source = PathFromUtf8(output_dir);
  std::vector<std::string> names;
  for (fs::directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const std::string file = Utf8Of(it->path().filename());
    if (IsCollectedResultFile(file, name)) names.push_back(file);
  }
  if (ec) return fail("분석 결과 폴더를 읽지 못함: " + output_dir + " (" + ec.message() + ")");
  std::sort(names.begin(), names.end());
  const fs::path target = (PathFromUtf8(root) / PathFromUtf8(record::kCollectedResultFolder)).make_preferred();

  // 복사 전에 모두 확인해, 내용이 다른 파일이 하나라도 있으면 그 번호에는 일부만 복사된 상태를 남기지 않습니다.
  struct Plan {
    fs::path from;
    fs::path to;
    bool present = false;  // 같은 내용이 이미 있음
  };
  std::vector<Plan> plans;
  std::string conflict;
  int32_t number = 1;
  for (; number <= record::kMaxReanalysisNumber; ++number) {
    const std::string prefix = record::CollectedResultPrefix(layout, number) + (tag.empty() ? std::string() : "_" + tag);
    plans.clear();
    conflict.clear();
    for (const std::string& file : names) {
      Plan plan{source / PathFromUtf8(file), target / PathFromUtf8(prefix + file.substr(name.size()))};
      if (fs::exists(plan.to, ec)) {
        const auto same = SameBytes(plan.from, plan.to);
        if (!same) return fail("분석결과 파일을 비교하지 못함: " + Utf8Of(plan.to));
        if (!*same) {
          conflict = Utf8Of(plan.to);
          break;
        }
        plan.present = true;
      }
      plans.push_back(std::move(plan));
    }
    if (conflict.empty()) break;
  }
  if (!conflict.empty()) {
    return fail("재분석 번호(_reanalysis) " + std::to_string(record::kMaxReanalysisNumber) + "까지 내용이 다른 같은 이름 파일이 있음: " +
                conflict);
  }
  fs::create_directories(target, ec);
  if (ec) return fail("분석결과 폴더를 만들지 못함: " + Utf8Of(target) + " (" + ec.message() + ")");
  CollectedResults collected;
  collected.folder = Utf8Of(target);
  collected.reanalysis = number;
  for (const Plan& plan : plans) {
    if (!plan.present) {
      fs::copy_file(plan.from, plan.to, fs::copy_options::none, ec);
      if (ec) return fail("복사하지 못함: " + Utf8Of(plan.to) + " (" + ec.message() + ")");
      ++collected.copied;
    }
    collected.files.push_back(Utf8Of(plan.to));
  }
  return collected;
}

std::string MeasurementQuality::WorstReason() const {
  // 관류 분석 프로그램(quality.assess)과 같이 fail, warn 순으로 가장 심한 항목 중 앞의 것을 고릅니다.
  const auto rank = [](const std::string& severity) { return severity == "fail" ? 2 : severity == "warn" ? 1 : 0; };
  const Item* worst = nullptr;
  for (const Item& item : items) {
    if (rank(item.severity) > 0 && (worst == nullptr || rank(item.severity) > rank(worst->severity))) worst = &item;
  }
  return worst != nullptr ? worst->reason : std::string();
}

}  // namespace hwlab::platform
