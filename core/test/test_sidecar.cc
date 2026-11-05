#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/record/sidecar.h"

namespace {

using hwlab::PixelFormat;
using hwlab::record::BuildSidecarJson;
using hwlab::record::FootSide;
using hwlab::SettingMode;
using hwlab::StreamKind;
using hwlab::timing::ClockMap;
using hwlab::record::Sidecar;

Sidecar MakeSidecar() {
  Sidecar s;
  s.video_file = "142233_left.avi";
  s.device = "See3CAM_24CUG";
  s.format = {PixelFormat::kUyvy, 960, 1080, 30};
  s.codec = "ffv1";
  s.foot = FootSide::kLeft;
  s.crop = {0, 0, 960, 1080};
  s.mode = SettingMode::kManual;
  s.settings = {false, -5, 4, false, 4600, 30, true};
  s.target_frames = 900;
  s.written_frames = 900;
  s.first_frame = {1000};
  s.last_frame = {30001000};
  return s;
}

// 키 순서와 들여쓰기까지 고정된 바이트를 냅니다.
TEST(Sidecar, WritesFixedLayout) {
  Sidecar s = MakeSidecar();
  s.gaps = {{120, 100000, 2}};
  s.frame_rate = {3, {{4000000, 4120000, 3}}, 25.04};
  s.brightness_pixels = 960 * 1080;
  s.clip_level = 250;
  s.records = {{0, {1000}, {1500}, 100, 0}, {1, {34333}, {34900}, 101, 3}, {4, {134333}, {134800}, 102, 0}};

  const std::string expected = R"({
  "schema_version": 8,
  "video_file": "142233_left.avi",
  "device": "See3CAM_24CUG",
  "stream": "rgb",
  "foot": "left",
  "light": null,
  "lens_shading": null,
  "format": {
    "pixel_format": "UYVY",
    "width": 960,
    "height": 1080,
    "frame_rate": 30,
    "codec": "ffv1"
  },
  "crop": {
    "x": 0,
    "y": 0,
    "width": 960,
    "height": 1080
  },
  "settings": {
    "mode": "manual",
    "exposure_auto": false,
    "exposure_step": -5,
    "exposure_ms": 31.250,
    "gain": 4,
    "white_balance_auto": false,
    "white_balance_k": 4600,
    "sdk_frame_rate": 30,
    "locked": true
  },
  "clock": {
    "offset_us": 0,
    "rate": 1.000000000,
    "max_residual_us": 0
  },
  "display_range": null,
  "frames": {
    "target": 900,
    "written": 900,
    "received": 0,
    "record_period_us": 0,
    "written_in_rgb_window": 0,
    "first_time_us": 1000,
    "last_time_us": 30001000,
    "median_interval_us": 0,
    "missing": 2,
    "dropped": 0,
    "buffer_frames": 0,
    "max_queue_frames": 0,
    "file_bytes": -1,
    "gaps": [
      {
        "after_frame": 120,
        "interval_us": 100000,
        "missing": 2
      }
    ],
    "slow_intervals": 3,
    "min_fps_1s": 25.0,
    "slow_spans": [
      {
        "start_us": 4000000,
        "end_us": 4120000,
        "intervals": 3
      }
    ]
  },
  "per_frame": {
    "brightness_pixels": 1036800,
    "clip_level": 250,
    "index": [0, 1, 4],
    "device_time_us": [1000, 34333, 134333],
    "host_time_us": [1500, 34900, 134800],
    "common_time_us": [1000, 34333, 134333],
    "brightness_sum": [100, 101, 102],
    "clipped_pixels": [0, 3, 0]
  }
}
)";
  EXPECT_EQ(BuildSidecarJson(s), expected);
}

// 자동 맞춤에서 읽지 못한 노출값은 null로 남깁니다.
TEST(Sidecar, WritesNullForUnknownExposure) {
  Sidecar s = MakeSidecar();
  s.mode = SettingMode::kAuto;
  s.settings.exposure_step.reset();

  const std::string json = BuildSidecarJson(s);

  EXPECT_NE(json.find("\"mode\": \"auto\""), std::string::npos);
  EXPECT_NE(json.find("\"exposure_step\": null"), std::string::npos);
  EXPECT_NE(json.find("\"exposure_ms\": null"), std::string::npos);
}

