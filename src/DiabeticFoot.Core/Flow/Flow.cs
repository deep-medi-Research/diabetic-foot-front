using DiabeticFoot.Core.Timing;

namespace DiabeticFoot.Core.Flow;

/// <summary>촬영 상태입니다(설계 문서 표 4).</summary>
/// <remarks>마무리(파일 닫기, 누락 검사)는 상태로 두지 않고 platform이 <see cref="ActionKind.EndRecording"/>을 받아 합니다.</remarks>
public enum Phase : byte
{
    Preview,
    Settling,
    Recording,
    Disconnected,
}

/// <summary>촬영 흐름에 넣는 사건의 종류입니다.</summary>
public enum EventKind : byte
{
    /// <summary>프레임 하나를 받음. 어느 스트림의 프레임인지는 <see cref="FlowEvent.Stream"/>입니다.</summary>
    Frame,

    /// <summary>platform이 카메라에 설정값을 설정함</summary>
    SettingChanged,
    StartPressed,
    StopPressed,
    Disconnected,
    Reconnected,

    /// <summary>제어 스레드가 잠금 요청(<see cref="ActionKind.LockSettings"/>)을 실행하고 다시 읽기까지 마침</summary>
    LockApplied,

    /// <summary>제어 스레드가 열화상 준비 요청(<see cref="ActionKind.PrepareThermal"/>)을 실행함</summary>
    ThermalPrepared,
}

/// <summary>
/// 촬영 흐름에 넣는 사건 하나입니다.
/// </summary>
/// <param name="Kind">사건의 종류</param>
/// <param name="Setting"><see cref="EventKind.SettingChanged"/>일 때만 씁니다.</param>
/// <param name="Stream"><see cref="EventKind.Frame"/>일 때만 씁니다.</param>
/// <param name="Storable">
/// <see cref="EventKind.Frame"/>일 때만 씁니다. false이면 쓰기 버퍼가 없어 이 프레임을 파일에 쓸 수 없습니다(저장 누락).
/// 목표 프레임 수는 파일에 쓴 프레임만 세므로(표 2) 쓸 수 없는 프레임은 세지 않습니다.
/// </param>
/// <param name="PpgSettleFrames">
/// <see cref="EventKind.StartPressed"/>일 때만 씁니다. 촬영 버튼 뒤 PPG 안정 대기로 녹화를 미루는 RGB 프레임 수이고, 0이면 기다리지 않습니다.
/// </param>
/// <remarks>
/// RGB가 아닌 프레임은 녹화 중에만 <see cref="ActionKind.WriteFrame"/>을 받고, 안정화 판정과 목표 프레임 수에는 들어가지 않습니다.
/// 녹화의 시작과 끝은 RGB 프레임으로만 정합니다.
/// 녹화는 안정화 프레임 수, 잠금 완료, 열화상 준비, PPG 안정 대기가 모두 끝난 RGB 프레임에서 시작합니다.
/// PPG 안정 대기는 촬영 버튼 뒤 받은 RGB 프레임을 1부터 셉니다.
/// 열화상을 쓰고(<see cref="FlowRule.PrepareThermal"/>) PPG 안정 대기가 있으면 잠금(자동 맞춤)과 열화상 준비(직접 지정)는 촬영 버튼에서
/// 하지 않고, 대기가 셔터 교정 프레임 수(<see cref="FlowRule.ThermalCalibrationFrames"/>, 잠금 뒤 안정화 프레임 수보다 작으면
/// 그 수)만큼 남은 RGB 프레임에서 요청합니다. 둘 다 열화상 셔터 교정을 하므로 교정이 대기와 함께 끝나고, 교정이 끝난 뒤 녹화까지의
/// 시간은 대기가 없을 때와 같습니다. 셔터 교정이 이 프레임 수보다 오래 걸리면 녹화는 PPG 안정 대기보다 늦게 시작합니다(설계 문서 표 10).
/// </remarks>
public sealed record FlowEvent(
    EventKind Kind = EventKind.Frame,
    SettingKind Setting = SettingKind.Exposure,
    StreamKind Stream = StreamKind.Rgb,
    bool Storable = true,
    int PpgSettleFrames = 0);

/// <summary>녹화가 끝난 이유입니다. 사이드카와 화면에 남깁니다.</summary>
public enum EndReason : byte
{
    /// <summary>목표 프레임 수를 채움</summary>
    Completed,

    /// <summary>중지 버튼</summary>
    Stopped,

    /// <summary>녹화 중 설정 변경. 앞뒤 프레임의 조건이 달라 녹화를 끝냅니다.</summary>
    SettingChanged,
    Disconnected,
}

