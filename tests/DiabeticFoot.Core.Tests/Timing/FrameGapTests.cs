using DiabeticFoot.Core.Tests.Support;
using DiabeticFoot.Core.Timing;
using static DiabeticFoot.Core.Timing.TimingFunctions;

namespace DiabeticFoot.Core.Tests.Timing;

public class FrameGapTests
{
    private const long Period30FpsUs = 33333;

    private static Timestamp[] EvenTimes(int count, long periodUs)
    {
        var times = new Timestamp[count];
        for (int i = 0; i < count; i++)
        {
            times[i] = new Timestamp(i * periodUs);
        }
        return times;
    }

    private static void Shift(Timestamp[] times, int from, long deltaUs)
    {
        for (int i = from; i < times.Length; i++)
        {
            times[i] = new Timestamp(times[i].Us + deltaUs);
        }
    }

    // 간격이 고른 프레임에는 누락이 없습니다.
    [Fact]
    public void FindsNothingWhenIntervalsAreEven()
    {
        var times = EvenTimes(900, Period30FpsUs);

        var gaps = FindFrameGaps(times, Period30FpsUs);

        Assert.Empty(gaps);
    }

    // 60fps 실측에서 본 117ms 간격은 6프레임 누락입니다.
    [Fact]
    public void Counts117msGapAtSixtyFpsAsSixMissing()
    {
        const long Period60FpsUs = 16667;
        var times = EvenTimes(10, Period60FpsUs);
        Shift(times, 5, 100000);

        var gaps = FindFrameGaps(times, Period60FpsUs);

        var gap = Assert.Single(gaps);
        Assert.Equal(4, gap.Before);
        Assert.Equal(116667, gap.IntervalUs);
        Assert.Equal(6, gap.MissingFrames);
        Assert.Equal(6, CountMissingFrames(gaps));
    }

    // 실측 녹화(1080p 60fps 3분)에서 lab 분석과 같은 누락(26곳, 160프레임)을 찾습니다.
    [Fact]
    public void MatchesMeasuredSee3CamRecording()
    {
        var times = new List<Timestamp>();
        foreach (string line in File.ReadLines(TestData.Path("see3cam_1080p60_3min_sample_us.txt")))
        {
            if (line.Length > 0 && line[0] != '#')
            {
                times.Add(new Timestamp(long.Parse(line)));
            }
        }
        Assert.Equal(10800, times.Count);

        var gaps = FindFrameGaps(times, 16667);

        Assert.Equal(26, gaps.Count);
        Assert.Equal(160, CountMissingFrames(gaps));
    }

    // 주기의 1.5배까지의 흔들림은 누락이 아닙니다.
    [Fact]
    public void IgnoresJitterUpToOneAndHalfPeriods()
    {
        var times = new[] { new Timestamp(0), new Timestamp(32000), new Timestamp(80000), new Timestamp(112000) };

        var gaps = FindFrameGaps(times, 32000);

        Assert.Empty(gaps);
    }

    // 주기를 모르면 판정하지 않습니다.
    [Fact]
    public void ReturnsNothingForNonPositivePeriod()
    {
        var times = EvenTimes(10, Period30FpsUs);

        Assert.Empty(FindFrameGaps(times, 0));
    }

    // 간격 중앙값은 누락 한 곳에 끌려가지 않습니다.
    [Fact]
    public void MedianIgnoresSingleGap()
    {
        var times = EvenTimes(31, Period30FpsUs);
        Shift(times, 10, 100000);

        Assert.Equal(Period30FpsUs, MedianIntervalUs(times));
    }

    // 2026-10-16 실기: 어두운 장면의 자동 맞춤은 15fps(66.7ms)였고 30fps 선언과 맞지 않습니다.
    [Fact]
    public void DetectsHalvedFrameRate()
    {
        var times = EvenTimes(31, 66751);

        Assert.False(MatchesPeriod(MedianIntervalUs(times), Period30FpsUs, 5));
        Assert.True(MatchesPeriod(33332, Period30FpsUs, 5));
    }
}