// 열화상 사이드카의 렌즈 셰이딩 보정 기록은 source(none, loaded, corrected, failed)와 file_unix_s이고, 없으면 null입니다.
TEST(Sidecar, WritesLensShading) {
  using hwlab::record::LensShadingSource;
  Sidecar s = MakeSidecar();
  s.stream = StreamKind::kThermal;
  EXPECT_NE(BuildSidecarJson(s).find("\"lens_shading\": null,"), std::string::npos);
  s.lens_shading = hwlab::record::LensShadingRecord{LensShadingSource::kLoaded, 1761205212};
  EXPECT_NE(BuildSidecarJson(s).find(R"("lens_shading": {
    "source": "loaded",
    "file_unix_s": 1761205212
  },)"),
            std::string::npos)
      << BuildSidecarJson(s);
  s.lens_shading = hwlab::record::LensShadingRecord{LensShadingSource::kNone, std::nullopt};
  EXPECT_NE(BuildSidecarJson(s).find(R"("source": "none",
    "file_unix_s": null)"),
            std::string::npos);
  s.lens_shading->source = LensShadingSource::kCorrected;
  EXPECT_NE(BuildSidecarJson(s).find("\"source\": \"corrected\""), std::string::npos);
  s.lens_shading->source = LensShadingSource::kFailed;
  EXPECT_NE(BuildSidecarJson(s).find("\"source\": \"failed\""), std::string::npos);
}

// 누락이 없으면 빈 배열입니다.
TEST(Sidecar, WritesEmptyGaps) {
  EXPECT_NE(BuildSidecarJson(MakeSidecar()).find("\"gaps\": []"), std::string::npos);
}

// 900장의 프레임별 기록은 값마다 900개 숫자가 든 배열 하나가 됩니다.
TEST(Sidecar, WritesNineHundredFrameRecords) {
  Sidecar s = MakeSidecar();
  for (int64_t i = 0; i < 900; ++i) {
    s.records.push_back({static_cast<hwlab::FrameIndex>(i), {i * 33333}, {i * 33333 + 400}, i, 0});
  }

  const std::string json = BuildSidecarJson(s);

  const size_t at = json.find("\"device_time_us\": [");
  ASSERT_NE(at, std::string::npos);
  const size_t end = json.find(']', at);
  int commas = 0;
  for (size_t i = at; i < end; ++i) {
    commas += json[i] == ',' ? 1 : 0;
  }
  EXPECT_EQ(commas, 899);
  EXPECT_NE(json.find("29966367]"), std::string::npos);  // 마지막 장치 시각 899 * 33333
}

// 따옴표, 역슬래시, 제어 문자는 이스케이프하고 한글은 그대로 둡니다.
TEST(Sidecar, EscapesStringsAndKeepsUtf8) {
  Sidecar s = MakeSidecar();
  s.video_file = "발\"a\\b\n\x01.avi";

  const std::string json = BuildSidecarJson(s);

  // MSVC는 raw 문자열 안의 \u도 유니코드 이스케이프로 읽어, 일반 문자열로 씁니다.
  EXPECT_NE(json.find("\"video_file\": \"발\\\"a\\\\b\\n\\u0001.avi\""), std::string::npos);
}

// 열화상 사이드카는 스트림 종류와 시계 식을 남기고 공통 시각을 시계 식으로 계산합니다. 조명 기록은 null입니다.
TEST(Sidecar, WritesStreamLightAndCommonTime) {
  Sidecar s = MakeSidecar();
  s.stream = StreamKind::kThermal;
  s.clock = ClockMap{1'234'000, 1.0001, 812};
  s.records = {{0, {0}, {1'235'000}, 0, 0}, {1, {1'000'000}, {2'335'000}, 0, 0}};

  const std::string json = BuildSidecarJson(s);

  EXPECT_NE(json.find("\"stream\": \"thermal\""), std::string::npos);
  EXPECT_NE(json.find("\"light\": null"), std::string::npos);
  EXPECT_NE(json.find("\"offset_us\": 1234000"), std::string::npos);
  EXPECT_NE(json.find("\"rate\": 1.000100000"), std::string::npos);
  EXPECT_NE(json.find("\"max_residual_us\": 812"), std::string::npos);
  // 1'234'000 + 1.0001 * 1'000'000
  EXPECT_NE(json.find("\"common_time_us\": [1234000, 2234100]"), std::string::npos);
}

