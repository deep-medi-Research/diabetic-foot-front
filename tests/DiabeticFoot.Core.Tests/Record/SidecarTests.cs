using DiabeticFoot.Core.Pixel;
using DiabeticFoot.Core.Record;
using DiabeticFoot.Core.Timing;
using static DiabeticFoot.Core.Record.RecordFunctions;

namespace DiabeticFoot.Core.Tests.Record;

public class SidecarTests
{
    private static Timestamp T(long us) => new(us);

    private static string Lf(string text) => text.ReplaceLineEndings("\n");

    private static Sidecar MakeSidecar() => new()
    {
        VideoFile = "142233_left.avi",
        Device = "See3CAM_24CUG",
        Format = new VideoFormat(PixelFormat.Uyvy, 960, 1080, 30),
        Codec = "ffv1",
        Foot = FootSide.Left,
        Crop = new Rect(0, 0, 960, 1080),
        Mode = SettingMode.Manual,
        Settings = new CameraState
        {
            ExposureAuto = false,
            ExposureStep = -5,
            Gain = 4,
            WhiteBalanceAuto = false,
            WhiteBalanceK = 4600,
            SdkFrameRate = 30,
            Locked = true,
        },
        TargetFrames = 900,
        WrittenFrames = 900,
        FirstFrame = T(1000),
        LastFrame = T(30001000),
    };

    // 키 순서와 들여쓰기까지 고정된 바이트를 냅니다.
    [Fact]
    public void WritesFixedLayout()
    {
        Sidecar s = MakeSidecar() with
        {
            Gaps = new[] { new FrameGap(120, 100000, 2) },
            FrameRate = new FrameRateCheck { SlowIntervals = 3, Spans = new[] { new SlowSpan(4000000, 4120000, 3) }, MinFps = 25.04 },
            BrightnessPixels = 960 * 1080,
            ClipLevel = 250,
            Records = new[]
            {
                new FrameRecord(0, T(1000), T(1500), 100, 0),
                new FrameRecord(1, T(34333), T(34900), 101, 3),
                new FrameRecord(4, T(134333), T(134800), 102, 0),
            },
        };

        string expected = Lf("""
{
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
""") + "\n";
        Assert.Equal(expected, BuildSidecarJson(s));
    }

    // 자동 맞춤에서 읽지 못한 노출값은 null로 남깁니다.
    [Fact]
    public void WritesNullForUnknownExposure()
    {
        Sidecar s = MakeSidecar() with { Mode = SettingMode.Auto };
        s = s with { Settings = s.Settings with { ExposureStep = null } };

        string json = BuildSidecarJson(s);

        Assert.Contains("\"mode\": \"auto\"", json);
        Assert.Contains("\"exposure_step\": null", json);
        Assert.Contains("\"exposure_ms\": null", json);
    }

    // 열화상 사이드카의 렌즈 셰이딩 보정 기록은 source(none, loaded, corrected, failed)와 file_unix_s이고, 없으면 null입니다.
    [Fact]
    public void WritesLensShading()
    {
        Sidecar s = MakeSidecar() with { Stream = StreamKind.Thermal };
        Assert.Contains("\"lens_shading\": null,", BuildSidecarJson(s));
        s = s with { LensShading = new LensShadingRecord(LensShadingSource.Loaded, 1761205212) };
        Assert.Contains(
            Lf("""
"lens_shading": {
    "source": "loaded",
    "file_unix_s": 1761205212
  },
"""),
            BuildSidecarJson(s));
        s = s with { LensShading = new LensShadingRecord(LensShadingSource.None, null) };
        Assert.Contains(
            Lf("""
"source": "none",
    "file_unix_s": null
"""),
            BuildSidecarJson(s));
        s = s with { LensShading = s.LensShading!.Value with { Source = LensShadingSource.Corrected } };
        Assert.Contains("\"source\": \"corrected\"", BuildSidecarJson(s));
        s = s with { LensShading = s.LensShading!.Value with { Source = LensShadingSource.Failed } };
        Assert.Contains("\"source\": \"failed\"", BuildSidecarJson(s));
    }

