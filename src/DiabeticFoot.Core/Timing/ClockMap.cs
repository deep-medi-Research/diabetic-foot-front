namespace DiabeticFoot.Core.Timing;

/// <summary>
/// 같은 프레임(또는 PPG 표본)에 붙은 장치 시각과 PC 수신 시각입니다.
/// </summary>
/// <param name="Device">장치 또는 드라이버가 붙인 시각입니다.</param>
/// <param name="Host">PC가 받은 시각(공통 시간축)입니다. 촬영 시각보다 항상 늦고, 늦은 정도(수신 지연)는 프레임마다 다릅니다.</param>
public readonly record struct ClockSample(Timestamp Device, Timestamp Host);

/// <summary>
/// 장치 시각을 공통 시간축(PC 시계)으로 옮기는 선형식입니다. 기본값은 항등식입니다.
/// </summary>
/// <param name="OffsetUs">공통 시각은 OffsetUs + Rate * device(마이크로초, 반올림)입니다.</param>
/// <param name="Rate">장치 시각 1마이크로초에 해당하는 PC 시각(마이크로초)입니다.</param>
/// <param name="MaxResidualUs">맞춘 표본 중 PC 수신 시각이 선형식보다 가장 늦은 값(수신 지연의 변동 폭)입니다.</param>
/// <remarks>
/// 열화상(i3 SDK)은 프레임에 장치 시각이 없어 PC 수신 시각을 장치 시각으로 쓰므로 항등식입니다.
/// PPG는 표본 번호를 표본률로 나눈 값이 장치 시각입니다. RGB는 Windows Media Foundation 시각입니다.
/// </remarks>
public sealed record ClockMap(long OffsetUs = 0, double Rate = 1.0, long MaxResidualUs = 0);

/// <summary>
/// 수신 빈틈으로 나눈 시계 구간 하나입니다.
/// </summary>
/// <param name="LastDevice">이 구간 마지막 표본의 장치 시각입니다. 앞 구간의 LastDevice보다 크고 이 값 이하인 장치 시각이 이 구간입니다.</param>
/// <param name="Map">구간의 식입니다. 속도 비는 같은 FitClockSegments 결과의 모든 구간이 같고, OffsetUs와 MaxResidualUs만 구간마다 다릅니다.</param>
public sealed record ClockSegment(Timestamp LastDevice, ClockMap Map);

public static partial class TimingFunctions
{
    private readonly record struct Point(double X, double Y);

    /// <summary>
    /// 장치 시각과 PC 수신 시각의 쌍에서 <see cref="ClockMap"/>을 구합니다.
    /// </summary>
    /// <param name="samples">받은 순서대로의 표본</param>
    /// <returns>
    /// 맞춘 선형식. 표본이 없으면 항등식입니다. 다음 경우에는 Rate를 1로 두고 OffsetUs를 가장 작은 (host - device)로 정합니다:
    /// 표본이 2개 이하, 장치 시각이 앞 표본보다 작아짐, 구한 Rate가 0 이하
    /// </returns>
    /// <remarks>
    /// 수신은 촬영보다 항상 늦으므로 모든 표본의 아래쪽을 지나는 직선(최소 지연 하한선)에 맞춥니다.
    /// 표본점의 아래 볼록 껍질 변 가운데 장치 시각 평균 위치를 지나는 변을 고릅니다. 이 직선은 모든 표본 아래에 있는 직선 중
    /// 수신 지연 합이 가장 작습니다. 따라서 공통 시각은 수신 지연이 가장 짧은 경우의 PC 수신 시각입니다.
    /// 장치 시각이 같은 표본이 여럿이면 PC 수신 시각이 가장 이른 것만 씁니다.
    /// </remarks>
    public static ClockMap FitClockMap(IReadOnlyList<ClockSample> samples) => FitClockMapCore(Sequences.AsSpan(samples));

