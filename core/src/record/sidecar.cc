#include "hwlab/record/sidecar.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace hwlab::record {

namespace {

// 2: stream, light_percent, clock, per_frame.common_time_us 추가
// 3: display_range, frames.received, frames.record_period_us, frames.written_in_rgb_window 추가
// 4: light_percent를 light(source, percent, port_vid_pid, serial, confirmed, response, flicker)로 바꿈
// 5: settings.exposure_ms(노출 단계의 명목 노출 시간) 추가
// 6: lens_shading(열화상 렌즈 셰이딩 보정 결과) 추가
// 7: lens_shading을 source(none, loaded, corrected, failed)와 file_unix_s(보정 파일의 수정 시각)로 바꿈
// 8: frames.slow_intervals, frames.min_fps_1s, frames.slow_spans(느린 간격, timing::CheckFrameRate) 추가
constexpr int kSchemaVersion = 8;
// PPG 사이드카는 영상 사이드카와 따로 셉니다.
// 2: device_info(장치 정보 응답) 추가
// 3: anchor_device_us, anchor_host_us(측정 데이터마다 마지막 표본의 장치 시각과 PC 수신 시각) 추가
constexpr int kPpgSchemaVersion = 3;

void AppendEscaped(std::string& out, const std::string& text) {
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
          // UTF-8 바이트는 그대로 둡니다. 한글 파일 이름을 이스케이프 없이 읽을 수 있게 하기 위함입니다.
          out.push_back(c);
        }
    }
  }
  out.push_back('"');
}

// 들여쓰기 2칸의 JSON을 씁니다. 사람이 열어 확인하는 파일이라 한 줄에 값 하나를 둡니다.
class JsonWriter {
 public:
  void Open(const char* key, char bracket) {
    Key(key);
    out_.push_back(bracket);
    first_ = true;
    ++depth_;
  }
  void Close(char bracket) {
    --depth_;
    if (first_) {
      out_.push_back(bracket);
      first_ = false;
      return;
    }
    out_.push_back('\n');
    Indent();
    out_.push_back(bracket);
    first_ = false;
  }
  void String(const char* key, const std::string& value) {
    Key(key);
    AppendEscaped(out_, value);
  }
  void Int(const char* key, int64_t value) {
    Key(key);
    out_ += std::to_string(value);
  }
  void Null(const char* key) {
    Key(key);
    out_ += "null";
  }
  void OptionalBool(const char* key, const std::optional<bool>& value) {
    Key(key);
    out_ += value ? (*value ? "true" : "false") : "null";
  }
  void OptionalInt(const char* key, const std::optional<int32_t>& value) {
    Key(key);
    out_ += value ? std::to_string(*value) : "null";
  }
  // 소수점 아래 자릿수를 고정해 같은 값에는 플랫폼과 관계없이 같은 바이트를 냅니다.
  void Fixed(const char* key, double value, int digits) {
    Key(key);
    char text[64];
    std::snprintf(text, sizeof(text), "%.*f", digits, value);
    out_ += text;
  }
  // 숫자 배열은 한 줄에 씁니다. 900개 값을 줄마다 나누면 파일을 사람이 읽기 어렵습니다.
  template <typename Get>
  void IntArray(const char* key, size_t count, Get get) {
    Key(key);
    out_.push_back('[');
    for (size_t i = 0; i < count; ++i) {
      if (i > 0) {
        out_ += ", ";
      }
      out_ += std::to_string(get(i));
    }
    out_.push_back(']');
  }
  std::string Take() {
    out_.push_back('\n');
    return std::move(out_);
  }

 private:
  void Key(const char* key) {
    if (depth_ > 0) {
      out_ += first_ ? "\n" : ",\n";
      Indent();
    }
    first_ = false;
    if (key != nullptr) {
      AppendEscaped(out_, key);
      out_ += ": ";
    }
  }
  void Indent() { out_.append(static_cast<size_t>(depth_) * 2, ' '); }

  std::string out_;
  int depth_ = 0;
  bool first_ = true;
};

const char* NameOf(PixelFormat format) {
  switch (format) {
    case PixelFormat::kUyvy:
      return "UYVY";
    case PixelFormat::kNv12:
      return "NV12";
    case PixelFormat::kY16:
      return "Y16";
  }
  return "";
}

const char* NameOf(LightSource source) {
  switch (source) {
    case LightSource::kUnknown:
      return "unknown";
    case LightSource::kExternal:
      return "external";
    case LightSource::kApp:
      return "app";
  }
  return "";
}

const char* NameOf(LensShadingSource source) {
  switch (source) {
    case LensShadingSource::kNone:
      return "none";
    case LensShadingSource::kLoaded:
      return "loaded";
    case LensShadingSource::kCorrected:
      return "corrected";
    case LensShadingSource::kFailed:
      return "failed";
  }
  return "";
}

