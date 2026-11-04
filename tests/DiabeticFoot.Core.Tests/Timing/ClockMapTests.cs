using DiabeticFoot.Core.Tests.Support;
using DiabeticFoot.Core.Timing;
using static DiabeticFoot.Core.Timing.TimingFunctions;

namespace DiabeticFoot.Core.Tests.Timing;

public class ClockMapTests
{
    private const long OffsetUs = 1_234_000;
    private const double Rate = 1.0001;  // 장치 시계가 PC 시계보다 100ppm 느림

    private static Timestamp T(long us) => new(us);

    private static ClockSample Sample(long deviceUs, long hostUs) => new(T(deviceUs), T(hostUs));

    // 촬영 순간의 PC 시각입니다.
    private static long TrueHostUs(long deviceUs) => OffsetUs + (long)((double)deviceUs * Rate);

    // 30fps 프레임 count장에 수신 지연 0~8ms를 붙입니다. 분포 객체는 표준 라이브러리마다 결과가 달라 엔진 출력을 직접 씁니다.
    private static List<ClockSample> MakeSamples(long startUs, int count, uint seed)
    {
        var engine = new Mt19937(seed);
        var samples = new List<ClockSample>();
        for (int i = 0; i < count; i++)
        {
            long deviceUs = startUs + (long)i * 33_333;
            long delayUs = engine.Next() % 8_001;
            samples.Add(Sample(deviceUs, TrueHostUs(deviceUs) + delayUs));
        }
        return samples;
    }

    private const double PpgRate = 1.005;  // PPG 장치 시계가 PC 시계보다 약 0.5% 느림(실측)
    private const long PpgMinDelayUs = 2_000;

    // PPG 표본의 측정 순간을 PC 시각으로 나타냅니다. extraUs는 장치가 보내지 않은 시간만큼 뒤로 밀린 양입니다.
    private static long PpgCaptureUs(long deviceUs, long extraUs) =>
        OffsetUs + extraUs + (long)((double)deviceUs * PpgRate);

    // 측정 데이터(표본 24개, 48ms)마다 마지막 표본의 장치 시각과 PC 수신 시각입니다. 수신 지연은 2~12ms입니다.
    // 앞 early개를 받은 뒤 장치가 extraUs 동안 보내지 않습니다(다시 시작 직후의 수신 빈틈).
    private static List<ClockSample> MakePpgSamples(int messages, int early, long extraUs, uint seed)
    {
        var engine = new Mt19937(seed);
        var samples = new List<ClockSample>();
        for (int m = 0; m < messages; m++)
        {
            long deviceUs = (long)m * 48_000 + 46_000;
            long delayUs = PpgMinDelayUs + engine.Next() % 10_001;
            samples.Add(Sample(deviceUs, PpgCaptureUs(deviceUs, m >= early ? extraUs : 0) + delayUs));
        }
        return samples;
    }

    // 오프셋 1.234초, 속도 차이 100ppm, 수신 지연 0~8ms인 900장에서 공통 시각 오차가 1ms 이내입니다.
    [Fact]
    public void FitsOffsetAndRateWithinOneMillisecond()
    {
        var samples = MakeSamples(0, 900, 20261019);

        var map = FitClockMap(samples);

        Assert.Equal(Rate, map.Rate, 5e-6);
        foreach (ClockSample s in samples)
        {
            long errorUs = Timestamp.IntervalUs(T(TrueHostUs(s.Device.Us)), ToCommonTime(map, s.Device));
            Assert.InRange(errorUs, -1_000, 1_000);
            // 하한선은 표본 아래를 지나므로 공통 시각은 수신 시각보다 늦지 않습니다(반올림 1us 허용).
            Assert.True(ToCommonTime(map, s.Device).Us <= s.Host.Us + 1);
        }
        Assert.True(map.MaxResidualUs > 0);
        Assert.True(map.MaxResidualUs <= 8_100);
    }