    private static ClockMap FitClockMapCore(ReadOnlySpan<ClockSample> samples)
    {
        if (samples.IsEmpty)
        {
            return new ClockMap();
        }
        for (int i = 1; i < samples.Length; i++)
        {
            if (samples[i].Device < samples[i - 1].Device)
            {
                return FitOffsetOnly(samples);
            }
        }
        // 표본 둘로 정한 기울기는 두 표본의 수신 지연 차이를 그대로 받아 실제 속도 차이(수백 ppm)보다 크게 틀립니다.
        if (samples.Length <= 2)
        {
            return FitOffsetOnly(samples);
        }

        // 첫 표본 기준의 차이로 계산해 큰 시각값(부팅 뒤 마이크로초)에서도 double 정밀도를 지킵니다.
        Timestamp device0 = samples[0].Device;
        Timestamp host0 = samples[0].Host;
        var points = new List<Point>(samples.Length);
        foreach (ClockSample s in samples)
        {
            var p = new Point((double)Timestamp.IntervalUs(device0, s.Device), (double)Timestamp.IntervalUs(host0, s.Host));
            if (points.Count > 0 && points[^1].X == p.X)
            {
                points[^1] = points[^1] with { Y = Math.Min(points[^1].Y, p.Y) };
                continue;
            }
            points.Add(p);
        }
        if (points.Count < 2)
        {
            return FitOffsetOnly(samples);
        }

        var hull = new List<Point>();
        foreach (Point p in points)
        {
            while (hull.Count >= 2 && Cross(hull[^2], hull[^1], p) <= 0)
            {
                hull.RemoveAt(hull.Count - 1);
            }
            hull.Add(p);
        }

        double meanX = 0;
        foreach (Point p in points)
        {
            meanX += p.X / (double)points.Count;
        }
        int edge = 0;
        while (edge + 2 < hull.Count && hull[edge + 1].X < meanX)
        {
            edge++;
        }
        Point a = hull[edge];
        Point b = hull[edge + 1];
        double rate = (b.Y - a.Y) / (b.X - a.X);
        if (!(rate > 0))
        {
            return FitOffsetOnly(samples);
        }

        double intercept = a.Y - rate * a.X;
        long offsetUs = RoundToLong((double)host0.Us + intercept - rate * (double)device0.Us);
        var map = new ClockMap(offsetUs, rate);
        return map with { MaxResidualUs = MaxResidualUs(map, samples) };
    }

    // 기울기를 정할 근거가 부족한 입력은 시계 속도가 같다고 보고 가장 짧은 수신 지연에 맞춥니다.
    private static ClockMap FitOffsetOnly(ReadOnlySpan<ClockSample> samples)
    {
        long offsetUs = long.MaxValue;
        foreach (ClockSample s in samples)
        {
            offsetUs = Math.Min(offsetUs, Timestamp.IntervalUs(s.Device, s.Host));
        }
        var map = new ClockMap(offsetUs);
        return map with { MaxResidualUs = MaxResidualUs(map, samples) };
    }

    // 속도 비가 rate일 때 모든 표본 아래를 지나는 오프셋 중 가장 큰 값(가장 짧은 수신 지연)입니다.
    private static long MinOffsetUs(ReadOnlySpan<ClockSample> samples, double rate)
    {
        long offsetUs = long.MaxValue;
        var shifted = new ClockMap(0, rate);
        foreach (ClockSample s in samples)
        {
            offsetUs = Math.Min(offsetUs, Timestamp.IntervalUs(ToCommonTime(shifted, s.Device), s.Host));
        }
        return offsetUs;
    }

    private static long MaxResidualUs(ClockMap map, ReadOnlySpan<ClockSample> samples)
    {
        long maxResidual = 0;
        foreach (ClockSample s in samples)
        {
            maxResidual = Math.Max(maxResidual, Timestamp.IntervalUs(ToCommonTime(map, s.Device), s.Host));
        }
        return maxResidual;
    }

