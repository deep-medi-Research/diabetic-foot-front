using System.Globalization;
using System.Text;
using DiabeticFoot.Core;
using DiabeticFoot.Core.Record;
using DiabeticFoot.Core.Timing;

namespace DiabeticFoot.Platform;

/// <summary>
/// 관류 분석 프로그램의 분석 구간 하나입니다.
/// </summary>
/// <remarks>
/// 관류 분석 프로그램은 프레임을 정확히 1/fps 간격으로 보고 계산하므로 구간 시각은 "프레임 번호 / fps"입니다. 녹화 중 fps가
/// 떨어지면 이 시각과 실제 시각이 어긋나므로, 구간을 프레임 번호로 바꿔 그 안의 실제 프레임 간격을 봅니다.
/// </remarks>
/// <param name="Kind">"pi"(10초 관류 구간, 요약 n_windows), "cuff_level"(커프 단계 구간, cuff_level.window_centres_s)입니다.</param>
/// <param name="CentreS">구간 가운데(프레임 번호 / fps)입니다.</param>
/// <param name="LengthS">구간 길이입니다.</param>
public sealed record AnalysisWindow(string Kind = "", double CentreS = 0, double LengthS = 0);

/// <summary>분석 구간 하나의 프레임 간격 요약입니다.</summary>
public sealed record FrameTimingWindow
{
    public AnalysisWindow Window { get; init; } = new();

    /// <summary>구간의 첫 프레임 번호(파일 안 순서, 0부터)입니다.</summary>
    public long FirstFrame { get; init; }

    /// <summary>구간 안 프레임 수입니다.</summary>
    public long Frames { get; init; }

    /// <summary>느린 간격 수(주기의 1.2배 초과 1.5배 이하, <c>CheckFrameRate</c>)입니다.</summary>
    public long SlowIntervals { get; init; }

    /// <summary>카메라 단 누락 장 수(주기의 1.5배 초과, <c>FindFrameGaps</c>)입니다.</summary>
    public long MissingFrames { get; init; }

    /// <summary>구간 안 1초 창의 가장 낮은 fps입니다. 프레임이 둘 미만이면 0입니다.</summary>
    public double MinFps { get; init; }

    /// <summary>구간 첫 프레임부터 마지막 프레임까지 실제 시각(공통 시각)입니다. 고른 30fps면 LengthS와 거의 같습니다.</summary>
    public double ElapsedS { get; init; }
}

public static class FrameTiming
{
    /// <summary>
    /// 분석 구간마다 그 안의 프레임 간격을 요약합니다.
    /// </summary>
    /// <param name="times">파일에 쓴 순서의 프레임 공통 시각(사이드카 per_frame.common_time_us)입니다.</param>
    /// <param name="fps">관류 분석 프로그램이 쓴 fps(요약 fps)입니다. 구간을 프레임 번호로 바꾸고 주기(1/fps)를 정합니다.</param>
    /// <returns>windows와 같은 순서입니다. fps가 0 이하이면 빈 목록입니다.</returns>
    public static IReadOnlyList<FrameTimingWindow> SummarizeFrameWindows(
        IReadOnlyList<Timestamp> times, double fps, IReadOnlyList<AnalysisWindow> windows)
    {
        var output = new List<FrameTimingWindow>();
        if (!(fps > 0)) return output;
        long period = PeriodUs(fps);
        long count = times.Count;
        foreach (AnalysisWindow w in windows)
        {
            long begin = Math.Clamp(LRound((w.CentreS - w.LengthS / 2) * fps), 0, count);
            long end = Math.Clamp(LRound((w.CentreS + w.LengthS / 2) * fps), begin, count);
            var part = new Timestamp[end - begin];
            for (long i = begin; i < end; ++i) part[i - begin] = times[(int)i];
            FrameRateCheck check = TimingFunctions.CheckFrameRate(part, period);
            output.Add(new FrameTimingWindow
            {
                Window = w,
                FirstFrame = begin,
                Frames = end - begin,
                SlowIntervals = check.SlowIntervals,
                MinFps = check.MinFps,
                MissingFrames = TimingFunctions.CountMissingFrames(TimingFunctions.FindFrameGaps(part, period)),
                ElapsedS = part.Length >= 2 ? (double)(part[^1].Us - part[0].Us) / 1e6 : 0,
            });
        }
        return output;
    }