// 열화상 사이드카는 확인용 영상의 표시 범위(원시값), 받은 장 수, 기록 주기, RGB 녹화 구간 안의 기록 장 수를 남깁니다.
TEST(Sidecar, WritesThermalRangeAndCounts) {
  Sidecar s = MakeSidecar();
  s.stream = StreamKind::kThermal;
  s.display_range = hwlab::pixel::Y16Range{7012, 8960};
  s.written_frames = 270;
  s.received_frames = 812;
  s.record_period_us = 111111;
  s.written_in_rgb_window = 268;

  const std::string json = BuildSidecarJson(s);

  EXPECT_NE(json.find("  \"display_range\": {\n    \"low\": 7012,\n    \"high\": 8960\n  },\n  \"frames\""),
            std::string::npos)
      << json;
  EXPECT_NE(json.find("\"written\": 270,\n    \"received\": 812,\n    \"record_period_us\": 111111,\n"
                      "    \"written_in_rgb_window\": 268,"),
            std::string::npos)
      << json;
}

// 앱이 보낸 조명은 출처, 밝기, VID:PID, 일련번호, 사람 확인, 반영 판정, 깜빡임 요약을 고정된 바이트로 남깁니다.
TEST(Sidecar, WritesAppLightBytes) {
  Sidecar s = MakeSidecar();
  hwlab::record::LightRecord light;
  light.source = hwlab::record::LightSource::kApp;
  light.percent = 70;
  light.usb_vendor_id = 0x0403;
  light.usb_product_id = 0x6001;
  light.serial = "A50285BIA";
  light.confirmed = true;
  light.response = hwlab::pixel::LightResponse::kApplied;
  hwlab::pixel::Flicker flicker;
  flicker.frame_count = 900;
  flicker.mean = 101.25;
  flicker.stddev = 0.5;
  flicker.peak_to_peak = 1.75;
  flicker.periodic_fraction = 0.93126;
  flicker.dominant_period_frames = 12.5;
  light.flicker = flicker;
  s.light = light;

  const std::string json = BuildSidecarJson(s);

  const std::string expected = R"(  "foot": "left",
  "light": {
    "source": "app",
    "percent": 70,
    "port_vid_pid": "0403:6001",
    "serial": "A50285BIA",
    "confirmed": true,
    "response": "applied",
    "flicker": {
      "frames": 900,
      "mean": 101.2500,
      "stddev": 0.5000,
      "peak_to_peak": 1.7500,
      "periodic_fraction": 0.9313,
      "dominant_period_frames": 12.5000
    }
  },
  "lens_shading": null,
  "format": {)";
  EXPECT_NE(json.find(expected), std::string::npos) << json;
}

// 앱 밖에서 켠 조명은 밝기, 포트, 일련번호가 null이고 판정은 unknown입니다. 주기가 없으면 주기만 null입니다.
TEST(Sidecar, WritesExternalLightWithNulls) {
  Sidecar s = MakeSidecar();
  hwlab::record::LightRecord light;
  light.source = hwlab::record::LightSource::kExternal;
  light.flicker = hwlab::pixel::Flicker{.frame_count = 3};
  s.light = light;

  const std::string json = BuildSidecarJson(s);

  const std::string expected = R"(  "light": {
    "source": "external",
    "percent": null,
    "port_vid_pid": null,
    "serial": null,
    "confirmed": false,
    "response": "unknown",
    "flicker": {
      "frames": 3,
      "mean": 0.0000,
      "stddev": 0.0000,
      "peak_to_peak": 0.0000,
      "periodic_fraction": 0.0000,
      "dominant_period_frames": null
    }
  },)";
  EXPECT_NE(json.find(expected), std::string::npos) << json;
}