const char* NameOf(pixel::LightResponse response) {
  switch (response) {
    case pixel::LightResponse::kUnknown:
      return "unknown";
    case pixel::LightResponse::kApplied:
      return "applied";
    case pixel::LightResponse::kNotSeen:
      return "not_seen";
  }
  return "";
}

// 밝기 값(화소 하나의 Y)은 소수점 아래 4자리, 비율과 주기도 4자리로 고정합니다.
void WriteLight(JsonWriter& w, const LightRecord& light) {
  w.Open("light", '{');
  w.String("source", NameOf(light.source));
  w.OptionalInt("percent", light.percent);
  if (light.usb_vendor_id && light.usb_product_id) {
    char vid_pid[16];
    std::snprintf(vid_pid, sizeof(vid_pid), "%04X:%04X", *light.usb_vendor_id, *light.usb_product_id);
    w.String("port_vid_pid", vid_pid);
  } else {
    w.Null("port_vid_pid");
  }
  if (light.serial.empty()) {
    w.Null("serial");
  } else {
    w.String("serial", light.serial);
  }
  w.OptionalBool("confirmed", light.confirmed);
  w.String("response", NameOf(light.response));
  if (light.flicker) {
    const pixel::Flicker& f = *light.flicker;
    w.Open("flicker", '{');
    w.Int("frames", f.frame_count);
    w.Fixed("mean", f.mean, 4);
    w.Fixed("stddev", f.stddev, 4);
    w.Fixed("peak_to_peak", f.peak_to_peak, 4);
    w.Fixed("periodic_fraction", f.periodic_fraction, 4);
    if (f.dominant_period_frames) {
      w.Fixed("dominant_period_frames", *f.dominant_period_frames, 4);
    } else {
      w.Null("dominant_period_frames");
    }
    w.Close('}');
  } else {
    w.Null("flicker");
  }
  w.Close('}');
}

const char* NameOf(StreamKind stream) {
  switch (stream) {
    case StreamKind::kRgb:
      return "rgb";
    case StreamKind::kThermal:
      return "thermal";
  }
  return "";
}

// 장치가 보낸 바이트 하나입니다. 인쇄할 수 없는 바이트를 그대로 쓰면 UTF-8이 아닌 JSON이 되므로 16진수로 씁니다.
std::string CodeText(uint8_t code) {
  if (code >= 0x20 && code < 0x7f) return std::string(1, static_cast<char>(code));
  char text[8];
  std::snprintf(text, sizeof(text), "0x%02x", code);
  return text;
}

void WriteErrorSpans(JsonWriter& w, const std::vector<PpgErrorSpan>& spans, const std::optional<PpgErrorWindow>& window) {
  w.Open("spans", '[');
  for (const PpgErrorSpan& span : spans) {
    w.Open(nullptr, '{');
    w.String("code", CodeText(span.code));
    w.Int("begin_us", span.begin.us);
    w.Int("end_us", span.end.us);
    w.Int("samples", span.samples);
    if (window) {
      w.Fixed("rgb_begin_s", IntervalUs(window->begin, span.begin) / 1e6, 3);
      w.Fixed("rgb_end_s", IntervalUs(window->begin, span.end) / 1e6, 3);
    } else {
      w.Null("rgb_begin_s");
      w.Null("rgb_end_s");
    }
    w.Close('}');
  }
  w.Close(']');
}

}  // namespace

std::vector<PpgErrorSpan> FindPpgErrorSpans(std::span<const Timestamp> times, std::span<const uint8_t> errors) {
  std::vector<PpgErrorSpan> spans;
  const size_t n = std::min(times.size(), errors.size());
  for (size_t i = 0; i < n; ++i) {
    if (errors[i] == 0) continue;
    if (i > 0 && errors[i - 1] == errors[i]) {
      spans.back().end = times[i];
      ++spans.back().samples;
      continue;
    }
    spans.push_back({errors[i], times[i], times[i], 1});
  }
  return spans;
}

PpgErrorWindow SummarizePpgErrors(std::span<const Timestamp> times, std::span<const uint8_t> errors, Timestamp begin,
                                  Timestamp end) {
  PpgErrorWindow out;
  out.begin = begin;
  out.end = end;
  std::array<PpgErrorShare, 256> by_code{};
  uint8_t previous = 0;
  int64_t run = 0;
  const size_t n = std::min(times.size(), errors.size());
  for (size_t i = 0; i < n; ++i) {
    if (times[i] < begin || times[i] > end) {
      previous = 0;
      continue;
    }
    ++out.samples;
    const uint8_t code = errors[i];
    if (code != previous) run = 0;
    previous = code;
    if (code == 0) continue;
    PpgErrorShare& share = by_code[code];
    if (run == 0) ++share.runs;
    ++run;
    ++share.samples;
    share.longest_samples = std::max(share.longest_samples, run);
  }
  for (size_t code = 1; code < by_code.size(); ++code) {
    if (by_code[code].samples == 0) continue;
    by_code[code].code = static_cast<uint8_t>(code);
    out.codes.push_back(by_code[code]);
  }
  return out;
}

