using DiabeticFoot.Core;
using DiabeticFoot.Core.Pixel;
using DiabeticFoot.Core.Record;
using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

// 원본 test_perfusion_summary.cc의 ShotReview 시험입니다(결과 창이 읽는 매니페스트, 사이드카, 열화상 원시값).
public class ShotReviewTests
{
    // 실제 촬영 102528의 매니페스트 files 항목에서 영상, 사이드카, 원시값 파일 이름을 읽습니다.
    [Fact]
    public void ReadsManifestFiles()
    {
        var files = ShotReview.ReadShotManifest("""
            {"schema_version": 3, "pixel_left_foot": "right", "files": {
              "left_video": "102528_left.mkv", "left_sidecar": "102528_left.json",
              "right_video": "102528_right.mkv", "right_sidecar": "102528_right.json",
              "thermal_raw": "102528_thermal.raw16", "thermal_sidecar": "102528_thermal.json",
              "thermal_preview": "102528_thermal.avi", "manifest": "102528_shot.json"},
              "analysis_inputs": ["left_video", "right_video"]}
            """, out var error);
        Assert.True(files is not null, error);
        Assert.Equal("102528_left.mkv", files!.LeftVideo);
        Assert.Equal("102528_right.json", files.RightSidecar);
        Assert.Equal("102528_thermal.raw16", files.ThermalRaw);
        Assert.Equal("102528_thermal.json", files.ThermalSidecar);
        Assert.Equal(FootSide.Right, files.PixelLeftFoot);
        // 열화상 없이 찍은 촬영은 열화상 이름이 비고, 영상 이름이 없으면 읽지 않습니다.
        var rgbOnly = ShotReview.ReadShotManifest("""{"files": {"left_video": "a.mkv", "right_video": "b.mkv"}}""");
        Assert.NotNull(rgbOnly);
        Assert.Equal("", rgbOnly!.ThermalRaw);
        Assert.Null(rgbOnly.PixelLeftFoot);
        Assert.Null(ShotReview.ReadShotManifest("""{"files": {"left_video": "a.mkv"}}""", out error));
        Assert.Contains("files.left_video", error);
    }

    // 사이드카에서 크기, 프레임별 공통 시각, 표시 범위를 읽습니다(실제 촬영 102528 열화상 사이드카의 앞부분 값).
    [Fact]
    public void ReadsSidecarFrames()
    {
        var thermal = ShotReview.ReadSidecarFrames("""
            {"format": {"width": 640, "height": 480},
             "display_range": {"low": 9418, "high": 9666},
             "per_frame": {"index": [2065, 2069], "common_time_us": [26441960429443, 26441960562713]}}
            """);
        Assert.NotNull(thermal);
        Assert.Equal(640, thermal!.Width);
        Assert.Equal(480, thermal.Height);
        Assert.Equal(2, thermal.CommonTime.Count);
        Assert.Equal(26441960562713, thermal.CommonTime[1].Us);
        Assert.NotNull(thermal.DisplayRange);
        Assert.Equal(9418, thermal.DisplayRange!.Value.Low);
        Assert.Equal(9666, thermal.DisplayRange.Value.High);
        var rgb = ShotReview.ReadSidecarFrames(
            """{"format": {"width": 640, "height": 1080}, "display_range": null, "per_frame": {"common_time_us": []}}""");
        Assert.NotNull(rgb);
        Assert.Null(rgb!.DisplayRange);
        Assert.Null(ShotReview.ReadSidecarFrames("""{"format": {"width": 640, "height": 480}}"""));
        Assert.Null(ShotReview.ReadSidecarFrames("""{"format": {"width": 640, "height": 480}, "per_frame": {"common_time_us": [1.5]}}"""));
        // 열화상 사이드카에는 카메라 설정 기록이 없습니다.
        Assert.Empty(thermal.Camera.Mode);
        Assert.Null(thermal.Camera.ExposureStep);
        Assert.Null(thermal.FrameRate);
    }

