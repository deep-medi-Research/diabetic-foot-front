using DiabeticFoot.Core.Pixel;
using DiabeticFoot.Core.Record;
using DiabeticFoot.Core.Timing;
using static DiabeticFoot.Core.Record.RecordFunctions;

namespace DiabeticFoot.Core.Tests.Record;

public class SidecarSummaryTests
{
    private static string ReplaceFirst(string text, string from, string to)
    {
        int at = text.IndexOf(from, StringComparison.Ordinal);
        Assert.NotEqual(-1, at);
        return text.Remove(at, from.Length).Insert(at, to);
    }

    // BuildSidecarJson이 쓴 열화상 사이드카에서 크기, 영역, 장 수, 누락 수를 읽습니다.
    [Fact]
    public void ReadsWhatBuildSidecarWrote()
    {
        var sidecar = new Sidecar
        {
            VideoFile = "142233_thermal.raw16",
            Device = "기기\"1\"\n",
            Stream = StreamKind.Thermal,
            Format = new VideoFormat(PixelFormat.Y16, 384, 288, 30),
            Crop = new Rect(0, 0, 384, 288),
            Clock = new ClockMap(Rate: 1.000123456),
            WrittenFrames = 275,
            WrittenInRgbWindow = 270,
            Gaps = new[] { new FrameGap(10, 99990, 2) },
            DroppedFrames = 1,
        };

        var summary = ReadSidecarSummary(BuildSidecarJson(sidecar), out string error);

        Assert.True(summary.HasValue, error);
        Assert.Equal(StreamKind.Thermal, summary.Value.Stream);
        Assert.Null(summary.Value.Foot);
        Assert.Equal(384, summary.Value.FrameWidth);
        Assert.Equal(288, summary.Value.FrameHeight);
        Assert.Equal(new Rect(0, 0, 384, 288), summary.Value.Crop);
        Assert.Equal(275, summary.Value.WrittenFrames);
        Assert.Equal(270, summary.Value.WrittenInRgbWindow);
        Assert.Equal(2, summary.Value.MissingFrames);
        Assert.Equal(1, summary.Value.DroppedFrames);
    }

    // 좌우 영상 사이드카의 발과 저장 영역을 읽습니다.
    [Fact]
    public void ReadsFootAndCrop()
    {
        var sidecar = new Sidecar
        {
            Format = new VideoFormat(PixelFormat.Uyvy, 1920, 1080, 30),
            Foot = FootSide.Right,
            Crop = new Rect(400, 0, 480, 1080),
        };

        var summary = ReadSidecarSummary(BuildSidecarJson(sidecar), out string error);

        Assert.True(summary.HasValue, error);
        Assert.Equal(StreamKind.Rgb, summary.Value.Stream);
        Assert.Equal(FootSide.Right, summary.Value.Foot);
        Assert.Equal(new Rect(400, 0, 480, 1080), summary.Value.Crop);
    }

    // 문법이 틀렸거나, 판이 낮거나, 필요한 키가 없으면 읽지 않고 이유를 남깁니다.
    [Fact]
    public void RejectsBrokenOrOldSidecar()
    {
        string good = BuildSidecarJson(new Sidecar());

        Assert.True(ReadSidecarSummary(good, out string error).HasValue, error);
        Assert.False(ReadSidecarSummary(good.Substring(0, good.Length / 2), out error).HasValue);
        Assert.NotEmpty(error);
        string old = ReplaceFirst(good, "\"schema_version\": 8", "\"schema_version\": 2");
        Assert.False(ReadSidecarSummary(old, out error).HasValue);
        string noWindow = ReplaceFirst(good, "written_in_rgb_window", "other_key_in_window__");
        Assert.False(ReadSidecarSummary(noWindow, out error).HasValue);
        Assert.False(ReadSidecarSummary("[]", out error).HasValue);
    }
}

public class ThermalFootRegionsTests
{
    // 영역을 주지 않으면 가운데 세로선에서 나누고, 화소 왼쪽 발에 따라 왼발과 오른발 영역을 바꿉니다.
    [Fact]
    public void SplitsAtCenterByPixelLeftFoot()
    {
        var rightOnLeft = ThermalFootRegions(384, 288, null, FootSide.Right);
        Assert.Equal(new Rect(192, 0, 192, 288), rightOnLeft.LeftFoot);
        Assert.Equal(new Rect(0, 0, 192, 288), rightOnLeft.RightFoot);

        var leftOnLeft = ThermalFootRegions(384, 288, null, FootSide.Left);
        Assert.Equal(new Rect(0, 0, 192, 288), leftOnLeft.LeftFoot);
        Assert.Equal(new Rect(192, 0, 192, 288), leftOnLeft.RightFoot);

        var odd = ThermalFootRegions(385, 288, null, FootSide.Left);
        Assert.Equal(new Rect(0, 0, 192, 288), odd.LeftFoot);
        Assert.Equal(new Rect(193, 0, 192, 288), odd.RightFoot);
    }

    // 영역을 직접 주면 그 값을 쓰고, 화소 왼쪽 발에 따라 순서만 바꿉니다.
    [Fact]
    public void UsesGivenRegions()
    {
        var given = (PixelLeft: new Rect(10, 20, 100, 200), PixelRight: new Rect(200, 20, 100, 200));

        var regions = ThermalFootRegions(384, 288, given, FootSide.Right);

        Assert.Equal(given.PixelRight, regions.LeftFoot);
        Assert.Equal(given.PixelLeft, regions.RightFoot);
    }
}