    // 장치 시각이 부팅 뒤 수일(10^11 마이크로초 이상)이어도 같은 정확도로 맞춥니다.
    [Fact]
    public void FitsLargeDeviceTimes()
    {
        var samples = MakeSamples(400_000_000_000, 900, 7);

        var map = FitClockMap(samples);

        foreach (ClockSample s in samples)
        {
            long errorUs = Timestamp.IntervalUs(T(TrueHostUs(s.Device.Us)), ToCommonTime(map, s.Device));
            Assert.InRange(errorUs, -1_000, 1_000);
        }
    }

    // 열화상처럼 PC 수신 시각을 장치 시각으로 쓰면 항등식이 나옵니다.
    [Fact]
    public void HostTimesAsDeviceTimesGiveIdentity()
    {
        var samples = new List<ClockSample>();
        for (long i = 0; i < 100; i++) samples.Add(Sample(5_000 + i * 33_000, 5_000 + i * 33_000));

        var map = FitClockMap(samples);

        Assert.Equal(0, map.OffsetUs);
        Assert.Equal(1.0, map.Rate);
        Assert.Equal(0, map.MaxResidualUs);
    }

    // 표본이 없으면 항등식입니다.
    [Fact]
    public void EmptyGivesIdentity()
    {
        Assert.Equal(new ClockMap(), FitClockMap(Array.Empty<ClockSample>()));
        Assert.Equal(123, ToCommonTime(new ClockMap(), T(123)).Us);
    }

    // 표본이 2개 이하이면 속도는 1로 두고 가장 짧은 수신 지연에 맞춥니다.
    [Fact]
    public void TwoOrFewerSamplesFitOffsetOnly()
    {
        var one = new[] { Sample(1_000, 6_000) };
        var oneMap = FitClockMap(one);
        Assert.Equal(new ClockMap(5_000, 1.0, 0), oneMap);

        var two = new[] { Sample(1_000, 6_000), Sample(2_000, 7_500) };
        var twoMap = FitClockMap(two);
        Assert.Equal(new ClockMap(5_000, 1.0, 500), twoMap);
    }

    // 장치 시각이 거꾸로 가면(장치 시계 초기화) 속도는 1로 두고 가장 짧은 수신 지연에 맞춥니다.
    [Fact]
    public void BackwardDeviceTimeFitsOffsetOnly()
    {
        var samples = new[] { Sample(10_000, 20_000), Sample(43_333, 53_500), Sample(0, 86_666), Sample(33_333, 120_000) };

        var map = FitClockMap(samples);

        Assert.Equal(1.0, map.Rate);
        Assert.Equal(10_000, map.OffsetUs);
        Assert.Equal(120_000 - 33_333 - 10_000, map.MaxResidualUs);
    }

    // 장치 시각이 모두 같으면 기울기를 정할 수 없어 속도 1로 맞춥니다.
    [Fact]
    public void EqualDeviceTimesFitOffsetOnly()
    {
        var samples = new[] { Sample(1_000, 3_000), Sample(1_000, 2_500), Sample(1_000, 4_000) };

        var map = FitClockMap(samples);

        Assert.Equal(new ClockMap(1_500, 1.0, 1_500), map);
    }

    // PC 수신 시각이 줄어드는 입력(구한 속도가 0 이하)은 속도 1로 맞춥니다.
    [Fact]
    public void NonPositiveRateFitsOffsetOnly()
    {
        var samples = new[] { Sample(0, 30_000), Sample(10_000, 20_000), Sample(20_000, 10_000) };

        var map = FitClockMap(samples);

        Assert.Equal(1.0, map.Rate);
        Assert.Equal(-10_000, map.OffsetUs);
    }
}

public class ClockSegmentsTests
{
    private const long OffsetUs = 1_234_000;
    private const double PpgRate = 1.005;  // PPG 장치 시계가 PC 시계보다 약 0.5% 느림(실측)
    private const long PpgMinDelayUs = 2_000;

    private static Timestamp T(long us) => new(us);

    private static ClockSample Sample(long deviceUs, long hostUs) => new(T(deviceUs), T(hostUs));

    private static long TrueHostUs(long deviceUs) => OffsetUs + (long)((double)deviceUs * 1.0001);

