using DiabeticFoot.Core.Pixel;
using static DiabeticFoot.Core.Pixel.PixelFunctions;

namespace DiabeticFoot.Core.Tests.Pixel;

public class FlickerTests
{
    private const long Pixels = 1920 * 1080;
    private const double Fps = 30.0;

    // 시각 0부터 t까지 PWM이 켜져 있던 시간입니다. 주기마다 앞쪽 duty만큼 켜집니다.
    private static double OnTime(double t, double pwmHz, double duty)
    {
        double period = 1.0 / pwmHz;
        double cycles = Math.Floor(t / period);
        return cycles * duty * period + Math.Min(t - cycles * period, duty * period);
    }

    // 프레임 i의 노출 구간 [i / fps, i / fps + exposure)에 들어온 빛을 영역 밝기 합계로 만듭니다. 켜짐 100%가 밝기 200입니다.
    private static List<long> RecordPwm(int frames, double pwmHz, double duty, double exposureS)
    {
        var sums = new List<long>();
        for (int i = 0; i < frames; ++i)
        {
            double start = i / Fps;
            double lit = (OnTime(start + exposureS, pwmHz, duty) - OnTime(start, pwmHz, duty)) / exposureS;
            sums.Add((long)Math.Round(lit * 200.0 * (double)Pixels, MidpointRounding.AwayFromZero));
        }
        return sums;
    }

    // PWM 994Hz, 노출 10ms(PWM 주기의 9.94배)에서 30fps 900장은 994 - 33 x 30 = 4Hz, 7.5프레임 주기로 오르내립니다.
    [Fact]
    public void FindsBeatPeriodWhenExposureMissesPwmPeriod()
    {
        List<long> sums = RecordPwm(900, 994.0, 0.5, 0.010);

        Flicker flicker = SummarizeFlicker(sums, Pixels, 0.5);

        Assert.NotNull(flicker.DominantPeriodFrames);
        Assert.Equal(7.5, flicker.DominantPeriodFrames.Value, 0.01);
        Assert.Equal(900, flicker.FrameCount);
        Assert.Equal(100.0, flicker.Mean, 0.5);
        Assert.True(flicker.PeakToPeak > 1.0);
        Assert.True(flicker.Stddev > 0.3);
    }

    // 정수가 아닌 주기(PWM 997Hz는 997 - 33 x 30 = 7Hz, 30 / 7 = 4.2857프레임)도 소수로 찾습니다.
    [Fact]
    public void FindsFractionalBeatPeriod()
    {
        List<long> sums = RecordPwm(900, 997.0, 0.3, 0.0123);

        Flicker flicker = SummarizeFlicker(sums, Pixels, 0.5);

        Assert.NotNull(flicker.DominantPeriodFrames);
        Assert.Equal(30.0 / 7.0, flicker.DominantPeriodFrames.Value, 0.01);
    }

    // 노출 10ms가 PWM 1000Hz 주기의 정확히 10배이면 프레임마다 켜짐 시간이 같아 주기가 없습니다.
    [Fact]
    public void NoPeriodWhenExposureIsPwmMultiple()
    {
        List<long> sums = RecordPwm(900, 1000.0, 0.5, 0.010);

        Flicker flicker = SummarizeFlicker(sums, Pixels, 0.5);

        Assert.Null(flicker.DominantPeriodFrames);
        Assert.Equal(100.0, flicker.Mean, 1e-6);
        Assert.True(flicker.PeakToPeak < 1e-5);
    }

    // 모든 프레임의 밝기가 같으면 주기가 없고 표준편차와 최대 최소 차가 0입니다.
    [Fact]
    public void NoPeriodForConstantBrightness()
    {
        long[] sums = Enumerable.Repeat(120 * Pixels, 900).ToArray();

        Flicker flicker = SummarizeFlicker(sums, Pixels, 0.5);

        Assert.Null(flicker.DominantPeriodFrames);
        Assert.Equal(120.0, flicker.Mean);
        Assert.Equal(0.0, flicker.Stddev);
        Assert.Equal(0.0, flicker.PeakToPeak);
    }

    // 주기 없는 잡음만 있으면 가장 큰 성분의 비율이 낮아 주기로 보지 않습니다.
    [Fact]
    public void NoPeriodForNoise()
    {
        var sums = new List<long>();
        uint state = 12345;
        for (int i = 0; i < 900; ++i)
        {
            state = state * 1664525u + 1013904223u;
            sums.Add(120 * Pixels + (long)(state >> 16) - 32768);
        }

        Flicker flicker = SummarizeFlicker(sums, Pixels, 0.5);

        Assert.Null(flicker.DominantPeriodFrames);
        Assert.True(flicker.Stddev > 0.0);
        Assert.True(flicker.PeriodicFraction < 0.1);
    }