/// <summary>촬영 흐름이 platform에 시키는 일의 종류입니다.</summary>
public enum ActionKind : byte
{
    /// <summary>자동 맞춤: 펌웨어 자동 노출을 잠금</summary>
    LockSettings,

    /// <summary>파일을 엶. 같은 <see cref="FlowFunctions.Advance"/>의 <see cref="WriteFrame"/>보다 먼저 옵니다.</summary>
    BeginRecording,

    /// <summary>이번 이벤트의 프레임을 그 스트림의 파일에 씀</summary>
    WriteFrame,

    /// <summary>이번 이벤트의 프레임을 쓰지 않고 저장 누락으로 셈(<see cref="FlowEvent.Storable"/>이 false)</summary>
    DropFrame,

    /// <summary>파일을 닫고 사이드카와 누락 검사를 함</summary>
    EndRecording,

    /// <summary>
    /// 미리보기 목표로 설정값을 모두 다시 설정하고 다시 읽음. 열화상 자동 교정 멈춤도 되돌립니다.
    /// 미리보기로 돌아가는 모든 경로(녹화 끝, 안정화 중 중지, 재연결)에서 나옵니다.
    /// </summary>
    ReapplySettings,

    /// <summary>직접 지정: RGB 설정은 그대로 두고 열화상 셔터 교정과 자동 교정 멈춤만 함</summary>
    PrepareThermal,
}

/// <summary>platform이 실행할 일 하나입니다.</summary>
/// <param name="Kind">일의 종류</param>
/// <param name="Reason"><see cref="ActionKind.EndRecording"/>일 때만 씁니다.</param>
public sealed record FlowAction(ActionKind Kind = ActionKind.WriteFrame, EndReason Reason = EndReason.Completed);

/// <summary>촬영 흐름의 규칙입니다. 촬영 선언에서 꺼냅니다(<c>FlowFunctions.RuleOf</c>).</summary>
public readonly record struct FlowRule
{
    public SettleRule Settle { get; init; }

    public long TargetFrames { get; init; }

    public SettingMode Mode { get; init; }

    /// <summary>
    /// 녹화 직전 열화상 준비(셔터 교정, 자동 교정 멈춤)를 기다림. 열화상 카메라를 쓰면 platform이 true로 정합니다.
    /// 자동 맞춤은 잠금 요청에서 함께 하므로 직접 지정에만 영향이 있습니다.
    /// </summary>
    public bool PrepareThermal { get; init; }

    /// <summary>
    /// 열화상 셔터 교정에 걸리는 RGB 프레임 수. 열화상을 쓰면 PPG 안정 대기가 이만큼(잠금 뒤 안정화 프레임 수보다 작으면 그 수)
    /// 남을 때 잠금과 열화상 준비를 요청해 교정이 대기와 함께 끝나게 합니다. platform이 <c>ThermalCamera.CalibrationUs</c>로 정합니다.
    /// </summary>
    public int ThermalCalibrationFrames { get; init; }
}

/// <summary>촬영 흐름의 상태입니다. 값으로 주고받으며 함수 밖에 상태를 두지 않습니다.</summary>
public readonly record struct FlowState
{
    public Phase Phase { get; init; }

    public SettleState Settle { get; init; }

    /// <summary>파일에 쓴 RGB 프레임 수</summary>
    public long WrittenFrames { get; init; }

    /// <summary>
    /// 잠금을 요청하고 아직 끝났다는 사건을 받지 못함. 잠금은 다른 스레드에서 실행되어 안정화 프레임 수보다 늦게 끝날 수 있습니다.
    /// </summary>
    public bool AwaitingLock { get; init; }

    /// <summary>직접 지정에서 열화상 준비를 요청하고 아직 끝났다는 사건을 받지 못함</summary>
    public bool AwaitingThermal { get; init; }

    /// <summary>마지막 LED 변경이 영상에 반영되기까지 남은 RGB 프레임 수(<see cref="SettleRule.LightAppliedFrame"/>에서 셈)</summary>
    public int FramesUntilLightApplied { get; init; }

    /// <summary>
    /// 자동 맞춤에서 촬영 버튼을 눌렀지만 LED 변경이 아직 반영되지 않았거나, 열화상을 쓰는데 PPG 안정 대기가 많이 남아 잠금 요청을 미룸
    /// </summary>
    public bool LockDeferred { get; init; }

    /// <summary>직접 지정에서 촬영 버튼을 눌렀지만 PPG 안정 대기가 많이 남아 열화상 준비 요청을 미룸</summary>
    public bool ThermalDeferred { get; init; }

    /// <summary>PPG 안정 대기가 끝나기까지 남은 RGB 프레임 수(촬영 버튼의 <see cref="FlowEvent.PpgSettleFrames"/>에서 셈)</summary>
    public int FramesUntilPpgSettled { get; init; }
}

