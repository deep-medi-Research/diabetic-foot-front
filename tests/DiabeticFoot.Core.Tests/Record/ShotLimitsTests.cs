using DiabeticFoot.Core.Record;
using static DiabeticFoot.Core.Record.RecordFunctions;

namespace DiabeticFoot.Core.Tests.Record;

public class ShotLimitsTests
{
    private const long SidecarBytesPerFrame = ShotSizePlan.SidecarBytesPerFrame;

    // 폴더 홍길동_19551103/40/20260915_142233(31자)에 파일 두 개를 둔 배치입니다.
    private static ShotLayout MakeLayout() => new()
    {
        Folder = "홍길동_19551103/40/20260915_142233",
        Stem = "20260915_142233",
        Files = new[]
        {
            new ShotFile(FileRole.Manifest, "20260915_142233_shot.json"),
            new ShotFile(FileRole.ThermalRaw, "20260915_142233_thermal.raw16"),
        },
    };

    // 기본 촬영(좌우 발 640x1200, 900장, 열화상 640x480을 111111us 주기로)의 추정량입니다.
    [Fact]
    public void EstimatesFeetAndThermal()
    {
        var plan = new ShotSizePlan
        {
            RgbFrames = 900,
            RgbFrameRate = 30,
            RgbVideos = new[] { new VideoSize(640, 1200), new VideoSize(640, 1200) },
            Thermal = true,
            ThermalSize = new VideoSize(640, 480),
            ThermalRecordPeriodUs = 111111,
        };

        // 30초 / 111111us = 270장, 첫 장 1을 더해 271장입니다.
        long rgb = 2 * 900 * (640L * 1200 * 2 + SidecarBytesPerFrame);
        long thermal = 271 * (640L * 480 * 2 * 3 + SidecarBytesPerFrame);
        Assert.Equal(rgb + thermal, EstimateShotBytes(plan));
    }

    // 기록 주기가 0이면 장치 최대 Hz로 장 수를 세고, 열화상이 없으면 RGB만 셉니다.
    [Fact]
    public void ThermalFramesFollowRecordPeriod()
    {
        var plan = new ShotSizePlan
        {
            RgbFrames = 300,
            RgbFrameRate = 30,
            RgbVideos = new[] { new VideoSize(1920, 1080) },
        };
        long rgb = 300 * (1920L * 1080 * 2 + SidecarBytesPerFrame);
        Assert.Equal(rgb, EstimateShotBytes(plan));

        plan = plan with { Thermal = true, ThermalSize = new VideoSize(4, 2), ThermalMaxRateHz = 30 };
        Assert.Equal(rgb + 301 * (4 * 2 * 2 * 3 + SidecarBytesPerFrame), EstimateShotBytes(plan));

        plan = plan with { ThermalMaxRateHz = 0 };
        Assert.Equal(rgb, EstimateShotBytes(plan));
    }

    // 여유가 추정량과 예비량의 합 이상이어야 시작하고, 읽지 못하면 모름입니다. 경고 기준 미만이면 시작은 하되 낮음입니다.
    [Fact]
    public void JudgesFreeSpace()
    {
        Assert.Equal(FreeSpace.Enough, JudgeFreeSpace(1500, 1000, 500, 0).Verdict);
        Assert.Equal(FreeSpace.Short, JudgeFreeSpace(1499, 1000, 500, 0).Verdict);
        Assert.Equal(1500, JudgeFreeSpace(1499, 1000, 500, 0).RequiredBytes);
        Assert.Equal(FreeSpace.Unknown, JudgeFreeSpace(null, 1000, 500, 0).Verdict);
        Assert.Equal(FreeSpace.Enough, JudgeFreeSpace(0, 0, -5, 0).Verdict);
    }

    [Fact]
    public void WarnsWhenFreeSpaceIsBelowLowBoundary()
    {
        Assert.Equal(FreeSpace.Enough, JudgeFreeSpace(10_000, 1000, 500, 10_000).Verdict);
        Assert.Equal(FreeSpace.Low, JudgeFreeSpace(9999, 1000, 500, 10_000).Verdict);
        Assert.Equal(FreeSpace.Low, JudgeFreeSpace(1500, 1000, 500, 10_000).Verdict);
        Assert.Equal(FreeSpace.Short, JudgeFreeSpace(1499, 1000, 500, 10_000).Verdict);
        Assert.Equal(FreeSpace.Unknown, JudgeFreeSpace(null, 1000, 500, 10_000).Verdict);
        // 필요한 양이 경고 기준보다 크면 부족과 충분 사이에 낮음 구간이 없습니다.
        Assert.Equal(FreeSpace.Short, JudgeFreeSpace(12_000, 20_000, 500, 10_000).Verdict);
        Assert.Equal(FreeSpace.Enough, JudgeFreeSpace(20_500, 20_000, 500, 10_000).Verdict);
    }

    // 분석 이름이 촬영 폴더 이름이면 가장 긴 경로는 촬영 폴더 안의 분석 결과 그림 perfusion/v10/20260915_142233_pi_timecourse.png로
    // 40 + 47 = 87자입니다. results 폴더로 모은 결과(82자, 아래 테스트)보다 깁니다. 한글은 UTF-16 한 단위로 셉니다.
    [Fact]
    public void LongestPathCountsAnalysisResults()
    {
        var layout = MakeLayout();
        // "C:\\data"(7) + 구분자(1) + 폴더(31) + 구분자(1) = 40
        Assert.Equal(87, LongestShotPathLength("C:\\data", layout, "20260915_142233"));
        Assert.Equal(87, LongestShotPathLength("C:\\data\\", layout, "20260915_142233"));
        // 분석 이름이 없으면 가장 긴 파일 20260915_142233_thermal.raw16(29자)입니다.
        Assert.Equal(40 + 29, LongestShotPathLength("C:\\data", layout, ""));
    }

    // 분석결과 폴더로 모은 결과 경로(재분석 번호 99까지)도 상한 안에 들어가는지 셉니다.
    [Fact]
    public void LongestPathCoversCollectedResults()
    {
        var layout = MakeLayout();
        // "C:\\data"(7) + 구분자(1) + results(7) + 구분자(1) + 앞부분(31) + "_reanalysis99"(13) + "_v10"(4) + "_pi_timecourse.png"(18) = 82
        Assert.Equal(
            "홍길동_19551103_40_20260915_142233_reanalysis99",
            CollectedResultPrefix(layout, ShotLayout.MaxReanalysisNumber));
        Assert.True(LongestShotPathLength("C:\\data", layout, "20260915_142233") >= 82);
        // 분석 이름이 한 글자이면 촬영 폴더 안 결과 경로(40 + 9 + 1 + 3 + 1 + 1 + 18 = 73)보다 모은 결과 경로(82)가 깁니다.
        Assert.Equal(82, LongestShotPathLength("C:\\data", layout, "a"));
    }

    // U+10000 이상 문자는 UTF-16 두 단위입니다. 한글 저장 위치는 한 글자 한 단위입니다.
    [Fact]
    public void PathLengthUsesUtf16Units()
    {
        var layout = MakeLayout();
        int baseLength = LongestShotPathLength("D:", layout, "");
        Assert.Equal(baseLength + 2, LongestShotPathLength("D:\U0001F600", layout, ""));
        Assert.Equal(baseLength + 2, LongestShotPathLength("D:촬영", layout, ""));
    }
}