    // 누락이 없으면 빈 배열입니다.
    [Fact]
    public void WritesEmptyGaps()
    {
        Assert.Contains("\"gaps\": []", BuildSidecarJson(MakeSidecar()));
    }

    // 900장의 프레임별 기록은 값마다 900개 숫자가 든 배열 하나가 됩니다.
    [Fact]
    public void WritesNineHundredFrameRecords()
    {
        var records = new List<FrameRecord>();
        for (long i = 0; i < 900; i++)
        {
            records.Add(new FrameRecord((ulong)i, T(i * 33333), T(i * 33333 + 400), i, 0));
        }
        Sidecar s = MakeSidecar() with { Records = records };

        string json = BuildSidecarJson(s);

        int at = json.IndexOf("\"device_time_us\": [", StringComparison.Ordinal);
        Assert.NotEqual(-1, at);
        int end = json.IndexOf(']', at);
        int commas = 0;
        for (int i = at; i < end; i++)
        {
            commas += json[i] == ',' ? 1 : 0;
        }
        Assert.Equal(899, commas);
        Assert.Contains("29966367]", json);  // 마지막 장치 시각 899 * 33333
    }

    // 따옴표, 역슬래시, 제어 문자는 이스케이프하고 한글은 그대로 둡니다.
    [Fact]
    public void EscapesStringsAndKeepsUtf8()
    {
        Sidecar s = MakeSidecar() with { VideoFile = "발\"a\\b\n\u0001.avi" };

        string json = BuildSidecarJson(s);

        Assert.Contains("\"video_file\": \"발\\\"a\\\\b\\n\\u0001.avi\"", json);
    }

    // 열화상 사이드카는 스트림 종류와 시계 식을 남기고 공통 시각을 시계 식으로 계산합니다. 조명 기록은 null입니다.
    [Fact]
    public void WritesStreamLightAndCommonTime()
    {
        Sidecar s = MakeSidecar() with
        {
            Stream = StreamKind.Thermal,
            Clock = new ClockMap(1_234_000, 1.0001, 812),
            Records = new[]
            {
                new FrameRecord(0, T(0), T(1_235_000), 0, 0),
                new FrameRecord(1, T(1_000_000), T(2_335_000), 0, 0),
            },
        };

        string json = BuildSidecarJson(s);

        Assert.Contains("\"stream\": \"thermal\"", json);
        Assert.Contains("\"light\": null", json);
        Assert.Contains("\"offset_us\": 1234000", json);
        Assert.Contains("\"rate\": 1.000100000", json);
        Assert.Contains("\"max_residual_us\": 812", json);
        // 1'234'000 + 1.0001 * 1'000'000
        Assert.Contains("\"common_time_us\": [1234000, 2234100]", json);
    }

    // 열화상 사이드카는 확인용 영상의 표시 범위(원시값), 받은 장 수, 기록 주기, RGB 녹화 구간 안의 기록 장 수를 남깁니다.
    [Fact]
    public void WritesThermalRangeAndCounts()
    {
        Sidecar s = MakeSidecar() with
        {
            Stream = StreamKind.Thermal,
            DisplayRange = new Y16Range(7012, 8960),
            WrittenFrames = 270,
            ReceivedFrames = 812,
            RecordPeriodUs = 111111,
            WrittenInRgbWindow = 268,
        };

        string json = BuildSidecarJson(s);

        Assert.Contains("  \"display_range\": {\n    \"low\": 7012,\n    \"high\": 8960\n  },\n  \"frames\"", json);
        Assert.Contains(
            "\"written\": 270,\n    \"received\": 812,\n    \"record_period_us\": 111111,\n" +
            "    \"written_in_rgb_window\": 268,",
            json);
    }