public class FrameRateTests
{
    private const long Period30FpsUs = 33333;

    private static Timestamp[] EvenTimes(int count, long periodUs)
    {
        var times = new Timestamp[count];
        for (int i = 0; i < count; i++)
        {
            times[i] = new Timestamp(i * periodUs);
        }
        return times;
    }

    // 30fps로 찍다가 2초 동안 25fps(40ms)로 떨어지면 누락은 없고 느린 간격 50개가 한 구간으로 묶입니다.
    [Fact]
    public void FindsTwoSecondsAtTwentyFiveFps()
    {
        var times = new List<Timestamp>();
        long t = 0;
        for (int i = 0; i < 300; i++) times.Add(new Timestamp(t += Period30FpsUs));
        for (int i = 0; i < 50; i++) times.Add(new Timestamp(t += 40000));
        for (int i = 0; i < 300; i++) times.Add(new Timestamp(t += Period30FpsUs));

        Assert.Empty(FindFrameGaps(times, Period30FpsUs));
        var check = CheckFrameRate(times, Period30FpsUs);
        Assert.Equal(50, check.SlowIntervals);
        var span = Assert.Single(check.Spans);
        Assert.Equal(299 * Period30FpsUs, span.StartUs);
        Assert.Equal(299 * Period30FpsUs + 50 * 40000, span.EndUs);
        Assert.Equal(50, span.Intervals);
        Assert.Equal(25.0, check.MinFps, 0.01);
    }

    // 고른 30fps는 느린 간격이 없고 최저 fps가 30입니다. 실측 흔들림(33.15~33.50ms)도 세지 않습니다.
    [Fact]
    public void IgnoresEvenThirtyFpsAndJitter()
    {
        var times = EvenTimes(900, Period30FpsUs);
        times[100] = new Timestamp(times[100].Us + 170);
        var check = CheckFrameRate(times, Period30FpsUs);
        Assert.Equal(0, check.SlowIntervals);
        Assert.Empty(check.Spans);
        Assert.Equal(30.0, check.MinFps, 0.2);
    }

    // 한 장 누락(간격 2배)은 느린 간격이 아니라 누락으로 세고, 최저 fps에는 반영합니다. 1초 넘게 떨어진 느린 간격은 다른 구간입니다.
    [Fact]
    public void KeepsGapsSeparateAndSplitsDistantSpans()
    {
        var times = new List<Timestamp>();
        long t = 0;
        void Add(int count, long step)
        {
            for (int i = 0; i < count; i++) times.Add(new Timestamp(t += step));
        }
        Add(60, Period30FpsUs);
        Add(1, 2 * Period30FpsUs);  // 누락
        Add(60, Period30FpsUs);
        Add(3, 41000);
        Add(60, Period30FpsUs);     // 2초 떨어짐
        Add(2, 42000);
        Add(30, Period30FpsUs);

        Assert.Single(FindFrameGaps(times, Period30FpsUs));
        var check = CheckFrameRate(times, Period30FpsUs);
        Assert.Equal(5, check.SlowIntervals);
        Assert.Equal(2, check.Spans.Count);
        Assert.Equal(3, check.Spans[0].Intervals);
        Assert.Equal(2, check.Spans[1].Intervals);
        Assert.True(check.MinFps < 30.0);
        Assert.Empty(CheckFrameRate(times, 0).Spans);
    }

    // C#에서 추가한 확인입니다. 구간 목록까지 값으로 같은 결과는 같다고 봅니다(원본의 operator== 기본값에 해당).
    [Fact]
    public void FrameRateChecksWithSameValuesAreEqual()
    {
        var a = CheckFrameRate(EvenTimes(60, Period30FpsUs), Period30FpsUs);
        var b = CheckFrameRate(EvenTimes(60, Period30FpsUs), Period30FpsUs);

        Assert.Equal(a, b);
        Assert.NotEqual(a, a with { SlowIntervals = 1 });
        Assert.NotEqual(a, a with { Spans = new[] { new SlowSpan(0, 1, 1) } });
    }
}

