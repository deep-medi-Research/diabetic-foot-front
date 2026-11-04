namespace DiabeticFoot.Core.Pixel;

/// <summary>영역 안 모든 화소의 밝기 합계입니다. 밝기는 UYVY와 NV12에서 Y, Y16에서 원시값입니다.</summary>
/// <param name="PixelCount">잰 화소 수입니다.</param>
/// <param name="Sum">밝기 합계입니다.</param>
/// <param name="ClippedPixels">밝기가 clip_level 이상인 화소 수입니다.</param>
public readonly record struct Brightness(long PixelCount = 0, long Sum = 0, long ClippedPixels = 0);

/// <summary>
/// 녹화한 프레임들의 영역 밝기가 프레임마다 얼마나 흔들리는지 요약한 값입니다.
/// </summary>
/// <remarks>
/// LED를 PWM으로 조절하면 PWM 주기와 노출 시간이 맞지 않을 때 노출 하나에 들어가는 켜짐 시간이 프레임마다 달라지고,
/// 프레임 밝기가 PWM 주파수와 fps의 차이로 정해지는 느린 주기(맥놀이)로 오르내립니다. 이 값으로 그 주기를 찾습니다.
/// 밝기 단위는 화소 하나의 밝기(UYVY와 NV12의 Y)입니다.
/// </remarks>
public readonly record struct Flicker
{
    public long FrameCount { get; init; }

    /// <summary>프레임 평균 밝기들의 평균입니다.</summary>
    public double Mean { get; init; }

    /// <summary>프레임 평균 밝기들의 표준편차(모집단)입니다.</summary>
    public double Stddev { get; init; }

    /// <summary>가장 밝은 프레임과 가장 어두운 프레임의 평균 밝기 차입니다.</summary>
    public double PeakToPeak { get; init; }

    /// <summary>가장 큰 주기 성분이 흔들림(직선 추세를 뺀 분산)에서 차지하는 비율입니다. 0에서 1 사이입니다.</summary>
    public double PeriodicFraction { get; init; }

    /// <summary>주된 주기(프레임 수, 소수)입니다. 주기 성분이 기준보다 약하면 비어 있습니다.</summary>
    public double? DominantPeriodFrames { get; init; }
}

/// <summary>LED 변경이 영상 밝기에 나타났는지 판정한 결과입니다. 사이드카 light.response에 남깁니다.</summary>
public enum LightResponse : byte
{
    /// <summary>판정하지 않음(기준값 없음, 프레임 부족, 보낸 밝기 변화가 0이거나 모름)입니다.</summary>
    Unknown,

    /// <summary>보낸 방향으로 기준 이상 바뀌었습니다.</summary>
    Applied,

    /// <summary>기준 이상 바뀌지 않았거나 반대 방향으로 바뀌었습니다.</summary>
    NotSeen,
}

/// <summary>
/// LED 변경의 반영 판정 기준입니다. LED 보드와 카메라 조합의 실측값이라 platform이 넘깁니다.
/// </summary>
/// <remarks>실측하지 않았습니다. WindowFrames나 MinMeanChange가 0 이하이면 판정하지 않습니다(<see cref="LightResponse.Unknown"/>).</remarks>
public readonly record struct LightResponseRule
{
    /// <summary>LED 명령 뒤 새 밝기로 보는 첫 프레임입니다(명령 뒤 받은 프레임을 1부터 셈).</summary>
    public int AppliedFrame { get; init; }

    /// <summary>명령 전과 반영 뒤에 각각 평균할 프레임 수입니다.</summary>
    public int WindowFrames { get; init; }

    /// <summary>반영으로 보는 평균 밝기(화소 하나의 Y) 변화의 최솟값입니다.</summary>
    public double MinMeanChange { get; init; }
}