    // 앱이 보낸 조명은 출처, 밝기, VID:PID, 일련번호, 사람 확인, 반영 판정, 깜빡임 요약을 고정된 바이트로 남깁니다.
    [Fact]
    public void WritesAppLightBytes()
    {
        var light = new LightRecord
        {
            Source = LightSource.App,
            Percent = 70,
            UsbVendorId = 0x0403,
            UsbProductId = 0x6001,
            Serial = "A50285BIA",
            Confirmed = true,
            Response = LightResponse.Applied,
            Flicker = new Flicker
            {
                FrameCount = 900,
                Mean = 101.25,
                Stddev = 0.5,
                PeakToPeak = 1.75,
                PeriodicFraction = 0.93126,
                DominantPeriodFrames = 12.5,
            },
        };
        Sidecar s = MakeSidecar() with { Light = light };

        string json = BuildSidecarJson(s);

        string expected = Lf("""
  "foot": "left",
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
  "format": {
""");
        Assert.Contains(expected, json);
    }

    // 앱 밖에서 켠 조명은 밝기, 포트, 일련번호가 null이고 판정은 unknown입니다. 주기가 없으면 주기만 null입니다.
    [Fact]
    public void WritesExternalLightWithNulls()
    {
        var light = new LightRecord { Source = LightSource.External, Flicker = new Flicker { FrameCount = 3 } };
        Sidecar s = MakeSidecar() with { Light = light };

        string json = BuildSidecarJson(s);

        string expected = Lf("""
  "light": {
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
  },
""");
        Assert.Contains(expected, json);
    }
}

public class PpgSidecarTests
{
    private static Timestamp T(long us) => new(us);

    private static string Lf(string text) => text.ReplaceLineEndings("\n");

    // PPG 사이드카는 장치 정보, 시계 구간마다 식, 표본 수와 누락, 통신 집계, 장치 상태를 키 순서와 들여쓰기까지 고정된 바이트로 남깁니다.
    [Fact]
    public void WritesFixedLayout()
    {
        var s = new PpgSidecar
        {
            SamplesFile = "a_ppg.csv",
            Port = "COM6",
            DeviceInfo = "100002280280320",
            SampleRateHz = 500,
            RecordBegin = T(1000),
            RecordEnd = T(40_000_000),
            Clock = new[]
            {
                new ClockSegment(T(46_000), new ClockMap(1_000_000, 1.005, 4_000)),
                new ClockSegment(T(30_000_000), new ClockMap(1_128_000, 1.005, 9_000)),
            },
            Samples = 15_000,
            Messages = 625,
            FirstSample = T(1_000_000),
            LastSample = T(31_160_000),
            MedianIntervalUs = 2010,
            Gaps = new[] { new FrameGap(23, 130_000, 64) },
            MeasureErrorSamples = 12,
            CrcFailures = 1,
            Implausible = 2,
            SkippedBytes = 7,
            Events = new[]
            {
                new PpgEvent(T(1_050_000), (byte)'s', (byte)'v'),
                new PpgEvent(T(39_990_000), (byte)'s', (byte)'u'),
            },
            ErrorSpans = new[] { new PpgErrorSpan((byte)'f', T(1_000_000), T(1_046_000), 24) },
            RgbWindow = new PpgErrorWindow
            {
                Begin = T(6_000_000),
                End = T(35_966_667),
                Samples = 14_990,
                Codes = new[] { new PpgErrorShare((byte)'s', 12, 12, 1) },
            },
        };

        string expected = Lf("""
{
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
""") + "\n";
        Assert.Equal(expected, BuildPpgSidecarJson(s));
    }

    // 표본이 없으면 시계 구간, 누락, 측정 오류 구간, 장치 상태가 빈 배열이고, RGB 녹화 구간과 장치 정보를 모르면 null입니다.
    [Fact]
    public void WritesEmptyArrays()
    {
        string json = BuildPpgSidecarJson(new PpgSidecar());

        Assert.Contains("\"device_info\": null,", json);
        Assert.Contains("\"clock\": [],", json);
        Assert.Contains("\"gaps\": []\n", json);
        Assert.Contains("\"spans\": [],\n    \"rgb_window\": null\n", json);
        Assert.Contains("\"events\": []\n}", json);
    }