    private static List<ClockSample> MakeSamples(long startUs, int count, uint seed)
    {
        var engine = new Mt19937(seed);
        var samples = new List<ClockSample>();
        for (int i = 0; i < count; i++)
        {
            long deviceUs = startUs + (long)i * 33_333;
            long delayUs = engine.Next() % 8_001;
            samples.Add(Sample(deviceUs, TrueHostUs(deviceUs) + delayUs));
        }
        return samples;
    }

    private static long PpgCaptureUs(long deviceUs, long extraUs) =>
        OffsetUs + extraUs + (long)((double)deviceUs * PpgRate);

    private static List<ClockSample> MakePpgSamples(int messages, int early, long extraUs, uint seed)
    {
        var engine = new Mt19937(seed);
        var samples = new List<ClockSample>();
        for (int m = 0; m < messages; m++)
        {
            long deviceUs = (long)m * 48_000 + 46_000;
            long delayUs = PpgMinDelayUs + engine.Next() % 10_001;
            samples.Add(Sample(deviceUs, PpgCaptureUs(deviceUs, m >= early ? extraUs : 0) + delayUs));
        }
        return samples;
    }

    // 수신 빈틈이 없으면 시계 구간은 하나이고 FitClockMap 결과와 같습니다.
    [Fact]
    public void NoGapIsSingleFit()
    {
        var samples = MakeSamples(0, 900, 20261021);

        var segments = FitClockSegments(samples, 50_000);

        var segment = Assert.Single(segments);
        Assert.Equal(FitClockMap(samples), segment.Map);
        Assert.Equal(samples[^1].Device, segment.LastDevice);
    }

    // 실측 모양(다시 시작 직후 측정 데이터 1~2개 뒤 평소보다 95~128ms 늦게 다음 데이터, 35초)입니다. 한 식으로 맞추면
    // 녹화 시작 무렵(3초) 공통 시각이 50ms 넘게 이르고, 시계 구간으로 나누면 빈틈 뒤 표본이 1ms 안에서 맞습니다.
    [Theory]
    [InlineData(1, 95_000)]
    [InlineData(1, 128_000)]
    [InlineData(2, 128_000)]
    public void RestartGapKeepsCommonTimeWithinOneMillisecond(int early, long extraUs)
    {
        var samples = MakePpgSamples(730, early, extraUs, 20261020);

        var segments = FitClockSegments(samples, 50_000);

        Assert.Equal(2, segments.Count);
        Assert.Equal(samples[early - 1].Device, segments[0].LastDevice);
        Assert.Equal(PpgRate, segments[1].Map.Rate, 5e-6);
        Assert.Equal(segments[0].Map.Rate, segments[1].Map.Rate);
        for (int i = early; i < samples.Count; i++)
        {
            long truth = PpgCaptureUs(samples[i].Device.Us, extraUs) + PpgMinDelayUs;
            long errorUs = ToCommonTime(segments, samples[i].Device).Us - truth;
            Assert.InRange(errorUs, -1_000, 1_000);
        }
        // 빈틈 앞 표본은 수신 시각보다 늦지 않고, 수신 지연의 흔들림(10ms) 안에서 맞습니다.
        for (int i = 0; i < early; i++)
        {
            ClockSample s = samples[i];
            long errorUs = ToCommonTime(segments, s.Device).Us - (PpgCaptureUs(s.Device.Us, 0) + PpgMinDelayUs);
            Assert.InRange(errorUs, -1, 10_001);
            Assert.True(ToCommonTime(segments, s.Device).Us <= s.Host.Us + 1);
        }

        var single = FitClockMap(samples);
        var at3s = T(62 * 48_000 + 46_000);
        Assert.True(ToCommonTime(single, at3s).Us - (PpgCaptureUs(at3s.Us, extraUs) + PpgMinDelayUs) < -50_000);
    }

    // 장치 시각마다 그 시각이 든 시계 구간의 식을 쓰고, 마지막 구간보다 뒤는 마지막 구간, 구간이 없으면 장치 시각 그대로입니다.
    [Fact]
    public void ToCommonTimePicksSegmentByDeviceTime()
    {
        var segments = new[]
        {
            new ClockSegment(T(1_000), new ClockMap(100, 1.0, 0)),
            new ClockSegment(T(5_000), new ClockMap(300, 1.0, 0)),
        };

        Assert.Equal(100, ToCommonTime(segments, T(0)).Us);
        Assert.Equal(1_100, ToCommonTime(segments, T(1_000)).Us);
        Assert.Equal(1_302, ToCommonTime(segments, T(1_002)).Us);
        Assert.Equal(9_300, ToCommonTime(segments, T(9_000)).Us);
        Assert.Equal(123, ToCommonTime(Array.Empty<ClockSegment>(), T(123)).Us);
    }