// PPG 사이드카는 장치 정보, 시계 구간마다 식, 표본 수와 누락, 통신 집계, 장치 상태를 키 순서와 들여쓰기까지 고정된 바이트로 남깁니다.
TEST(PpgSidecar, WritesFixedLayout) {
  hwlab::record::PpgSidecar s;
  s.samples_file = "a_ppg.csv";
  s.port = "COM6";
  s.device_info = "100002280280320";
  s.sample_rate_hz = 500;
  s.record_begin = {1000};
  s.record_end = {40'000'000};
  s.clock = {{{46'000}, ClockMap{1'000'000, 1.005, 4'000}}, {{30'000'000}, ClockMap{1'128'000, 1.005, 9'000}}};
  s.samples = 15'000;
  s.messages = 625;
  s.first_sample = {1'000'000};
  s.last_sample = {31'160'000};
  s.median_interval_us = 2010;
  s.gaps = {{23, 130'000, 64}};
  s.measure_error_samples = 12;
  s.crc_failures = 1;
  s.implausible = 2;
  s.skipped_bytes = 7;
  s.events = {{{1'050'000}, 's', 'v'}, {{39'990'000}, 's', 'u'}};
  s.error_spans = {{'f', {1'000'000}, {1'046'000}, 24}};
  s.rgb_window = hwlab::record::PpgErrorWindow{{6'000'000}, {35'966'667}, 14'990, {{'s', 12, 12, 1}}};

  const std::string expected = R"({
  "schema_version": 2,
  "samples_file": "a_ppg.csv",
  "port": "COM6",
  "device_info": "100002280280320",
  "sample_rate_hz": 500,
  "record": {
    "begin_us": 1000,
    "end_us": 40000000
  },
  "clock": [
    {
      "last_device_us": 46000,
      "offset_us": 1000000,
      "rate": 1.005000000,
      "max_residual_us": 4000
    },
    {
      "last_device_us": 30000000,
      "offset_us": 1128000,
      "rate": 1.005000000,
      "max_residual_us": 9000
    }
  ],
  "samples": {
    "count": 15000,
    "messages": 625,
    "first_time_us": 1000000,
    "last_time_us": 31160000,
    "median_interval_us": 2010,
    "missing": 64,
    "measure_error": 12,
    "gaps": [
      {
        "after_sample": 23,
        "interval_us": 130000,
        "missing": 64
      }
    ]
  },
  "measure_errors": {
    "spans": [
      {
        "code": "f",
        "begin_us": 1000000,
        "end_us": 1046000,
        "samples": 24,
        "rgb_begin_s": -5.000,
        "rgb_end_s": -4.954
      }
    ],
    "rgb_window": {
      "begin_us": 6000000,
      "end_us": 35966667,
      "samples": 14990,
      "codes": [
        {
          "code": "s",
          "samples": 12,
          "longest_samples": 12,
          "runs": 1
        }
      ]
    }
  },
  "link": {
    "crc_failures": 1,
    "bad_lengths": 0,
    "implausible": 2,
    "skipped_bytes": 7
  },
  "events": [
    {
      "time_us": 1050000,
      "code": "s",
      "value": "v"
    },
    {
      "time_us": 39990000,
      "code": "s",
      "value": "u"
    }
  ]
}
)";
  EXPECT_EQ(hwlab::record::BuildPpgSidecarJson(s), expected);
}

// 표본이 없으면 시계 구간, 누락, 측정 오류 구간, 장치 상태가 빈 배열이고, RGB 녹화 구간과 장치 정보를 모르면 null입니다.
TEST(PpgSidecar, WritesEmptyArrays) {
  const std::string json = hwlab::record::BuildPpgSidecarJson({});

  EXPECT_NE(json.find("\"device_info\": null,"), std::string::npos) << json;
  EXPECT_NE(json.find("\"clock\": [],"), std::string::npos) << json;
  EXPECT_NE(json.find("\"gaps\": []\n"), std::string::npos) << json;
  EXPECT_NE(json.find("\"spans\": [],\n    \"rgb_window\": null\n"), std::string::npos) << json;
  EXPECT_NE(json.find("\"events\": []\n}"), std::string::npos) << json;
}

// RGB 녹화 구간을 모르면 측정 오류 구간의 RGB 기준 초를 null로 씁니다. 인쇄할 수 없는 값은 16진수로 씁니다.
TEST(PpgSidecar, WritesSpanWithoutRgbWindow) {
  hwlab::record::PpgSidecar s;
  s.error_spans = {{0x9a, {10}, {20}, 2}};
  s.events = {{{5}, 'e', 0x01}};

  const std::string json = hwlab::record::BuildPpgSidecarJson(s);

  EXPECT_NE(json.find("\"code\": \"0x9a\",\n        \"begin_us\": 10,\n        \"end_us\": 20,\n        \"samples\": 2,\n"
                      "        \"rgb_begin_s\": null,\n        \"rgb_end_s\": null\n"),
            std::string::npos)
      << json;
  EXPECT_NE(json.find("\"value\": \"0x01\""), std::string::npos) << json;
}

std::vector<hwlab::Timestamp> SampleTimes(size_t count) {
  std::vector<hwlab::Timestamp> times;
  for (size_t i = 0; i < count; ++i) times.push_back({static_cast<int64_t>(i) * 2000});
  return times;
}

// 같은 값이 이어진 표본을 구간 하나로 묶고, 값이 바뀌면 새 구간을 엽니다.
TEST(PpgErrors, FindsSpansOfSameCode) {
  const std::vector<uint8_t> errors = {0, 's', 's', 'f', 0, 0, 's', 's', 's', 0};
  const auto spans = hwlab::record::FindPpgErrorSpans(SampleTimes(errors.size()), errors);

  const std::vector<hwlab::record::PpgErrorSpan> expected = {
      {'s', {2000}, {4000}, 2}, {'f', {6000}, {6000}, 1}, {'s', {12000}, {16000}, 3}};
  EXPECT_EQ(spans, expected);
}

// 끝 표본까지 오류이면 마지막 구간이 끝 표본에서 닫힙니다. 오류가 없으면 구간이 없습니다.
TEST(PpgErrors, ClosesSpanAtLastSample) {
  const std::vector<uint8_t> errors = {0, 'p', 'p'};
  const auto spans = hwlab::record::FindPpgErrorSpans(SampleTimes(errors.size()), errors);

  ASSERT_EQ(spans.size(), 1u);
  EXPECT_EQ(spans[0].end.us, 4000);
  EXPECT_TRUE(hwlab::record::FindPpgErrorSpans(SampleTimes(3), std::vector<uint8_t>(3, 0)).empty());
}

// 구간 안 표본만 셉니다. 구간 경계에서 잘린 이어짐은 구간 안 부분만 길이로 셉니다.
TEST(PpgErrors, SummarizesOnlyInsideWindow) {
  const std::vector<uint8_t> errors = {'s', 's', 's', 0, 'f', 's', 's', 's', 0, 's'};
  const auto window = hwlab::record::SummarizePpgErrors(SampleTimes(errors.size()), errors, {4000}, {16000});

  EXPECT_EQ(window.begin.us, 4000);
  EXPECT_EQ(window.end.us, 16000);
  EXPECT_EQ(window.samples, 7);  // 표본 2~8
  const std::vector<hwlab::record::PpgErrorShare> expected = {{'f', 1, 1, 1}, {'s', 4, 3, 2}};
  EXPECT_EQ(window.codes, expected);
}

// 구간 안 표본이 없으면 표본 수 0, 값 목록이 빕니다.
TEST(PpgErrors, EmptyWindowHasNoCodes) {
  const std::vector<uint8_t> errors = {'s', 's'};
  const auto window = hwlab::record::SummarizePpgErrors(SampleTimes(errors.size()), errors, {10'000}, {20'000});

  EXPECT_EQ(window.samples, 0);
  EXPECT_TRUE(window.codes.empty());
}

}  // namespace
