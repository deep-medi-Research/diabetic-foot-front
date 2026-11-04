using DiabeticFoot.Core.Timing;
using static DiabeticFoot.Core.Timing.TimingFunctions;

namespace DiabeticFoot.Core.Tests.Timing;

public class RecordRateTests
{
    private const long NineHzUs = 111_111;
    private const long SecondUs = 1_000_000;

    // 수신 시각 목록을 받은 순서대로 넣고 기록한 시각을 돌려줍니다. 중앙값은 platform처럼 최근 31장으로 잽니다.
    private static List<long> Recorded(IEnumerable<long> arrivals, long periodUs = NineHzUs)
    {
        var rule = new RecordRateRule(periodUs);
        var state = new RecordRateState();
        var recent = new Queue<Timestamp>();
        var output = new List<long>();
        foreach (long t in arrivals)
        {
            recent.Enqueue(new Timestamp(t));
            if (recent.Count > 31) recent.Dequeue();
            long median = MedianIntervalUs(recent.ToArray());
            var step = AdvanceRecordRate(state, new Timestamp(t), median, rule);
            state = step.State;
            if (step.Record) output.Add(t);
        }
        return output;
    }

    // 장치 주기에 0 이상 jitterUs 이하의 수신 지연을 더한 도착 시각입니다. 같은 seed에는 같은 순서가 나옵니다.
    private static List<long> Arrivals(long periodUs, long spanUs, long jitterUs, uint seed = 7)
    {
        var arrivals = new List<long>();
        long last = -1;
        for (long ideal = 1_000_000_000; ideal < 1_000_000_000 + spanUs; ideal += periodUs)
        {
            seed = unchecked(seed * 1664525u + 1013904223u);
            long delay = jitterUs > 0 ? (long)(seed >> 8) % (jitterUs + 1) : 0;
            last = Math.Max(last + 1, ideal + delay);
            arrivals.Add(last);
        }
        return arrivals;
    }

    // 첫 프레임은 바로 기록합니다.
    [Fact]
    public void RecordsFirstFrame()
    {
        var step = AdvanceRecordRate(default, new Timestamp(5_000_000), 0, new RecordRateRule(NineHzUs));
        Assert.True(step.Record);
        Assert.Equal(5_000_000 + NineHzUs, step.State.Next.Us);
    }

    // 30Hz 장치를 30초 받으면 9.0Hz로 약 270장을 기록해 관류 분석의 하한(225장)을 넘습니다.
    [Fact]
    public void ThirtyHzForThirtySecondsKeepsAbout270()
    {
        var output = Recorded(Arrivals(33_333, 30 * SecondUs, 3_000));
        Assert.True(output.Count >= 225);
        Assert.True(output.Count >= 268);
        Assert.True(output.Count <= 271);
        double meanHz = (double)(output.Count - 1) * 1e6 / (double)(output[^1] - output[0]);
        Assert.Equal(9.0, meanHz, 0.05);
    }

    // 고를 때 기록 간격은 기록 주기에서 도착 간격 하나를 뺀 값보다 짧아지지 않습니다.
    [Fact]
    public void IntervalsStayNearPeriod()
    {
        var output = Recorded(Arrivals(33_333, 10 * SecondUs, 0));
        Assert.True(output.Count > 2);
        for (int i = 1; i < output.Count; i++) Assert.True(output[i] - output[i - 1] >= NineHzUs - 33_333);
    }

    // 9Hz 장치는 도착 간격이 변해도 받은 프레임을 모두 기록합니다.
    [Fact]
    public void NineHzDeviceWithJitterRecordsAll()
    {
        var arrivals = Arrivals(NineHzUs, 30 * SecondUs, 20_000);
        Assert.Equal(arrivals.Count, Recorded(arrivals).Count);
    }

    // 7.5Hz 장치는 기록 주기보다 느리므로 받은 프레임을 모두 기록합니다.
    [Fact]
    public void SevenAndHalfHzDeviceRecordsAll()
    {
        var arrivals = Arrivals(133_333, 30 * SecondUs, 20_000);
        Assert.Equal(arrivals.Count, Recorded(arrivals).Count);
        Assert.True(arrivals.Count >= 225);
    }

    // 실측 간격 중앙값이 기록 주기의 0.8배보다 짧을 때만 고릅니다.
    [Fact]
    public void ThinsOnlyBelowEightyPercentOfPeriod()
    {
        var rule = new RecordRateRule(100_000);
        var state = AdvanceRecordRate(default, new Timestamp(0), 0, rule).State;
        // 다음 기록 시각(100000)보다 이른 프레임입니다.
        Assert.True(AdvanceRecordRate(state, new Timestamp(60_000), 80_000, rule).Record);
        Assert.False(AdvanceRecordRate(state, new Timestamp(60_000), 79_999, rule).Record);
    }

    // 장치가 2초 멈춘 뒤 다시 보내도 밀린 장을 이어서 기록하지 않습니다.
    [Fact]
    public void DoesNotBurstAfterStall()
    {
        var arrivals = Arrivals(33_333, SecondUs, 0);
        long resume = arrivals[^1] + 2 * SecondUs;
        for (long t = resume; t < resume + SecondUs; t += 33_333) arrivals.Add(t);

        var output = Recorded(arrivals);

        Assert.True(output.Count > 10);
        for (int i = 1; i < output.Count; i++) Assert.True(output[i] - output[i - 1] >= NineHzUs - 33_333);
        int after = output.Count(t => t >= resume);
        Assert.True(after <= 10);
    }

    // 기록 주기가 0이면 거르지 않습니다.
    [Fact]
    public void ZeroPeriodRecordsAll()
    {
        var arrivals = Arrivals(33_333, SecondUs, 0);
        Assert.Equal(arrivals.Count, Recorded(arrivals, 0).Count);
    }
}