    // 촬영 때 입력한 기준과 조명을 매니페스트에서 읽습니다(실제 촬영 20261028_160307의 값).
    [Fact]
    public void ReadsManifestCriterionAndLight()
    {
        var files = ShotReview.ReadShotManifest("""
            {"criterion": 40,
             "lighting": {"source": "external", "product": "4000K COB LED, 12V", "brightness_percent": null},
             "files": {"left_video": "a.mkv", "right_video": "b.mkv"}}
            """);
        Assert.NotNull(files);
        Assert.Equal(40, files!.Criterion);
        Assert.Equal("external", files.LightSource);
        Assert.Equal("4000K COB LED, 12V", files.LightProduct);
        // 기준을 고르지 않은 촬영(-1)과 기준이 없는 이전 매니페스트는 null입니다.
        var unset = ShotReview.ReadShotManifest("""{"criterion": -1, "files": {"left_video": "a.mkv", "right_video": "b.mkv"}}""");
        Assert.NotNull(unset);
        Assert.Null(unset!.Criterion);
        Assert.Empty(unset.LightProduct);
        Assert.Empty(unset.AnkleLevel);
    }

    // 발목 혈압 단계를 매니페스트 schema 6의 blood_pressure에서 읽습니다(실기 촬영 20261030_100959의 값).
    [Fact]
    public void ReadsManifestAnkleLevel()
    {
        var files = ShotReview.ReadShotManifest("""
            {"schema_version": 6, "criterion": 40,
             "blood_pressure": {"arm": {"systolic_mmhg": 120, "diastolic_mmhg": 80}, "ankle": {"systolic_mmhg": 135, "diastolic_mmhg": 84}, "ankle_level": {"name": "D", "pressure_mmhg": 84}},
             "files": {"left_video": "a.avi", "right_video": "b.avi"}}
            """);
        Assert.NotNull(files);
        Assert.Equal("D", files!.AnkleLevel);
        Assert.Equal(40, files.Criterion);
    }

    // RGB 사이드카의 카메라 설정을 읽습니다(실제 촬영 20261028_160307 왼발 사이드카의 값).
    [Fact]
    public void ReadsSidecarCameraSettings()
    {
        var rgb = ShotReview.ReadSidecarFrames("""
            {"format": {"pixel_format": "UYVY", "width": 774, "height": 1080,
              "frame_rate": 30, "codec": "ffv1"},
             "settings": {"mode": "manual", "exposure_auto": false, "exposure_step": -9, "exposure_ms": 1.953, "gain": 1,
               "white_balance_auto": false, "white_balance_k": 4000, "sdk_frame_rate": 30, "locked": true},
             "per_frame": {"common_time_us": [1, 2]}}
            """);
        Assert.NotNull(rgb);
        Assert.Equal(30, rgb!.FrameRate);
        Assert.Equal("manual", rgb.Camera.Mode);
        Assert.Equal(-9, rgb.Camera.ExposureStep);
        Assert.Equal(1.953, rgb.Camera.ExposureMs);
        Assert.Equal(1, rgb.Camera.Gain);
        Assert.Equal(false, rgb.Camera.WhiteBalanceAuto);
        Assert.Equal(4000, rgb.Camera.WhiteBalanceK);
        Assert.Equal(true, rgb.Camera.Locked);
    }