    // 0 이하이면 o, a, b가 시계 방향이거나 한 직선 위라 a는 아래 껍질의 꼭짓점이 아닙니다.
    private static double Cross(Point o, Point a, Point b) => (a.X - o.X) * (b.Y - o.Y) - (a.Y - o.Y) * (b.X - o.X);

    // C++ llround와 같게 0.5는 0에서 먼 쪽으로 올립니다.
    private static long RoundToLong(double value) => (long)Math.Round(value, MidpointRounding.AwayFromZero);

    /// <summary>
    /// 장치 시각을 공통 시간축(PC 시계)으로 옮깁니다.
    /// </summary>
    /// <param name="map"><see cref="FitClockMap"/>의 결과</param>
    /// <param name="device">장치 시각</param>
    /// <returns>공통 시간축 시각(PC 시계, 마이크로초)</returns>
    public static Timestamp ToCommonTime(ClockMap map, Timestamp device) =>
        new(map.OffsetUs + RoundToLong(map.Rate * (double)device.Us));

    /// <summary>
    /// 표본을 수신 빈틈에서 시계 구간으로 나누고 구간마다 <see cref="ClockMap"/>을 구합니다.
    /// </summary>
    /// <param name="samples">받은 순서대로의 표본</param>
    /// <param name="gapUs">수신 빈틈으로 볼, PC 수신 시각이 장치 시각보다 더 흐른 시간. 0 이하이면 나누지 않습니다</param>
    /// <returns>
    /// 장치 시각 순서의 구간. 표본이 없으면 빈 목록입니다. 장치 시각이 앞 표본보다 작아지는 입력은 나누지 않고
    /// 모든 표본의 <see cref="FitClockMap"/> 결과 하나입니다
    /// </returns>
    /// <remarks>
    /// 이웃한 두 표본 사이에서 PC 수신 시각이 장치 시각보다 gapUs 넘게 더 흐르면 그 사이를 수신 빈틈으로 봅니다.
    /// 속도 비는 장치 시각 폭이 가장 긴 구간을 FitClockMap으로 맞춘 값을 모든 구간에 씁니다. 오프셋은 구간마다 그 구간의
    /// 모든 표본 아래를 지나는 값 중 가장 큰 값(가장 짧은 수신 지연)입니다. 가장 긴 구간의 식은 FitClockMap 결과와 같습니다.
    /// 장치가 표본을 보내지 않다가 다시 보내도 표본 번호로 구한 장치 시각은 이어집니다. 이런 표본을 한 식으로 맞추면
    /// 아래 껍질이 빈틈 앞 표본에서 꺾여 속도 비가 커지고, 빈틈 뒤 표본의 공통 시각이 빈틈만큼 이르게 나옵니다.
    /// 빈틈 앞 구간이 표본 한두 개로 짧아도 속도 비를 긴 구간에서 가져오므로 오프셋만 맞추면 됩니다.
    /// </remarks>
    public static IReadOnlyList<ClockSegment> FitClockSegments(IReadOnlyList<ClockSample> samples, long gapUs)
    {
        var all = Sequences.AsSpan(samples);
        var segments = new List<ClockSegment>();
        if (all.IsEmpty)
        {
            return segments;
        }
        var starts = new List<int> { 0 };
        for (int i = 1; i < all.Length; i++)
        {
            if (all[i].Device < all[i - 1].Device)
            {
                Timestamp last = all[0].Device;
                foreach (ClockSample s in all)
                {
                    if (last < s.Device)
                    {
                        last = s.Device;
                    }
                }
                segments.Add(new ClockSegment(last, FitClockMapCore(all)));
                return segments;
            }
            long extraUs = Timestamp.IntervalUs(all[i - 1].Host, all[i].Host) -
                           Timestamp.IntervalUs(all[i - 1].Device, all[i].Device);
            if (gapUs > 0 && extraUs > gapUs)
            {
                starts.Add(i);
            }
        }
        starts.Add(all.Length);

        int longest = 0;
        for (int k = 1; k + 1 < starts.Count; k++)
        {
            var a = Part(all, starts, k);
            var b = Part(all, starts, longest);
            if (Timestamp.IntervalUs(a[0].Device, a[^1].Device) > Timestamp.IntervalUs(b[0].Device, b[^1].Device))
            {
                longest = k;
            }
        }
        // 빈틈 앞 구간은 표본이 한두 개라 기울기를 정할 수 없으므로 가장 긴 구간의 속도 비를 함께 씁니다.
        ClockMap fitted = FitClockMapCore(Part(all, starts, longest));
        for (int k = 0; k + 1 < starts.Count; k++)
        {
            var part = Part(all, starts, k);
            ClockMap map = fitted;
            if (k != longest)
            {
                map = map with { OffsetUs = MinOffsetUs(part, map.Rate) };
                map = map with { MaxResidualUs = MaxResidualUs(map, part) };
            }
            segments.Add(new ClockSegment(part[^1].Device, map));
        }
        return segments;
    }