/// <summary><see cref="FlowFunctions.Advance"/>의 결과입니다. 다음 상태와 platform이 할 일입니다.</summary>
/// <param name="State">다음 상태</param>
/// <param name="Actions">platform이 순서대로 실행합니다.</param>
public sealed record FlowStep(FlowState State, IReadOnlyList<FlowAction> Actions);

public static partial class FlowFunctions
{
    /// <summary>
    /// 사건 하나를 받아 다음 상태와 할 일을 정합니다. 촬영 흐름의 판단은 모두 이 함수에서 합니다.
    /// </summary>
    /// <param name="state">지금 상태</param>
    /// <param name="flowEvent">받은 사건</param>
    /// <param name="rule">촬영 흐름 규칙</param>
    /// <returns>다음 상태와 할 일 목록. 할 일은 platform이 순서대로 실행합니다</returns>
    /// <remarks>
    /// 같은 상태와 사건에는 항상 같은 결과를 내고, 스레드나 시계나 파일을 쓰지 않습니다.
    /// platform은 사건을 한 스레드(수신 스레드)에서 차례로 넣어야 합니다.
    /// </remarks>
    public static FlowStep Advance(FlowState state, FlowEvent flowEvent, FlowRule rule)
    {
        if (flowEvent.Kind == EventKind.Frame && flowEvent.Stream != StreamKind.Rgb)
        {
            // 보조 스트림은 RGB 녹화 구간만 저장합니다. 설정 변경은 RGB 카메라에만 하므로 안정화 프레임 수에 넣지 않습니다.
            if (state.Phase == Phase.Recording)
            {
                return new FlowStep(state, new[] { new FlowAction(ActionKind.WriteFrame) });
            }
            return Stay(state);
        }
        if (flowEvent.Kind == EventKind.Frame && state.FramesUntilLightApplied > 0)
        {
            state = state with { FramesUntilLightApplied = state.FramesUntilLightApplied - 1 };
        }
        if (flowEvent.Kind == EventKind.Frame && state.FramesUntilPpgSettled > 0)
        {
            state = state with { FramesUntilPpgSettled = state.FramesUntilPpgSettled - 1 };
        }
        if (flowEvent.Kind == EventKind.SettingChanged && flowEvent.Setting == SettingKind.Light)
        {
            state = state with
            {
                FramesUntilLightApplied = Math.Max(state.FramesUntilLightApplied, rule.Settle.LightAppliedFrame),
            };
        }
        switch (state.Phase)
        {
            case Phase.Preview:
                return OnPreview(state, flowEvent, rule);
            case Phase.Settling:
                return OnSettling(state, flowEvent, rule);
            case Phase.Recording:
                return OnRecording(state, flowEvent, rule);
            case Phase.Disconnected:
                return OnDisconnected(state, flowEvent);
        }
        return Stay(state);
    }

    /// <summary>
    /// 지금 상태에서 LED 밝기를 바꿔도 되는지 정합니다.
    /// </summary>
    /// <param name="phase">지금 촬영 상태</param>
    /// <returns>미리보기에서만 true</returns>
    /// <remarks>
    /// 안정화 중에 바꾸면 자동 맞춤이 이미 잠근 노출이 새 LED 밝기와 맞지 않고, 녹화 중에 바꾸면 앞뒤 프레임의 조건이 달라집니다.
    /// 연결 끊김 중에는 다시 열린 뒤 안정화를 셀 기준 프레임이 없습니다. 그래서 LED 변경은 미리보기에서만 받고,
    /// 자동 맞춤은 LED 변경이 반영된 뒤에 잠급니다(<see cref="SettleRule.LightAppliedFrame"/>).
    /// </remarks>
    public static bool AllowsLightChange(Phase phase) => phase == Phase.Preview;

    private static FlowStep Stay(FlowState state) => new(state, Array.Empty<FlowAction>());

    // 녹화를 끝내고 미리보기 목표로 되돌립니다. 자동 맞춤은 미리보기에서 잠금을 풀어야 다음 장면에 노출을 다시 맞춥니다.
    // 직접 지정은 두 목표가 같아 실제로 바뀌는 설정이 없습니다.
    private static FlowAction[] EndActions(EndReason reason) =>
        new[] { new FlowAction(ActionKind.EndRecording, reason), new FlowAction(ActionKind.ReapplySettings) };