    // 빈틈 판정 시간이 0 이하이면 나누지 않고, 표본이 없으면 빈 목록이며, 장치 시각이 거꾸로 가면 FitClockMap 결과 하나입니다.
    [Fact]
    public void CasesThatDoNotSplit()
    {
        var samples = MakePpgSamples(100, 1, 128_000, 3);
        var unsplit = FitClockSegments(samples, 0);
        var only = Assert.Single(unsplit);
        Assert.Equal(FitClockMap(samples), only.Map);

        Assert.Empty(FitClockSegments(Array.Empty<ClockSample>(), 50_000));

        var backward = new[] { Sample(10_000, 20_000), Sample(43_333, 53_500), Sample(0, 286_666), Sample(33_333, 320_000) };
        var fallback = FitClockSegments(backward, 50_000);
        var single = Assert.Single(fallback);
        Assert.Equal(FitClockMap(backward), single.Map);
        Assert.Equal(T(43_333), single.LastDevice);
    }
}

public class NearestFramesTests
{
    private const long OffsetUs = 1_234_000;

    private static Timestamp T(long us) => new(us);

    // 기준 프레임마다 가장 가까운 다른 스트림 위치를 고르고, 거리가 같으면 앞 위치입니다.
    [Fact]
    public void PicksClosestAndEarlierOnTie()
    {
        var rgb = new[] { T(0), T(33_333), T(66_666), T(100_000), T(200_000) };
        var thermal = new[] { T(-5_000), T(30_000), T(50_000), T(70_000) };

        var nearest = NearestFrames(rgb, thermal);

        Assert.Equal(new[] { 0, 1, 3, 3, 3 }, nearest);

        var tie = new[] { T(0), T(20) };
        Assert.Equal(new[] { 0 }, NearestFrames(new[] { T(10) }, tie));
    }

    // 다른 스트림이 비어 있으면 빈 목록, 기준이 비어 있어도 빈 목록입니다.
    [Fact]
    public void EmptyInputsGiveEmpty()
    {
        var rgb = new[] { T(0), T(33_333) };
        Assert.Empty(NearestFrames(rgb, Array.Empty<Timestamp>()));
        Assert.Empty(NearestFrames(Array.Empty<Timestamp>(), rgb));
    }

    // RGB 30fps 900장과 열화상 30Hz를 공통 시간축으로 옮기면 RGB 프레임마다 반 주기 안의 열화상 프레임을 찾습니다.
    [Fact]
    public void MatchesStreamsOnCommonTime()
    {
        var engine = new Mt19937(11);
        var rgbSamples = new List<ClockSample>();
        for (int i = 0; i < 900; i++)
        {
            long deviceUs = (long)i * 33_333;
            long hostUs = OffsetUs + (long)((double)deviceUs * 1.0001) + engine.Next() % 8_001;
            rgbSamples.Add(new ClockSample(T(deviceUs), T(hostUs)));
        }
        var rgbMap = FitClockMap(rgbSamples);
        var rgb = rgbSamples.Select(s => ToCommonTime(rgbMap, s.Device)).ToList();
        // 열화상은 PC 수신 시각을 그대로 쓰며 RGB보다 10ms 늦게 시작합니다.
        var thermal = new List<Timestamp>();
        for (long i = 0; i < 905; i++) thermal.Add(T(OffsetUs - 10_000 + i * 33_333));

        var nearest = NearestFrames(rgb, thermal);

        Assert.Equal(rgb.Count, nearest.Count);
        for (int i = 0; i < rgb.Count; i++)
        {
            long distanceUs = Timestamp.IntervalUs(rgb[i], thermal[nearest[i]]);
            Assert.True(Math.Abs(distanceUs) <= 16_667);
        }
    }
}