    // 지금 앱이 쓰는 사이드카(schema 8, 조명 기록과 깜빡임 요약 포함)를 결과 창(ReadSidecarFrames)과
    // 분석 요청(RecordFunctions.ReadSidecarSummary)이 모두 읽습니다.
    [Fact]
    public void ReadsCurrentSidecarSchemaWithLight()
    {
        var records = new List<FrameRecord>();
        for (long i = 0; i < 3; ++i)
        {
            var tUs = i * 33_333;
            records.Add(new FrameRecord((ulong)i, new Timestamp(1_000_000 + tUs), new Timestamp(2_000_000 + tUs), 100, 0));
        }
        var rgb = new Sidecar
        {
            Format = new VideoFormat(PixelFormat.Uyvy, 640, 1080, 30),
            Foot = FootSide.Left,
            Crop = new Rect(0, 0, 640, 1080),
            WrittenFrames = 3,
            WrittenInRgbWindow = 3,
            Records = records,
            Light = new LightRecord
            {
                Source = LightSource.App,
                Percent = 70,
                UsbVendorId = 0x0403,
                UsbProductId = 0x6001,
                Serial = "A50285BIA",
                Confirmed = true,
                Flicker = new Flicker { FrameCount = 3, Mean = 1.0, DominantPeriodFrames = 2.5 },
            },
        };
        var rgbJson = RecordFunctions.BuildSidecarJson(rgb);
        Assert.Contains("\"schema_version\": 8", rgbJson);

        var frames = ShotReview.ReadSidecarFrames(rgbJson, out var error);
        Assert.True(frames is not null, error);
        Assert.Equal(640, frames!.Width);
        Assert.Equal(3, frames.CommonTime.Count);
        Assert.Equal(1_066_666, frames.CommonTime[2].Us);
        Assert.Null(frames.DisplayRange);
        var summary = RecordFunctions.ReadSidecarSummary(rgbJson, out error);
        Assert.True(summary.HasValue, error);
        Assert.Equal(3, summary!.Value.WrittenFrames);
        Assert.Equal(FootSide.Left, summary.Value.Foot);

        // 열화상 사이드카는 조명이 null이고 표시 범위가 있습니다.
        var thermal = new Sidecar
        {
            Stream = StreamKind.Thermal,
            Format = new VideoFormat(PixelFormat.Y16, 640, 480, 9),
            Crop = new Rect(0, 0, 640, 480),
            DisplayRange = new Y16Range(9418, 9666),
            Records = [new FrameRecord(0, new Timestamp(5), new Timestamp(5), 0, 0)],
        };
        var thermalJson = RecordFunctions.BuildSidecarJson(thermal);
        var thermalFrames = ShotReview.ReadSidecarFrames(thermalJson, out error);
        Assert.True(thermalFrames is not null, error);
        Assert.NotNull(thermalFrames!.DisplayRange);
        Assert.Equal(9666, thermalFrames.DisplayRange!.Value.High);
        Assert.True(RecordFunctions.ReadSidecarSummary(thermalJson, out error).HasValue, error);
    }

    // 원시값 파일에서 번호 i의 프레임을 리틀 엔디언으로 읽고, 범위 밖 번호는 읽지 않습니다.
    [Fact]
    public void ReadsRaw16FrameByNumber()
    {
        var path = Path.Combine(Path.GetTempPath(), "diabeticfoot_test_review.raw16");
        var bytes = new List<byte>();
        for (var frame = 0; frame < 3; ++frame)
        {
            for (var i = 0; i < 6; ++i)
            {
                var v = (ushort)(frame * 1000 + i + 0x100);
                bytes.Add((byte)(v & 0xFF));
                bytes.Add((byte)(v >> 8));
            }
        }
        File.WriteAllBytes(path, bytes.ToArray());
        var values = Array.Empty<ushort>();
        Assert.True(ShotReview.ReadRaw16Frame(path, 3, 2, 2, ref values));
        Assert.Equal(6, values.Length);
        Assert.Equal(2000 + 0x100, values[0]);
        Assert.Equal(2005 + 0x100, values[5]);
        Assert.False(ShotReview.ReadRaw16Frame(path, 3, 2, 3, ref values));
        Assert.False(ShotReview.ReadRaw16Frame(path, 3, 2, -1, ref values));
        Assert.False(ShotReview.ReadRaw16Frame(Path.Combine(Path.GetTempPath(), "diabeticfoot_missing.raw16"), 3, 2, 0, ref values));
    }
}