    private static FlowStep End(FlowState state, Phase next, EndReason reason)
    {
        state = state with { Phase = next };
        if (next == Phase.Disconnected)
        {
            // 연결이 끊긴 카메라에는 설정할 수 없습니다. 재연결 때 다시 설정합니다.
            return new FlowStep(state, new[] { new FlowAction(ActionKind.EndRecording, reason) });
        }
        return new FlowStep(state, EndActions(reason));
    }

    // 잠금은 노출과 게인을 함께 고정하므로 두 설정 변경으로 다룹니다.
    private static SettleState AfterLock(SettleState settle, SettleRule rule)
    {
        settle = TimingFunctions.AfterSettingChange(settle, SettingKind.Exposure, rule);
        return TimingFunctions.AfterSettingChange(settle, SettingKind.Gain, rule);
    }

    // 미룬 동안 앞 촬영의 늦은 완료 사건이 기다림을 풀었을 수 있어, 요청을 보낼 때 다시 기다립니다.
    private static FlowStep RequestLock(FlowState state, FlowRule rule)
    {
        state = state with
        {
            Settle = AfterLock(state.Settle, rule.Settle),
            LockDeferred = false,
            AwaitingLock = true,
        };
        return new FlowStep(state, new[] { new FlowAction(ActionKind.LockSettings) });
    }

    private static FlowStep RequestThermal(FlowState state)
    {
        state = state with { ThermalDeferred = false, AwaitingThermal = true };
        return new FlowStep(state, new[] { new FlowAction(ActionKind.PrepareThermal) });
    }

    // 잠금과 열화상 준비는 열화상 셔터 교정을 함께 하므로, 녹화보다 일찍 하면 교정 뒤 녹화까지의 시간이 길어집니다.
    // 그래서 열화상을 쓰면 PPG 안정 대기가 교정 프레임 수(잠금 뒤 안정화 프레임 수보다 작으면 그 수)만큼 남을 때까지 미룹니다.
    private static bool IsThermalCalibrationDue(FlowState state, FlowRule rule)
    {
        int lead = Math.Max(AfterLock(default, rule.Settle).FramesUntilSettled, rule.ThermalCalibrationFrames);
        return !rule.PrepareThermal || state.FramesUntilPpgSettled <= lead;
    }

    // 안정화에서 미리보기나 연결 끊김으로 나갈 때 이번 촬영의 기다림을 모두 버립니다.
    private static FlowState ClearSettlingWaits(FlowState state) =>
        state with
        {
            AwaitingLock = false,
            AwaitingThermal = false,
            LockDeferred = false,
            ThermalDeferred = false,
            FramesUntilPpgSettled = 0,
        };

    private static FlowStep OnPreview(FlowState state, FlowEvent flowEvent, FlowRule rule)
    {
        switch (flowEvent.Kind)
        {
            case EventKind.Frame:
                state = state with { Settle = TimingFunctions.AfterFrame(state.Settle) };
                return Stay(state);
            case EventKind.SettingChanged:
                state = state with
                {
                    Settle = TimingFunctions.AfterSettingChange(state.Settle, flowEvent.Setting, rule.Settle),
                };
                return Stay(state);
            case EventKind.StartPressed:
            {
                state = state with
                {
                    Phase = Phase.Settling,
                    WrittenFrames = 0,
                    FramesUntilPpgSettled = Math.Max(flowEvent.PpgSettleFrames, 0),
                };
                if (rule.Mode == SettingMode.Auto)
                {
                    state = state with { AwaitingLock = true };
                    // 펌웨어 자동 노출이 새 LED 밝기를 본 뒤에 잠가야 노출이 녹화 조명에 맞습니다.
                    if (state.FramesUntilLightApplied > 0 || !IsThermalCalibrationDue(state, rule))
                    {
                        return Stay(state with { LockDeferred = true });
                    }
                    return RequestLock(state, rule);
                }
                // 직접 지정은 RGB 설정을 바꾸지 않으므로 안정화를 다시 세지 않고 열화상 준비만 기다립니다.
                if (rule.PrepareThermal)
                {
                    state = state with { AwaitingThermal = true };
                    if (!IsThermalCalibrationDue(state, rule))
                    {
                        return Stay(state with { ThermalDeferred = true });
                    }
                    return RequestThermal(state);
                }
                return Stay(state);
            }
            case EventKind.StopPressed:
            case EventKind.Reconnected:
            case EventKind.LockApplied:
            case EventKind.ThermalPrepared:
                return Stay(state);
            case EventKind.Disconnected:
                state = state with { Phase = Phase.Disconnected };
                return Stay(state);
        }
        return Stay(state);
    }