    // 직선으로 밝아지는 추세 위의 주기도 추세에 가리지 않고 찾습니다.
    [Fact]
    public void IgnoresLinearDrift()
    {
        List<long> sums = RecordPwm(900, 994.0, 0.5, 0.010);
        for (int i = 0; i < sums.Count; ++i)
        {
            sums[i] += (long)i * Pixels / 100;
        }

        Flicker flicker = SummarizeFlicker(sums, Pixels, 0.5);

        Assert.NotNull(flicker.DominantPeriodFrames);
        Assert.Equal(7.5, flicker.DominantPeriodFrames.Value, 0.01);
    }

    // 이웃한 프레임끼리 번갈아 밝고 어두우면 2프레임 주기입니다.
    [Fact]
    public void FindsTwoFramePeriod()
    {
        var sums = new List<long>();
        for (int i = 0; i < 900; ++i)
        {
            sums.Add((i % 2 == 0 ? 110 : 100) * Pixels);
        }

        Flicker flicker = SummarizeFlicker(sums, Pixels, 0.5);

        Assert.NotNull(flicker.DominantPeriodFrames);
        Assert.Equal(2.0, flicker.DominantPeriodFrames.Value, 1e-6);
        Assert.Equal(1.0, flicker.PeriodicFraction, 1e-4);
        Assert.Equal(10.0, flicker.PeakToPeak);
    }

    // 4장보다 적으면 요약하지 않습니다.
    [Fact]
    public void TooFewFrames()
    {
        long[] sums = { 1, 2, 3 };

        Flicker flicker = SummarizeFlicker(sums, 1, 0.5);

        Assert.Equal(3, flicker.FrameCount);
        Assert.Null(flicker.DominantPeriodFrames);
    }
}

public class LightResponseTests
{
    // 시험용 기준입니다. 실측값이 아닙니다.
    private static readonly LightResponseRule Rule = new() { AppliedFrame = 2, WindowFrames = 3, MinMeanChange = 2.0 };

    // 밝기를 올렸을 때 반영 뒤 평균이 기준 이상 오르면 반영으로 봅니다. 명령 전은 마지막 3장, 반영 뒤는 처음 3장만 씁니다.
    [Fact]
    public void AppliedWhenBrightnessFollowsCommand()
    {
        double[] before = { 10.0, 50.0, 60.0, 60.0, 60.0 };
        double[] after = { 63.0, 62.0, 62.0, 0.0 };

        Assert.Equal(LightResponse.Applied, JudgeLightResponse(before, after, 20, Rule));
    }

    // 밝기를 내렸으면 평균이 내려가야 반영입니다.
    [Fact]
    public void AppliedWhenDimmingLowersBrightness()
    {
        double[] before = { 80.0, 80.0, 80.0 };
        double[] after = { 70.0, 71.0, 72.0 };

        Assert.Equal(LightResponse.Applied, JudgeLightResponse(before, after, -30, Rule));
    }

    // 변화가 기준보다 작거나 반대 방향이면 보이지 않은 것으로 봅니다.
    [Fact]
    public void NotSeenWhenChangeTooSmallOrOpposite()
    {
        double[] before = { 60.0, 60.0, 60.0 };
        double[] small = { 61.0, 61.0, 61.0 };
        double[] darker = { 50.0, 50.0, 50.0 };

        Assert.Equal(LightResponse.NotSeen, JudgeLightResponse(before, small, 10, Rule));
        Assert.Equal(LightResponse.NotSeen, JudgeLightResponse(before, darker, 10, Rule));
    }

    // 기준값이 없거나 프레임이 모자라거나 보낸 변화가 0이면 판정하지 않습니다.
    [Fact]
    public void UnknownWithoutRuleFramesOrChange()
    {
        double[] before = { 60.0, 60.0, 60.0 };
        double[] after = { 90.0, 90.0, 90.0 };

        Assert.Equal(LightResponse.Unknown, JudgeLightResponse(before, after, 10, new LightResponseRule()));
        Assert.Equal(LightResponse.Unknown, JudgeLightResponse(new[] { 60.0, 60.0 }, after, 10, Rule));
        Assert.Equal(LightResponse.Unknown, JudgeLightResponse(before, new[] { 90.0 }, 10, Rule));
        Assert.Equal(LightResponse.Unknown, JudgeLightResponse(before, after, 0, Rule));
    }
}