public static partial class PixelFunctions
{
    /// <summary>영역의 밝기 합계와 포화 화소 수를 잽니다.</summary>
    /// <param name="frame">잴 프레임</param>
    /// <param name="rect">잴 영역</param>
    /// <param name="clipLevel">이 밝기 이상을 포화 화소로 셉니다</param>
    /// <returns>화소 수, 합계, 포화 화소 수. 영역이 비었거나 프레임을 벗어나면 화소 수가 0입니다</returns>
    /// <remarks>표본을 건너뛰지 않고 모든 화소를 읽습니다. 블랙 레벨 변화(약 +7)를 표본 오차 없이 비교하기 위해서입니다.</remarks>
    public static Brightness MeasureBrightness(FrameView frame, Rect rect, int clipLevel)
    {
        if (frame.Plane0.IsEmpty || !IsInside(rect, frame))
        {
            return default;
        }
        ReadOnlySpan<byte> plane = frame.Plane0.Span;
        (long sum, long clipped) = frame.Format switch
        {
            PixelFormat.Uyvy => AccumulateUyvy(plane, frame.RowStride0, rect, clipLevel),
            PixelFormat.Nv12 => AccumulateNv12(plane, frame.RowStride0, rect, clipLevel),
            PixelFormat.Y16 => AccumulateY16(plane, frame.RowStride0, rect, clipLevel),
            _ => (0L, 0L),
        };
        return new Brightness((long)rect.Width * rect.Height, sum, clipped);
    }

    /// <summary>평균 밝기를 구합니다.</summary>
    /// <param name="brightness"><see cref="MeasureBrightness"/>의 결과</param>
    /// <returns>합계를 화소 수로 나눈 값. 화소 수가 0이면 0입니다</returns>
    public static double MeanOf(Brightness brightness)
    {
        if (brightness.PixelCount == 0)
        {
            return 0.0;
        }
        return (double)brightness.Sum / (double)brightness.PixelCount;
    }

    /// <summary>프레임별 영역 밝기 합계로 깜빡임을 요약합니다.</summary>
    /// <param name="sums">프레임 순서대로의 영역 밝기 합계(<see cref="Brightness.Sum"/>). 모든 프레임의 영역 크기가 같아야 합니다</param>
    /// <param name="pixelCount">영역의 화소 수(<see cref="Brightness.PixelCount"/>)</param>
    /// <param name="minPeriodicFraction">가장 큰 주기 성분의 비율이 이 값 이상일 때만 주기로 봅니다(예: 0.5)</param>
    /// <returns>요약. 프레임이 4장 미만이거나 pixelCount가 0 이하이면 FrameCount만 채웁니다</returns>
    /// <remarks>
    /// 직선 추세(블랙 레벨이나 LED 온도에 따른 느린 변화)를 뺀 뒤 주파수 성분을 소수 단위로 찾습니다.
    /// 찾는 주기는 2프레임부터 프레임 수의 절반까지입니다. 두 번 이상 반복되지 않은 주기는 추세와 구분하지 않습니다.
    /// 주기는 fps로 접힌 값입니다. PWM 주파수를 f, fps를 r라 하면 r / |f - k r| (k는 f / r에 가장 가까운 정수)
    /// 프레임입니다. 노출 시간이 PWM 주기의 정수배이면 켜짐 시간이 프레임마다 같아 주기가 나타나지 않습니다.
    /// </remarks>
    public static Flicker SummarizeFlicker(IReadOnlyList<long> sums, long pixelCount, double minPeriodicFraction)
    {
        ReadOnlySpan<long> frameSums = Sequences.AsSpan(sums);
        int n = frameSums.Length;
        var flicker = new Flicker { FrameCount = n };
        if (n < 4 || pixelCount <= 0)
        {
            return flicker;
        }
        var means = new double[n];
        for (int i = 0; i < n; ++i)
        {
            means[i] = (double)frameSums[i] / (double)pixelCount;
        }
        double total = 0.0;
        foreach (double m in means) total += m;
        double mean = total / (double)n;
        double squares = 0.0;
        foreach (double m in means) squares += (m - mean) * (m - mean);
        double lowest = means[0];
        double highest = means[0];
        foreach (double m in means)
        {
            lowest = Math.Min(lowest, m);
            highest = Math.Max(highest, m);
        }
        flicker = flicker with
        {
            Mean = mean,
            Stddev = Math.Sqrt(squares / (double)n),
            PeakToPeak = highest - lowest,
        };

        // 최소제곱 직선을 빼서 느린 변화가 긴 주기로 잡히지 않게 합니다.
        double center = (double)(n - 1) / 2.0;
        double slopeNum = 0.0;
        double slopeDen = 0.0;
        for (int i = 0; i < n; ++i)
        {
            double x = (double)i - center;
            slopeNum += x * (means[i] - mean);
            slopeDen += x * x;
        }
        double slope = slopeNum / slopeDen;
        var residual = new double[n];
        double variance = 0.0;
        for (int i = 0; i < n; ++i)
        {
            residual[i] = means[i] - mean - slope * ((double)i - center);
            variance += residual[i] * residual[i];
        }
        variance /= (double)n;
        // 정수 합계를 나눈 값의 반올림 오차보다 작은 흔들림은 일정한 밝기로 봅니다.
        if (variance <= 1e-18 * Math.Max(1.0, mean * mean))
        {
            return flicker;
        }

        // 정수 k로 봉우리를 고른 뒤 주변을 0.01, 0.0001 간격으로 좁혀 소수 주기를 찾습니다.
        double half = (double)(n / 2);
        double k = FindPeak(residual, 2.0, half, 1.0, out double power);
        k = FindPeak(residual, Math.Max(2.0, k - 1.0), Math.Min(half, k + 1.0), 0.01, out power);
        k = FindPeak(residual, Math.Max(2.0, k - 0.01), Math.Min(half, k + 0.01), 0.0001, out power);

        // 진폭 A인 사인파 하나는 |X|^2 = (A n / 2)^2, 분산 A^2 / 2라 비율이 1이 됩니다. 나이퀴스트 성분은 그 2배로 잡혀 나눕니다.
        double amplitudePower = 2.0 * power / ((double)n * (double)n);
        double nyquistScale = (n % 2 == 0 && k >= half - 1e-9) ? 0.5 : 1.0;
        double periodicFraction = Math.Min(1.0, amplitudePower * nyquistScale / variance);
        flicker = flicker with { PeriodicFraction = periodicFraction };
        if (periodicFraction >= minPeriodicFraction)
        {
            flicker = flicker with { DominantPeriodFrames = (double)n / k };
        }
        return flicker;
    }