public class CheckAnalysisInputsTests
{
    private static readonly FileRole[] AllPresent =
    {
        FileRole.LeftVideo, FileRole.LeftSidecar, FileRole.RightVideo, FileRole.RightSidecar,
        FileRole.ThermalSidecar, FileRole.ThermalLeftArray, FileRole.ThermalRightArray, FileRole.Manifest,
    };

    private const long MinThermalFrames = AnalysisFiles.MinThermalFramesInRgbWindow;

    private static SidecarSummary FootVideo(FootSide foot) => new()
    {
        Foot = foot,
        FrameWidth = 1920,
        FrameHeight = 1080,
        Crop = new Rect(foot == FootSide.Left ? 960 : 400, 0, 480, 1080),
        WrittenFrames = 900,
        WrittenInRgbWindow = 900,
    };

    private static SidecarSummary Thermal(long inWindow) => new()
    {
        Stream = StreamKind.Thermal,
        FrameWidth = 384,
        FrameHeight = 288,
        Crop = new Rect(0, 0, 384, 288),
        WrittenFrames = inWindow + 2,
        WrittenInRgbWindow = inWindow,
    };

    private static AnalysisFiles GoodFiles() => new()
    {
        Present = AllPresent,
        Left = FootVideo(FootSide.Left),
        Right = FootVideo(FootSide.Right),
        Thermal = Thermal(270),
    };

    // 필요한 파일이 모두 있고 값이 기준을 넘으면 문제가 없습니다.
    [Fact]
    public void PassesCompleteShot()
    {
        Assert.Empty(CheckAnalysisInputs(GoodFiles(), MinThermalFrames));
    }

    // 없는 역할마다 문제를 하나씩 냅니다. 좌우 영상이 없으면 막고, 열화상 파일이 일부 없으면 경고입니다.
    [Fact]
    public void ReportsMissingRoles()
    {
        AnalysisFiles files = GoodFiles() with
        {
            Present = new[] { FileRole.LeftVideo, FileRole.LeftSidecar, FileRole.RightSidecar, FileRole.ThermalSidecar, FileRole.ThermalLeftArray },
        };

        var findings = CheckAnalysisInputs(files, MinThermalFrames);

        var expected = new[]
        {
            new InputFinding(InputProblem.MissingFile, FileRole.RightVideo, true, 0, 0),
            new InputFinding(InputProblem.MissingFile, FileRole.ThermalRightArray, false, 0, 0),
        };
        Assert.Equal(expected, findings);
        Assert.True(HasBlockingFinding(findings));
    }

    // 열화상 파일이 하나도 없어도 분석할 수 있으므로 막지 않는 경고 하나만 냅니다.
    [Fact]
    public void WarnsThermalAbsent()
    {
        AnalysisFiles files = GoodFiles() with
        {
            Present = new[] { FileRole.LeftVideo, FileRole.LeftSidecar, FileRole.RightVideo, FileRole.RightSidecar },
            Thermal = null,
        };

        var findings = CheckAnalysisInputs(files, MinThermalFrames);

        var expected = new[] { new InputFinding(InputProblem.ThermalAbsent, FileRole.ThermalSidecar, false, 0, 0) };
        Assert.Equal(expected, findings);
        Assert.False(HasBlockingFinding(findings));
    }

    // 열화상 사이드카 파일이 있는데 읽지 못하면 열화상만 빼는 경고입니다.
    [Fact]
    public void ReportsUnreadableSidecar()
    {
        AnalysisFiles files = GoodFiles() with { Thermal = null };

        var findings = CheckAnalysisInputs(files, MinThermalFrames);

        var expected = new[] { new InputFinding(InputProblem.UnreadableSidecar, FileRole.ThermalSidecar, false, 0, 0) };
        Assert.Equal(expected, findings);
    }

    // RGB 녹화 구간 안 열화상이 225장보다 적으면 열화상만 빼는 경고입니다. 225장은 통과입니다.
    [Fact]
    public void ReportsFewThermalFrames()
    {
        AnalysisFiles files = GoodFiles() with { Thermal = Thermal(224) };

        var findings = CheckAnalysisInputs(files, MinThermalFrames);

        var expected = new[] { new InputFinding(InputProblem.FewThermalFrames, FileRole.ThermalSidecar, false, 224, 225) };
        Assert.Equal(expected, findings);
        Assert.False(HasBlockingFinding(findings));
        files = files with { Thermal = Thermal(225) };
        Assert.Empty(CheckAnalysisInputs(files, MinThermalFrames));
    }

    // 열화상 누락은 분석을 막지 않는 경고입니다. 카메라 단 누락과 저장 누락을 더해 남깁니다.
    [Fact]
    public void WarnsThermalGap()
    {
        AnalysisFiles good = GoodFiles();
        AnalysisFiles files = good with { Thermal = good.Thermal!.Value with { MissingFrames = 3, DroppedFrames = 1 } };

        var findings = CheckAnalysisInputs(files, MinThermalFrames);

        var expected = new[] { new InputFinding(InputProblem.ThermalGap, FileRole.ThermalSidecar, false, 4, 0) };
        Assert.Equal(expected, findings);
        Assert.False(HasBlockingFinding(findings));
    }

    // 좌우 영상의 저장 영역이 세로로 길지 않으면(높이 <= 폭) 분석을 막습니다.
    [Fact]
    public void ReportsLandscapeVideo()
    {
        AnalysisFiles good = GoodFiles();
        AnalysisFiles files = good with { Right = good.Right!.Value with { Crop = new Rect(0, 0, 1080, 1080) } };

        var findings = CheckAnalysisInputs(files, MinThermalFrames);

        var expected = new[] { new InputFinding(InputProblem.NotPortrait, FileRole.RightVideo, true, 1080, 1080) };
        Assert.Equal(expected, findings);
    }
}