    private static FlowStep OnSettling(FlowState state, FlowEvent flowEvent, FlowRule rule)
    {
        switch (flowEvent.Kind)
        {
            case EventKind.Frame:
            {
                state = state with { Settle = TimingFunctions.AfterFrame(state.Settle) };
                if (state.LockDeferred && state.FramesUntilLightApplied <= 0 && IsThermalCalibrationDue(state, rule))
                {
                    return RequestLock(state, rule);
                }
                if (state.ThermalDeferred && IsThermalCalibrationDue(state, rule))
                {
                    return RequestThermal(state);
                }
                // 잠금이 끝났다는 사건을 받기 전에는 프레임 수가 차도 녹화하지 않습니다.
                if (!state.Settle.IsSettled || state.AwaitingLock || state.AwaitingThermal ||
                    state.FramesUntilPpgSettled > 0)
                {
                    return Stay(state);
                }
                // 안정화가 끝난 프레임부터 씁니다.
                state = state with { Phase = Phase.Recording, WrittenFrames = flowEvent.Storable ? 1 : 0 };
                var actions = new List<FlowAction>
                {
                    new(ActionKind.BeginRecording),
                    new(flowEvent.Storable ? ActionKind.WriteFrame : ActionKind.DropFrame),
                };
                if (state.WrittenFrames >= rule.TargetFrames)
                {
                    state = state with { Phase = Phase.Preview };
                    actions.AddRange(EndActions(EndReason.Completed));
                }
                return new FlowStep(state, actions);
            }
            case EventKind.SettingChanged:
                state = state with
                {
                    Settle = TimingFunctions.AfterSettingChange(state.Settle, flowEvent.Setting, rule.Settle),
                };
                return Stay(state);
            case EventKind.StopPressed:
                // 아직 파일을 열지 않았으므로 닫을 것은 없고, 잠금과 열화상 준비(자동 교정 멈춤)는 녹화 끝과 같게 되돌립니다.
                // 잠금이나 준비가 아직 실행 전이어도 platform은 요청을 받은 순서로 실행하므로 되돌림이 그 뒤에 옵니다.
                state = ClearSettlingWaits(state) with { Phase = Phase.Preview };
                return new FlowStep(state, new[] { new FlowAction(ActionKind.ReapplySettings) });
            case EventKind.Disconnected:
                state = ClearSettlingWaits(state) with { Phase = Phase.Disconnected };
                return Stay(state);
            case EventKind.LockApplied:
                state = state with { AwaitingLock = false };
                return Stay(state);
            case EventKind.ThermalPrepared:
                state = state with { AwaitingThermal = false };
                return Stay(state);
            case EventKind.StartPressed:
            case EventKind.Reconnected:
                return Stay(state);
        }
        return Stay(state);
    }

    private static FlowStep OnRecording(FlowState state, FlowEvent flowEvent, FlowRule rule)
    {
        switch (flowEvent.Kind)
        {
            case EventKind.Frame:
            {
                if (!flowEvent.Storable)
                {
                    return new FlowStep(state, new[] { new FlowAction(ActionKind.DropFrame) });
                }
                state = state with { WrittenFrames = state.WrittenFrames + 1 };
                var actions = new List<FlowAction> { new(ActionKind.WriteFrame) };
                if (state.WrittenFrames >= rule.TargetFrames)
                {
                    state = state with { Phase = Phase.Preview };
                    actions.AddRange(EndActions(EndReason.Completed));
                }
                return new FlowStep(state, actions);
            }
            case EventKind.SettingChanged:
                state = state with
                {
                    Settle = TimingFunctions.AfterSettingChange(state.Settle, flowEvent.Setting, rule.Settle),
                };
                return End(state, Phase.Preview, EndReason.SettingChanged);
            case EventKind.StopPressed:
                return End(state, Phase.Preview, EndReason.Stopped);
            case EventKind.Disconnected:
                return End(state, Phase.Disconnected, EndReason.Disconnected);
            case EventKind.StartPressed:
            case EventKind.Reconnected:
            case EventKind.LockApplied:
            case EventKind.ThermalPrepared:
                return Stay(state);
        }
        return Stay(state);
    }

    private static FlowStep OnDisconnected(FlowState state, FlowEvent flowEvent)
    {
        if (flowEvent.Kind != EventKind.Reconnected)
        {
            return Stay(state);
        }
        // 재연결 뒤의 설정값은 platform이 다시 설정하며 SettingChanged로 알려 줍니다.
        state = state with { Phase = Phase.Preview };
        return new FlowStep(state, new[] { new FlowAction(ActionKind.ReapplySettings) });
    }
}