public class SettleTests
{
    // See3CAM_24CUG 실측값(설계 문서 표 5).
    private static readonly SettleRule See3Cam = new()
    {
        ExposureAppliedFrame = 4,
        GainAppliedFrame = 3,
        WhiteBalanceAppliedFrame = 3,
        BlackLevelSettledFrame = 75,
    };

    // 받은 프레임마다 판정 결과를 기록해, n번째 프레임(1부터)의 결과를 꺼낼 수 있게 합니다.
    private sealed record Trace(List<bool> Applied, List<bool> Settled);

    private static Trace RunFrames(SettleState state, int frames)
    {
        var trace = new Trace(new List<bool>(), new List<bool>());
        for (int i = 0; i < frames; i++)
        {
            state = AfterFrame(state);
            trace.Applied.Add(state.IsApplied);
            trace.Settled.Add(state.IsSettled);
        }
        return trace;
    }

    // 노출은 바꾼 뒤 4번째 프레임부터 새 값으로 찍힙니다.
    [Fact]
    public void ExposureAppliesFromFourthFrame()
    {
        var state = AfterSettingChange(default, SettingKind.Exposure, See3Cam);

        var trace = RunFrames(state, 5);

        Assert.False(trace.Applied[2]);  // 3번째 프레임
        Assert.True(trace.Applied[3]);   // 4번째 프레임
    }

    // 게인은 바꾼 뒤 3번째 프레임부터 새 값으로 찍힙니다.
    [Fact]
    public void GainAppliesFromThirdFrame()
    {
        var state = AfterSettingChange(default, SettingKind.Gain, See3Cam);

        var trace = RunFrames(state, 4);

        Assert.False(trace.Applied[1]);
        Assert.True(trace.Applied[2]);
    }

    // 노출을 바꾼 뒤 75번째 프레임에서 안정화가 끝납니다.
    [Fact]
    public void ExposureSettlesAtSeventyFifthFrame()
    {
        var state = AfterSettingChange(default, SettingKind.Exposure, See3Cam);

        var trace = RunFrames(state, 80);

        Assert.False(trace.Settled[73]);  // 74번째 프레임
        Assert.True(trace.Settled[74]);   // 75번째 프레임
    }

    // 안정화 도중 설정을 다시 바꾸면 그 시점부터 75프레임을 다시 기다립니다.
    [Fact]
    public void ChangeDuringSettlingRestartsWait()
    {
        var state = AfterSettingChange(default, SettingKind.Gain, See3Cam);
        for (int i = 0; i < 50; i++)
        {
            state = AfterFrame(state);
        }

        state = AfterSettingChange(state, SettingKind.Exposure, See3Cam);
        var trace = RunFrames(state, 80);

        Assert.False(trace.Settled[73]);
        Assert.True(trace.Settled[74]);
    }

    // 같은 시점에 노출과 게인을 바꾸면 더 늦은 노출 기준으로 반영됩니다.
    [Fact]
    public void SimultaneousChangesWaitForLatest()
    {
        var state = AfterSettingChange(default, SettingKind.Gain, See3Cam);
        state = AfterSettingChange(state, SettingKind.Exposure, See3Cam);

        var trace = RunFrames(state, 5);

        Assert.False(trace.Applied[2]);
        Assert.True(trace.Applied[3]);
    }

    // 설정을 바꾼 적이 없으면 처음부터 반영되고 안정된 상태입니다.
    [Fact]
    public void NoChangeMeansSettled()
    {
        var trace = RunFrames(default, 1);

        Assert.True(trace.Applied[0]);
        Assert.True(trace.Settled[0]);
    }
}
