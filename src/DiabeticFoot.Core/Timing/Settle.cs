namespace DiabeticFoot.Core.Timing;

/// <summary>
/// 설정 변경이 영상에 반영되고 안정되기까지의 프레임 수입니다.
/// </summary>
/// <remarks>
/// 카메라마다 실측한 값을 platform이 넘깁니다(설계 문서 표 5). 프레임 수는 설정 변경 뒤 받은 프레임을 1부터 센 순서입니다.
/// </remarks>
public readonly record struct SettleRule
{
    /// <summary>새 노출로 찍힌 첫 프레임입니다.</summary>
    public int ExposureAppliedFrame { get; init; }

    public int GainAppliedFrame { get; init; }

    public int WhiteBalanceAppliedFrame { get; init; }

    /// <summary>설정 변경 뒤 블랙 레벨 변화가 끝나는 프레임입니다.</summary>
    public int BlackLevelSettledFrame { get; init; }

    /// <summary>LED 명령 뒤 새 밝기로 찍힌 첫 프레임입니다. 자동 맞춤은 이 프레임이 지난 뒤 잠급니다. 실측하지 않아 0(기다리지 않음)이 기본입니다.</summary>
    public int LightAppliedFrame { get; init; }
}

/// <summary>반영과 안정화까지 남은 프레임 수입니다. 값으로 주고받으며 함수 밖에 상태를 두지 않습니다.</summary>
public readonly record struct SettleState(int FramesUntilApplied = 0, int FramesUntilSettled = 0)
{
    /// <summary>새 설정값으로 찍힌 프레임이면 true입니다.</summary>
    public bool IsApplied => FramesUntilApplied <= 0;

    /// <summary>블랙 레벨 변화까지 끝나 녹화에 쓸 수 있는 프레임이면 true입니다.</summary>
    public bool IsSettled => FramesUntilSettled <= 0;
}

public static partial class TimingFunctions
{
    /// <summary>
    /// 설정 변경 하나를 반영해 남은 프레임 수를 다시 셉니다.
    /// </summary>
    /// <param name="state">지금 상태</param>
    /// <param name="kind">바꾼 설정값의 종류</param>
    /// <param name="rule">카메라 실측값</param>
    /// <returns>새 상태. 여러 설정값을 연달아 바꾸면 가장 늦게 끝나는 쪽을 기다립니다</returns>
    public static SettleState AfterSettingChange(SettleState state, SettingKind kind, SettleRule rule) =>
        new(
            Math.Max(state.FramesUntilApplied, AppliedFrame(kind, rule)),
            Math.Max(state.FramesUntilSettled, SettledFrame(kind, rule)));

    /// <summary>
    /// 프레임 하나를 받은 만큼 남은 프레임 수를 줄입니다. 프레임을 받은 직후에 부릅니다.
    /// </summary>
    /// <returns>새 상태. 이 상태로 방금 받은 프레임을 판정합니다(<see cref="SettleState.IsApplied"/>, <see cref="SettleState.IsSettled"/>)</returns>
    public static SettleState AfterFrame(SettleState state) =>
        new(Math.Max(state.FramesUntilApplied - 1, 0), Math.Max(state.FramesUntilSettled - 1, 0));

    private static int AppliedFrame(SettingKind kind, SettleRule rule) => kind switch
    {
        SettingKind.Exposure => rule.ExposureAppliedFrame,
        SettingKind.Gain => rule.GainAppliedFrame,
        SettingKind.WhiteBalance => rule.WhiteBalanceAppliedFrame,
        SettingKind.Light => rule.LightAppliedFrame,
        _ => 0,
    };

    // 색온도와 LED 변경 뒤 블랙 레벨 변화는 측정하지 않아, 노출과 게인처럼 기다립니다.
    private static int SettledFrame(SettingKind kind, SettleRule rule) =>
        Math.Max(AppliedFrame(kind, rule), rule.BlackLevelSettledFrame);
}
