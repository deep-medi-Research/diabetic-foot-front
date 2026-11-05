using DiabeticFoot.Core;
using static DiabeticFoot.Platform.FrameTiming;

namespace DiabeticFoot.Platform.Tests;

public class FrameTimingTests
{
    const long PeriodUs = 33333;

    // 30fps 900장 중 프레임 300~350이 25fps(40ms)인 녹화입니다.
    static List<Timestamp> TimesWithSlowSpan()
    {
        var times = new List<Timestamp>();
        long t = 1_000_000;
        for (int i = 0; i < 900; ++i)
        {
            times.Add(new Timestamp(t));
            t += i is >= 300 and < 350 ? 40000 : PeriodUs;
        }
        return times;
    }

    // 느린 간격이 든 분석 구간만 느린 간격 수와 최저 fps가 달라집니다. 구간은 프레임 번호 / fps로 프레임에 대응합니다.
    [Fact]
    public void SummarizesEachAnalysisWindow()
    {
        List<Timestamp> times = TimesWithSlowSpan();
        AnalysisWindow[] windows = [new("pi", 5, 10), new("pi", 11, 10), new("cuff_level", 25, 10)];
        IReadOnlyList<FrameTimingWindow> rows = SummarizeFrameWindows(times, 30, windows);
        Assert.Equal(3, rows.Count);
        Assert.Equal(0, rows[0].FirstFrame);
        Assert.Equal(300, rows[0].Frames);
        Assert.Equal(0, rows[0].SlowIntervals);
        Assert.Equal(30, rows[0].MinFps, 0.1);
        Assert.Equal(180, rows[1].FirstFrame);
        Assert.Equal(50, rows[1].SlowIntervals);
        Assert.Equal(25, rows[1].MinFps, 0.1);
        Assert.True(rows[1].ElapsedS > rows[0].ElapsedS);
        Assert.Equal(0, rows[2].SlowIntervals);
        Assert.Equal(0, rows[2].MissingFrames);
        Assert.Empty(SummarizeFrameWindows(times, 0, windows));
    }

    // 프레임별 CSV는 첫 프레임 간격을 비우고, 1.2배 초과는 slow, 1.5배 초과는 gap입니다.
    [Fact]
    public void WritesFrameRowsWithKind()
    {
        Timestamp[] times = [new(0), new(33333), new(73333), new(140000)];
        string csv = BuildFrameTimingCsv(times, 30);
        Assert.Equal(
            "frame,analysis_s,elapsed_s,common_time_us,interval_ms,kind\n" +
            "0,0.0000,0.000000,0,,\n" +
            "1,0.0333,0.033333,33333,33.333,\n" +
            "2,0.0667,0.073333,73333,40.000,slow\n" +
            "3,0.1000,0.140000,140000,66.667,gap\n",
            csv);
    }

    [Fact]
    public void WritesWindowRows()
    {
        IReadOnlyList<FrameTimingWindow> rows = SummarizeFrameWindows(TimesWithSlowSpan(), 30, [new AnalysisWindow("cuff_level", 15, 10)]);
        string csv = BuildFrameWindowsCsv(rows);
        Assert.Equal(
            "kind,centre_s,start_s,end_s,first_frame,frames,elapsed_s,slow_intervals,missing_frames,min_fps",
            csv[..csv.IndexOf('\n')]);
        Assert.Contains("cuff_level,15.000,10.000,20.000,300,300,", csv);
        Assert.Contains(",50,0,25.0\n", csv);
    }
}