std::string BuildSidecarJson(const Sidecar& s) {
  JsonWriter w;
  w.Open(nullptr, '{');
  w.Int("schema_version", kSchemaVersion);
  w.String("video_file", s.video_file);
  w.String("device", s.device);
  w.String("stream", NameOf(s.stream));
  if (s.foot) {
    w.String("foot", *s.foot == FootSide::kLeft ? "left" : "right");
  } else {
    w.Null("foot");
  }
  if (s.light) {
    WriteLight(w, *s.light);
  } else {
    w.Null("light");
  }
  if (s.lens_shading) {
    w.Open("lens_shading", '{');
    w.String("source", NameOf(s.lens_shading->source));
    if (s.lens_shading->file_unix_s) {
      w.Int("file_unix_s", *s.lens_shading->file_unix_s);
    } else {
      w.Null("file_unix_s");
    }
    w.Close('}');
  } else {
    w.Null("lens_shading");
  }

  w.Open("format", '{');
  w.String("pixel_format", NameOf(s.format.format));
  w.Int("width", s.format.width);
  w.Int("height", s.format.height);
  w.Int("frame_rate", s.format.frame_rate);
  w.String("codec", s.codec);
  w.Close('}');

  w.Open("crop", '{');
  w.Int("x", s.crop.x);
  w.Int("y", s.crop.y);
  w.Int("width", s.crop.width);
  w.Int("height", s.crop.height);
  w.Close('}');

  w.Open("settings", '{');
  w.String("mode", s.mode == SettingMode::kAuto ? "auto" : "manual");
  w.OptionalBool("exposure_auto", s.settings.exposure_auto);
  w.OptionalInt("exposure_step", s.settings.exposure_step);
  if (s.settings.exposure_step) {
    w.Fixed("exposure_ms", ExposureStepMs(*s.settings.exposure_step), 3);
  } else {
    w.Null("exposure_ms");
  }
  w.OptionalInt("gain", s.settings.gain);
  w.OptionalBool("white_balance_auto", s.settings.white_balance_auto);
  w.OptionalInt("white_balance_k", s.settings.white_balance_k);
  w.OptionalInt("sdk_frame_rate", s.settings.sdk_frame_rate);
  w.OptionalBool("locked", s.settings.locked);
  w.Close('}');

  w.Open("clock", '{');
  w.Int("offset_us", s.clock.offset_us);
  // 속도 차이는 수백 ppm이라 소수점 아래 9자리(0.001ppm)까지 남깁니다.
  w.Fixed("rate", s.clock.rate, 9);
  w.Int("max_residual_us", s.clock.max_residual_us);
  w.Close('}');

  if (s.display_range) {
    w.Open("display_range", '{');
    w.Int("low", s.display_range->low);
    w.Int("high", s.display_range->high);
    w.Close('}');
  } else {
    w.Null("display_range");
  }

  w.Open("frames", '{');
  w.Int("target", s.target_frames);
  w.Int("written", s.written_frames);
  w.Int("received", s.received_frames);
  w.Int("record_period_us", s.record_period_us);
  w.Int("written_in_rgb_window", s.written_in_rgb_window);
  w.Int("first_time_us", s.first_frame.us);
  w.Int("last_time_us", s.last_frame.us);
  w.Int("median_interval_us", s.median_interval_us);
  w.Int("missing", timing::CountMissingFrames(s.gaps));
  w.Int("dropped", s.dropped_frames);
  w.Int("buffer_frames", s.buffer_frames);
  w.Int("max_queue_frames", s.max_queue_frames);
  w.Int("file_bytes", s.file_bytes);
  w.Open("gaps", '[');
  for (const timing::FrameGap& gap : s.gaps) {
    w.Open(nullptr, '{');
    w.Int("after_frame", static_cast<int64_t>(gap.before));
    w.Int("interval_us", gap.interval_us);
    w.Int("missing", gap.missing_frames);
    w.Close('}');
  }
  w.Close(']');
  w.Int("slow_intervals", s.frame_rate.slow_intervals);
  w.Fixed("min_fps_1s", s.frame_rate.min_fps, 1);
  w.Open("slow_spans", '[');
  for (const timing::SlowSpan& span : s.frame_rate.spans) {
    w.Open(nullptr, '{');
    w.Int("start_us", span.start_us);
    w.Int("end_us", span.end_us);
    w.Int("intervals", span.intervals);
    w.Close('}');
  }
  w.Close(']');
  w.Close('}');

  // 값마다 배열 하나입니다. 평균 밝기는 brightness_sum / brightness_pixels입니다.
  const auto& r = s.records;
  w.Open("per_frame", '{');
  w.Int("brightness_pixels", s.brightness_pixels);
  w.Int("clip_level", s.clip_level);
  w.IntArray("index", r.size(), [&](size_t i) { return static_cast<int64_t>(r[i].index); });
  w.IntArray("device_time_us", r.size(), [&](size_t i) { return r[i].device_time.us; });
  w.IntArray("host_time_us", r.size(), [&](size_t i) { return r[i].host_time.us; });
  w.IntArray("common_time_us", r.size(), [&](size_t i) { return timing::ToCommonTime(s.clock, r[i].device_time).us; });
  w.IntArray("brightness_sum", r.size(), [&](size_t i) { return r[i].brightness_sum; });
  w.IntArray("clipped_pixels", r.size(), [&](size_t i) { return r[i].clipped_pixels; });
  w.Close('}');

  w.Close('}');
  return w.Take();
}