    private static ReadOnlySpan<ClockSample> Part(ReadOnlySpan<ClockSample> all, List<int> starts, int k) =>
        all.Slice(starts[k], starts[k + 1] - starts[k]);

    /// <summary>
    /// 장치 시각을 그 시각이 든 시계 구간의 식으로 공통 시간축에 옮깁니다.
    /// </summary>
    /// <param name="segments"><see cref="FitClockSegments"/>의 결과</param>
    /// <param name="device">장치 시각</param>
    /// <returns>
    /// 공통 시각. LastDevice가 device 이상인 첫 구간의 식을 쓰고, 모든 구간의 LastDevice보다 크면 마지막 구간의 식을
    /// 씁니다. 구간이 없으면 장치 시각 그대로입니다
    /// </returns>
    public static Timestamp ToCommonTime(IReadOnlyList<ClockSegment> segments, Timestamp device)
    {
        if (segments.Count == 0)
        {
            return device;
        }
        foreach (ClockSegment s in segments)
        {
            if (device <= s.LastDevice)
            {
                return ToCommonTime(s.Map, device);
            }
        }
        return ToCommonTime(segments[^1].Map, device);
    }

    /// <summary>
    /// 기준 스트림의 프레임마다 공통 시각이 가장 가까운 다른 스트림 프레임의 위치를 찾습니다.
    /// </summary>
    /// <param name="reference">기준 스트림(RGB)의 공통 시각. 오름차순</param>
    /// <param name="other">다른 스트림의 공통 시각. 오름차순</param>
    /// <returns>
    /// reference와 같은 길이의 목록. i번째 값은 reference[i]와 가장 가까운 other의 위치입니다.
    /// 거리가 같으면 앞 위치를 고릅니다. other가 비어 있으면 빈 목록입니다
    /// </returns>
    /// <remarks>오름차순이 아닌 입력에도 같은 입력에는 같은 결과를 내지만, 가장 가까운 위치라는 보장은 없습니다.</remarks>
    public static IReadOnlyList<int> NearestFrames(IReadOnlyList<Timestamp> reference, IReadOnlyList<Timestamp> other)
    {
        var refs = Sequences.AsSpan(reference);
        var others = Sequences.AsSpan(other);
        var nearest = new List<int>();
        if (others.IsEmpty)
        {
            return nearest;
        }
        nearest.Capacity = refs.Length;
        static long Distance(Timestamp a, Timestamp b) =>
            a < b ? Timestamp.IntervalUs(a, b) : Timestamp.IntervalUs(b, a);
        // 두 목록이 오름차순이면 가장 가까운 위치도 앞으로만 움직이므로 한 번 훑어 찾습니다.
        int j = 0;
        foreach (Timestamp r in refs)
        {
            while (j + 1 < others.Length && Distance(others[j + 1], r) < Distance(others[j], r))
            {
                j++;
            }
            nearest.Add(j);
        }
        return nearest;
    }
}