    // RGB 녹화 구간을 모르면 측정 오류 구간의 RGB 기준 초를 null로 씁니다. 인쇄할 수 없는 값은 16진수로 씁니다.
    [Fact]
    public void WritesSpanWithoutRgbWindow()
    {
        var s = new PpgSidecar
        {
            ErrorSpans = new[] { new PpgErrorSpan(0x9a, T(10), T(20), 2) },
            Events = new[] { new PpgEvent(T(5), (byte)'e', 0x01) },
        };

        string json = BuildPpgSidecarJson(s);

        Assert.Contains(
            "\"code\": \"0x9a\",\n        \"begin_us\": 10,\n        \"end_us\": 20,\n        \"samples\": 2,\n" +
            "        \"rgb_begin_s\": null,\n        \"rgb_end_s\": null\n",
            json);
        Assert.Contains("\"value\": \"0x01\"", json);
    }
}

public class PpgErrorsTests
{
    private static Timestamp T(long us) => new(us);

    private static Timestamp[] SampleTimes(int count)
    {
        var times = new Timestamp[count];
        for (int i = 0; i < count; i++) times[i] = T(i * 2000L);
        return times;
    }

    private static byte[] Codes(params object[] values) =>
        values.Select(v => v is char c ? (byte)c : Convert.ToByte(v)).ToArray();

    // 같은 값이 이어진 표본을 구간 하나로 묶고, 값이 바뀌면 새 구간을 엽니다.
    [Fact]
    public void FindsSpansOfSameCode()
    {
        byte[] errors = Codes(0, 's', 's', 'f', 0, 0, 's', 's', 's', 0);
        var spans = FindPpgErrorSpans(SampleTimes(errors.Length), errors);

        var expected = new[]
        {
            new PpgErrorSpan((byte)'s', T(2000), T(4000), 2),
            new PpgErrorSpan((byte)'f', T(6000), T(6000), 1),
            new PpgErrorSpan((byte)'s', T(12000), T(16000), 3),
        };
        Assert.Equal(expected, spans);
    }

    // 끝 표본까지 오류이면 마지막 구간이 끝 표본에서 닫힙니다. 오류가 없으면 구간이 없습니다.
    [Fact]
    public void ClosesSpanAtLastSample()
    {
        byte[] errors = Codes(0, 'p', 'p');
        var spans = FindPpgErrorSpans(SampleTimes(errors.Length), errors);

        var span = Assert.Single(spans);
        Assert.Equal(4000, span.End.Us);
        Assert.Empty(FindPpgErrorSpans(SampleTimes(3), new byte[3]));
    }

    // 구간 안 표본만 셉니다. 구간 경계에서 잘린 이어짐은 구간 안 부분만 길이로 셉니다.
    [Fact]
    public void SummarizesOnlyInsideWindow()
    {
        byte[] errors = Codes('s', 's', 's', 0, 'f', 's', 's', 's', 0, 's');
        var window = SummarizePpgErrors(SampleTimes(errors.Length), errors, T(4000), T(16000));

        Assert.Equal(4000, window.Begin.Us);
        Assert.Equal(16000, window.End.Us);
        Assert.Equal(7, window.Samples);  // 표본 2~8
        var expected = new[] { new PpgErrorShare((byte)'f', 1, 1, 1), new PpgErrorShare((byte)'s', 4, 3, 2) };
        Assert.Equal(expected, window.Codes);
    }

    // 구간 안 표본이 없으면 표본 수 0, 값 목록이 빕니다.
    [Fact]
    public void EmptyWindowHasNoCodes()
    {
        byte[] errors = Codes('s', 's');
        var window = SummarizePpgErrors(SampleTimes(errors.Length), errors, T(10_000), T(20_000));

        Assert.Equal(0, window.Samples);
        Assert.Empty(window.Codes);
    }
}