    /// <summary>
    /// 프레임별 간격 CSV를 만듭니다. 열은 frame, analysis_s, elapsed_s, common_time_us, interval_ms, kind입니다.
    /// </summary>
    /// <remarks>
    /// analysis_s는 관류 분석 프로그램이 보는 시각(frame / fps), elapsed_s는 첫 프레임부터의 실제 시각, interval_ms는 앞 프레임과의
    /// 실제 간격(첫 프레임은 비움), kind는 느린 간격이면 slow, 누락이면 gap, 아니면 비웁니다.
    /// </remarks>
    public static string BuildFrameTimingCsv(IReadOnlyList<Timestamp> times, double fps)
    {
        var output = new StringBuilder("frame,analysis_s,elapsed_s,common_time_us,interval_ms,kind\n");
        if (times.Count == 0 || !(fps > 0)) return output.ToString();
        long period = PeriodUs(fps);
        for (int i = 0; i < times.Count; ++i)
        {
            output.Append(Int(i)).Append(',')
                .Append(CFormat.Fixed(i / fps, 4)).Append(',')
                .Append(CFormat.Fixed((double)(times[i].Us - times[0].Us) / 1e6, 6)).Append(',')
                .Append(Int(times[i].Us)).Append(',');
            if (i > 0)
            {
                long interval = times[i].Us - times[i - 1].Us;
                output.Append(CFormat.Fixed(interval / 1000.0, 3)).Append(',');
                // CheckFrameRate, FindFrameGaps와 같은 기준입니다.
                if (interval * 2 > period * 3)
                {
                    output.Append("gap");
                }
                else if (interval * 5 > period * 6)
                {
                    output.Append("slow");
                }
            }
            else
            {
                output.Append(',');
            }
            output.Append('\n');
        }
        return output.ToString();
    }

    /// <summary>
    /// 분석 구간별 CSV를 만듭니다. 열은 kind, centre_s, start_s, end_s, first_frame, frames, elapsed_s, slow_intervals,
    /// missing_frames, min_fps입니다.
    /// </summary>
    public static string BuildFrameWindowsCsv(IReadOnlyList<FrameTimingWindow> windows)
    {
        var output = new StringBuilder("kind,centre_s,start_s,end_s,first_frame,frames,elapsed_s,slow_intervals,missing_frames,min_fps\n");
        foreach (FrameTimingWindow r in windows)
        {
            output.Append(r.Window.Kind).Append(',')
                .Append(CFormat.Fixed(r.Window.CentreS, 3)).Append(',')
                .Append(CFormat.Fixed(r.Window.CentreS - r.Window.LengthS / 2, 3)).Append(',')
                .Append(CFormat.Fixed(r.Window.CentreS + r.Window.LengthS / 2, 3)).Append(',')
                .Append(Int(r.FirstFrame)).Append(',')
                .Append(Int(r.Frames)).Append(',')
                .Append(CFormat.Fixed(r.ElapsedS, 3)).Append(',')
                .Append(Int(r.SlowIntervals)).Append(',')
                .Append(Int(r.MissingFrames)).Append(',')
                .Append(CFormat.Fixed(r.MinFps, 1)).Append('\n');
        }
        return output.ToString();
    }

    /// <summary>프레임별 간격 CSV의 경로입니다(<c>&lt;name&gt;_frame_timing.csv</c>). 분석결과 폴더로 복사합니다.</summary>
    public static string FrameTimingCsvPath(string outputDir, string name) => JoinPath(outputDir, name + "_frame_timing.csv");

    /// <summary>분석 구간별 CSV의 경로입니다(<c>&lt;name&gt;_frame_windows.csv</c>). 분석결과 폴더로 복사합니다.</summary>
    public static string FrameWindowsCsvPath(string outputDir, string name) => JoinPath(outputDir, name + "_frame_windows.csv");

    static long PeriodUs(double fps) => LRound(1e6 / fps);

    static long LRound(double value) => (long)Math.Round(value, MidpointRounding.AwayFromZero);

    static string Int(long value) => value.ToString(CultureInfo.InvariantCulture);

    static string JoinPath(string dir, string name)
    {
        if (dir.Length == 0 || dir[^1] == '/' || dir[^1] == '\\') return dir + name;
        return dir + "/" + name;
    }
}
