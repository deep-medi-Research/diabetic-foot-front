namespace DiabeticFoot.Core.Timing;

/// <summary>
/// 받은 프레임 중 파일에 기록할 프레임을 기록 주기로 고르는 규칙입니다.
/// </summary>
/// <param name="PeriodUs">기록 주기입니다. 0 이하이면 받은 프레임을 모두 기록합니다.</param>
/// <param name="ThinBelowPercent">
/// 실측 간격 중앙값이 기록 주기의 이 비율(퍼센트)보다 짧을 때만 고릅니다. 장치가 기록 주기와 비슷하거나 느리게 보내면
/// (제품 장치 8~9Hz) 도착 간격 변동으로 다음 기록 시각보다 조금 일찍 온 프레임을 버리지 않고 모두 기록하기 위함입니다.
/// </param>
/// <remarks>
/// 관류 분석은 열화상을 30초에 225장(7.5Hz) 이상 요구하고 시각을 읽지 않고 장 수로만 판정합니다.
/// 30Hz로 받을 때 기록 주기 111111us(9.0Hz)는 30초에 270장, 133333us는 225장, 134000us는 224장입니다.
/// </remarks>
public sealed record RecordRateRule(long PeriodUs = 0, int ThinBelowPercent = 80);

/// <summary>기록 주기 판정의 상태입니다. 녹화를 시작할 때 기본값으로 되돌립니다.</summary>
/// <param name="Started">이번 녹화에서 한 장이라도 기록했으면 true입니다.</param>
/// <param name="Next">다음 기록 시각입니다. 이 시각보다 이른 프레임은 고를 때 기록하지 않습니다.</param>
public readonly record struct RecordRateState(bool Started = false, Timestamp Next = default);

/// <summary><see cref="TimingFunctions.AdvanceRecordRate"/>의 결과입니다.</summary>
/// <param name="State">다음 상태입니다.</param>
/// <param name="Record">이 프레임을 기록하면 true입니다.</param>
public readonly record struct RecordRateStep(RecordRateState State, bool Record);

public static partial class TimingFunctions
{
    /// <summary>
    /// 프레임 하나를 기록할지 정합니다.
    /// </summary>
    /// <param name="state">지금 상태</param>
    /// <param name="time">프레임 시각(공통 시간축). 받은 순서대로 넣습니다</param>
    /// <param name="medianIntervalUs">최근 수신 간격의 중앙값. 모르면 0이며 이때는 고릅니다</param>
    /// <param name="rule">기록 주기 규칙</param>
    /// <returns>다음 상태와 기록 여부. 주기가 0 이하이거나 중앙값이 주기의 ThinBelowPercent 이상이면 항상 기록합니다</returns>
    /// <remarks>
    /// 고를 때는 첫 프레임을 기록하고, 다음 기록 시각에 주기를 더해 가므로 기록 간격의 평균이 주기와 같습니다.
    /// 프레임이 다음 기록 시각보다 한 주기 이상 늦게 오면(장치가 한동안 멈춤) 그 프레임 시각부터 다시 셉니다.
    /// 밀린 만큼 이어서 기록하면 그 구간만 프레임 간격이 좁아지기 때문입니다.
    /// </remarks>
    public static RecordRateStep AdvanceRecordRate(
        RecordRateState state, Timestamp time, long medianIntervalUs, RecordRateRule rule)
    {
        if (rule.PeriodUs <= 0)
        {
            return new RecordRateStep(state, true);
        }
        bool slowDevice = medianIntervalUs > 0 && medianIntervalUs * 100 >= rule.PeriodUs * rule.ThinBelowPercent;
        if (slowDevice || !state.Started)
        {
            // 모두 기록하는 동안에도 다음 기록 시각을 옮겨 두어야, 장치가 빨라졌을 때 바로 앞 기록에서 한 주기 뒤부터 고릅니다.
            return new RecordRateStep(new RecordRateState(true, new Timestamp(time.Us + rule.PeriodUs)), true);
        }
        if (time < state.Next)
        {
            return new RecordRateStep(state, false);
        }
        var next = new Timestamp(state.Next.Us + rule.PeriodUs);
        if (next <= time)
        {
            next = new Timestamp(time.Us + rule.PeriodUs);
        }
        return new RecordRateStep(state with { Next = next }, true);
    }
}
