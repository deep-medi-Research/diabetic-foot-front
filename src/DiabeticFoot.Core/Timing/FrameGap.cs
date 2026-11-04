namespace DiabeticFoot.Core.Timing;

/// <summary>프레임 간격이 정해진 주기의 1.5배를 넘은 자리 하나입니다.</summary>
/// <param name="Before">누락 직전 프레임의 위치(times 기준)입니다.</param>
/// <param name="IntervalUs">그 자리의 프레임 간격(마이크로초)입니다.</param>
/// <param name="MissingFrames">간격을 주기로 나눠 반올림한 값에서 1을 뺀 수입니다.</param>
public readonly record struct FrameGap(int Before, long IntervalUs, long MissingFrames);

/// <summary>느린 간격이 이어진 구간 하나입니다. 시각은 첫 프레임 기준입니다.</summary>
/// <param name="StartUs">구간의 첫 느린 간격이 시작한 프레임의 시각입니다.</param>
/// <param name="EndUs">구간의 마지막 느린 간격이 끝난 프레임의 시각입니다.</param>
/// <param name="Intervals">구간 안의 느린 간격 수입니다.</param>
public readonly record struct SlowSpan(long StartUs, long EndUs, long Intervals);

/// <summary>녹화 중 프레임 속도가 떨어진 정도입니다(<see cref="TimingFunctions.CheckFrameRate"/>).</summary>
public sealed record FrameRateCheck
{
    /// <summary>느린 간격 수입니다. 주기의 1.2배를 넘고 1.5배 이하인 간격입니다.</summary>
    public long SlowIntervals { get; init; }

    /// <summary>느린 간격을 1초 안팎으로 묶은 구간입니다.</summary>
    public IReadOnlyList<SlowSpan> Spans { get; init; } = Array.Empty<SlowSpan>();

    /// <summary>1초 창(주기 기준 프레임 수)에서 잰 가장 낮은 fps입니다. 누락도 반영합니다. 프레임이 둘 미만이면 0입니다.</summary>
    public double MinFps { get; init; }

    public bool Equals(FrameRateCheck? other) =>
        other is not null && SlowIntervals == other.SlowIntervals && MinFps == other.MinFps &&
        Spans.SequenceEqual(other.Spans);

    public override int GetHashCode() => HashCode.Combine(SlowIntervals, MinFps, Spans.Count);
}

public static partial class TimingFunctions
{
    /// <summary>
    /// 카메라 단 누락 자리를 찾습니다. 이웃한 두 프레임의 간격이 주기의 1.5배를 넘으면 그 사이에 프레임이 빠진 것으로 봅니다.
    /// </summary>
    /// <param name="times">받은 순서대로 정렬된 프레임 시각. 저장 누락으로 버린 프레임의 시각도 넣어야 합니다</param>
    /// <param name="periodUs">선언한 fps의 프레임 주기</param>
    /// <returns>누락 자리 목록. periodUs가 0 이하이거나 누락이 없으면 빈 목록입니다</returns>
    /// <remarks>카메라가 fps를 스스로 낮춘 경우에는 모든 간격이 누락으로 잡힙니다. 먼저 <see cref="MatchesPeriod"/>로 주기를 확인합니다.</remarks>
    public static IReadOnlyList<FrameGap> FindFrameGaps(IReadOnlyList<Timestamp> times, long periodUs)
    {
        var gaps = new List<FrameGap>();
        if (periodUs <= 0)
        {
            return gaps;
        }
        var t = Sequences.AsSpan(times);
        for (int i = 1; i < t.Length; i++)
        {
            long intervalUs = Timestamp.IntervalUs(t[i - 1], t[i]);
            // 한 장이 빠지면 간격이 주기의 2배가 되므로, 1.5배 이하의 흔들림은 누락으로 세지 않습니다.
            if (intervalUs * 2 > periodUs * 3)
            {
                long periods = (intervalUs + periodUs / 2) / periodUs;
                gaps.Add(new FrameGap(i - 1, intervalUs, periods - 1));
            }
        }
        return gaps;
    }

    /// <summary>누락 자리들에서 빠진 프레임 수를 더합니다.</summary>
    public static long CountMissingFrames(IReadOnlyList<FrameGap> gaps)
    {
        long missing = 0;
        foreach (FrameGap gap in gaps)
        {
            missing += gap.MissingFrames;
        }
        return missing;
    }