    /// <summary>LED 변경이 영상 밝기에 나타났는지 판정합니다.</summary>
    /// <param name="before">명령 전 프레임의 평균 밝기(<see cref="MeanOf"/>), 시간 순서. 마지막 WindowFrames장을 씁니다</param>
    /// <param name="after">반영 프레임(AppliedFrame)부터의 평균 밝기, 시간 순서. 처음 WindowFrames장을 씁니다</param>
    /// <param name="sentDeltaPercent">보낸 밝기에서 직전 밝기를 뺀 값(%)</param>
    /// <param name="rule">판정 기준</param>
    /// <returns>기준값이 없거나 프레임이 모자라거나 sentDeltaPercent가 0이면 <see cref="LightResponse.Unknown"/>입니다</returns>
    /// <remarks>
    /// 명령 전 마지막 WindowFrames장과 반영 뒤 처음 WindowFrames장의 평균 밝기를 비교해, 보낸 방향으로 MinMeanChange 이상
    /// 바뀌었으면 반영으로 봅니다.
    /// 영상이 포화되었거나 자동 맞춤이 노출을 다시 맞춘 뒤의 프레임이면 LED가 바뀌어도 밝기가 바뀌지 않아 <see cref="LightResponse.NotSeen"/>이 됩니다.
    /// 장치가 LED 밝기를 돌려주지 않으므로 이 판정이 명령이 반영되었는지 볼 수 있는 유일한 근거입니다.
    /// </remarks>
    public static LightResponse JudgeLightResponse(
        IReadOnlyList<double> before, IReadOnlyList<double> after, int sentDeltaPercent, LightResponseRule rule)
    {
        if (rule.WindowFrames <= 0 || rule.MinMeanChange <= 0.0 || sentDeltaPercent == 0) return LightResponse.Unknown;
        int window = rule.WindowFrames;
        if (before.Count < window || after.Count < window) return LightResponse.Unknown;
        double beforeSum = 0.0;
        for (int i = before.Count - window; i < before.Count; ++i) beforeSum += before[i];
        double afterSum = 0.0;
        for (int i = 0; i < window; ++i) afterSum += after[i];
        double change = (afterSum - beforeSum) / (double)window;
        double toward = sentDeltaPercent > 0 ? change : -change;
        return toward >= rule.MinMeanChange ? LightResponse.Applied : LightResponse.NotSeen;
    }