std::string BuildPpgSidecarJson(const PpgSidecar& s) {
  JsonWriter w;
  w.Open(nullptr, '{');
  w.Int("schema_version", kPpgSchemaVersion);
  w.String("samples_file", s.samples_file);
  w.String("port", s.port);
  if (s.device_info) {
    w.String("device_info", *s.device_info);
  } else {
    w.Null("device_info");
  }
  w.Int("sample_rate_hz", s.sample_rate_hz);

  w.Open("record", '{');
  w.Int("begin_us", s.record_begin.us);
  w.Int("end_us", s.record_end.us);
  w.Close('}');

  w.Open("clock", '[');
  for (const timing::ClockSegment& c : s.clock) {
    w.Open(nullptr, '{');
    w.Int("last_device_us", c.last_device.us);
    w.Int("offset_us", c.map.offset_us);
    w.Fixed("rate", c.map.rate, 9);
    w.Int("max_residual_us", c.map.max_residual_us);
    w.Close('}');
  }
  w.Close(']');
  w.IntArray("anchor_device_us", s.anchors.size(), [&](size_t i) { return s.anchors[i].device.us; });
  w.IntArray("anchor_host_us", s.anchors.size(), [&](size_t i) { return s.anchors[i].host.us; });

  w.Open("samples", '{');
  w.Int("count", s.samples);
  w.Int("messages", static_cast<int64_t>(s.anchors.size()));
  w.Int("first_time_us", s.first_sample.us);
  w.Int("last_time_us", s.last_sample.us);
  w.Int("median_interval_us", s.median_interval_us);
  w.Int("missing", timing::CountMissingFrames(s.gaps));
  w.Int("measure_error", s.measure_error_samples);
  w.Open("gaps", '[');
  for (const timing::FrameGap& gap : s.gaps) {
    w.Open(nullptr, '{');
    w.Int("after_sample", static_cast<int64_t>(gap.before));
    w.Int("interval_us", gap.interval_us);
    w.Int("missing", gap.missing_frames);
    w.Close('}');
  }
  w.Close(']');
  w.Close('}');

  w.Open("measure_errors", '{');
  WriteErrorSpans(w, s.error_spans, s.rgb_window);
  if (s.rgb_window) {
    const PpgErrorWindow& window = *s.rgb_window;
    w.Open("rgb_window", '{');
    w.Int("begin_us", window.begin.us);
    w.Int("end_us", window.end.us);
    w.Int("samples", window.samples);
    w.Open("codes", '[');
    for (const PpgErrorShare& share : window.codes) {
      w.Open(nullptr, '{');
      w.String("code", CodeText(share.code));
      w.Int("samples", share.samples);
      w.Int("longest_samples", share.longest_samples);
      w.Int("runs", share.runs);
      w.Close('}');
    }
    w.Close(']');
    w.Close('}');
  } else {
    w.Null("rgb_window");
  }
  w.Close('}');

  w.Open("link", '{');
  w.Int("crc_failures", s.crc_failures);
  w.Int("bad_lengths", s.bad_lengths);
  w.Int("implausible", s.implausible);
  w.Int("skipped_bytes", s.skipped_bytes);
  w.Close('}');

  w.Open("events", '[');
  for (const PpgEvent& e : s.events) {
    w.Open(nullptr, '{');
    w.Int("time_us", e.host_time.us);
    w.String("code", CodeText(e.code));
    w.String("value", CodeText(e.value));
    w.Close('}');
  }
  w.Close(']');

  w.Close('}');
  return w.Take();
}

}  // namespace hwlab::record