    /// <summary>
    /// 프레임 간격의 중앙값을 구합니다. 누락 몇 곳에 값이 끌려가지 않아 실제 fps를 잴 때 씁니다.
    /// </summary>
    /// <param name="times">받은 순서대로 정렬된 프레임 시각</param>
    /// <returns>간격 중앙값(마이크로초). 시각이 둘 미만이면 0입니다. 간격이 짝수 개이면 위쪽 중앙값입니다</returns>
    public static long MedianIntervalUs(IReadOnlyList<Timestamp> times)
    {
        var t = Sequences.AsSpan(times);
        if (t.Length < 2)
        {
            return 0;
        }
        var intervals = new long[t.Length - 1];
        for (int i = 1; i < t.Length; i++)
        {
            intervals[i - 1] = Timestamp.IntervalUs(t[i - 1], t[i]);
        }
        Array.Sort(intervals);
        return intervals[intervals.Length / 2];
    }

    /// <summary>
    /// 실제 프레임 간격이 선언한 주기와 맞는지 판정합니다.
    /// </summary>
    /// <param name="medianIntervalUs"><see cref="MedianIntervalUs"/>로 잰 실제 간격</param>
    /// <param name="periodUs">선언한 fps의 프레임 주기</param>
    /// <param name="tolerancePercent">허용하는 차이(퍼센트)</param>
    /// <returns>차이가 허용 범위 안이면 true. 간격이나 주기가 0 이하이면 false입니다</returns>
    /// <remarks>
    /// 카메라가 fps를 스스로 낮추면(어두운 장면의 펌웨어 자동 노출, 2026-10-16 실측 15fps) 누락이 아니라
    /// 주기가 다른 것이므로 누락 판정과 따로 봅니다.
    /// </remarks>
    public static bool MatchesPeriod(long medianIntervalUs, long periodUs, int tolerancePercent)
    {
        if (medianIntervalUs <= 0 || periodUs <= 0)
        {
            return false;
        }
        long diff = medianIntervalUs > periodUs ? medianIntervalUs - periodUs : periodUs - medianIntervalUs;
        return diff * 100 <= periodUs * tolerancePercent;
    }

    /// <summary>
    /// 프레임 속도가 떨어진 구간을 찾습니다.
    /// </summary>
    /// <param name="times">받은 순서대로 정렬된 프레임 시각</param>
    /// <param name="periodUs">선언한 fps의 프레임 주기. 0 이하이면 빈 결과입니다</param>
    /// <returns>느린 간격 수, 구간, 1초 창 최저 fps</returns>
    /// <remarks>
    /// 주기의 1.2배를 넘고 1.5배 이하인 간격을 느린 간격으로 세고, 다음 느린 간격이 1초 안에 오면 같은 구간으로 묶습니다.
    /// 1.2배는 30fps의 흔들림(노트북 실측 27건, 33.15~33.50ms)보다 충분히 크고 25fps(40ms)를 잡는 값입니다.
    /// </remarks>
    public static FrameRateCheck CheckFrameRate(IReadOnlyList<Timestamp> times, long periodUs)
    {
        var t = Sequences.AsSpan(times);
        if (periodUs <= 0 || t.Length < 2)
        {
            return new FrameRateCheck();
        }
        const long MergeUs = 1_000_000;
        long origin = t[0].Us;
        long slowIntervals = 0;
        var spans = new List<SlowSpan>();
        for (int i = 1; i < t.Length; i++)
        {
            long intervalUs = Timestamp.IntervalUs(t[i - 1], t[i]);
            // 1.2배 초과이면서 1.5배 이하만 셉니다. 1.5배를 넘으면 누락(FindFrameGaps)입니다.
            if (intervalUs * 5 <= periodUs * 6 || intervalUs * 2 > periodUs * 3)
            {
                continue;
            }
            slowIntervals++;
            long start = t[i - 1].Us - origin;
            long end = t[i].Us - origin;
            if (spans.Count > 0 && start - spans[^1].EndUs <= MergeUs)
            {
                SlowSpan last = spans[^1];
                spans[^1] = last with { EndUs = end, Intervals = last.Intervals + 1 };
            }
            else
            {
                spans.Add(new SlowSpan(start, end, 1));
            }
        }
        // 1초 창: 주기로 1초에 들어가는 간격 수만큼의 프레임을 묶어 그 사이 시간으로 fps를 잽니다.
        long window = Math.Max(1L, (1_000_000 + periodUs / 2) / periodUs);
        int span = (int)Math.Min(window, t.Length - 1);
        double lowest = 0;
        for (int k = 0; k + span < t.Length; k++)
        {
            long elapsed = Timestamp.IntervalUs(t[k], t[k + span]);
            if (elapsed <= 0)
            {
                continue;
            }
            double fps = (double)span * 1e6 / (double)elapsed;
            if (lowest == 0 || fps < lowest)
            {
                lowest = fps;
            }
        }
        return new FrameRateCheck { SlowIntervals = slowIntervals, Spans = spans, MinFps = lowest };
    }
}