    private static bool IsInside(Rect rect, FrameView frame) =>
        !rect.IsEmpty && rect.X >= 0 && rect.Y >= 0 && rect.X + rect.Width <= frame.Width &&
        rect.Y + rect.Height <= frame.Height;

    // 화소 형식마다 읽는 위치가 달라 형식 분기를 행 루프 밖에 두어 녹화 중 30fps 쓰기 스레드의 부담을 줄입니다.
    // 화소 쌍 4바이트가 U Y0 V Y1 순서라 Y는 화소마다 2바이트 간격, 1바이트 뒤에 있습니다.
    private static (long Sum, long Clipped) AccumulateUyvy(ReadOnlySpan<byte> plane, int rowStride, Rect rect, int clipLevel)
    {
        long sum = 0;
        long clipped = 0;
        for (int y = rect.Y; y < rect.Y + rect.Height; ++y)
        {
            ReadOnlySpan<byte> row = plane[(y * rowStride)..];
            for (int x = rect.X; x < rect.X + rect.Width; ++x)
            {
                int value = row[x * 2 + 1];
                sum += value;
                clipped += value >= clipLevel ? 1 : 0;
            }
        }
        return (sum, clipped);
    }

    private static (long Sum, long Clipped) AccumulateNv12(ReadOnlySpan<byte> plane, int rowStride, Rect rect, int clipLevel)
    {
        long sum = 0;
        long clipped = 0;
        for (int y = rect.Y; y < rect.Y + rect.Height; ++y)
        {
            ReadOnlySpan<byte> row = plane[(y * rowStride)..];
            for (int x = rect.X; x < rect.X + rect.Width; ++x)
            {
                int value = row[x];
                sum += value;
                clipped += value >= clipLevel ? 1 : 0;
            }
        }
        return (sum, clipped);
    }

    private static (long Sum, long Clipped) AccumulateY16(ReadOnlySpan<byte> plane, int rowStride, Rect rect, int clipLevel)
    {
        long sum = 0;
        long clipped = 0;
        for (int y = rect.Y; y < rect.Y + rect.Height; ++y)
        {
            ReadOnlySpan<byte> row = plane[(y * rowStride)..];
            for (int x = rect.X; x < rect.X + rect.Width; ++x)
            {
                int value = row[x * 2] | (row[x * 2 + 1] << 8);
                sum += value;
                clipped += value >= clipLevel ? 1 : 0;
            }
        }
        return (sum, clipped);
    }

    // 주파수 k(전체 길이에 들어가는 반복 횟수, 소수 허용) 성분의 세기 |X(k)|^2입니다.
    private static double PowerAt(double[] values, double k)
    {
        double step = 2.0 * Math.PI * k / (double)values.Length;
        double re = 0.0;
        double im = 0.0;
        for (int i = 0; i < values.Length; ++i)
        {
            double phase = step * (double)i;
            re += values[i] * Math.Cos(phase);
            im -= values[i] * Math.Sin(phase);
        }
        return re * re + im * im;
    }

    // 가장 센 성분의 k를 [low, high] 안에서 간격 step으로 찾습니다.
    private static double FindPeak(double[] values, double low, double high, double step, out double power)
    {
        double bestK = low;
        double best = -1.0;
        for (double k = low; k <= high + step * 0.5; k += step)
        {
            double p = PowerAt(values, k);
            if (p > best)
            {
                best = p;
                bestK = k;
            }
        }
        power = best;
        return bestK;
    }
}
