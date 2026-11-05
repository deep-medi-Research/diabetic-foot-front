using DiabeticFoot.Core.Flow;
using DiabeticFoot.Core.Timing;
using static DiabeticFoot.Core.Flow.FlowFunctions;

namespace DiabeticFoot.Core.Tests.Flow;

public class FlowTests
{
    // See3CAM 실측값(설계 문서 표 5)과 목표 900장입니다.
    private static FlowRule MakeRule(SettingMode mode) => new()
    {
        Settle = new SettleRule
        {
            ExposureAppliedFrame = 4,
            GainAppliedFrame = 3,
            WhiteBalanceAppliedFrame = 3,
            BlackLevelSettledFrame = 75,
        },
        TargetFrames = 900,
        Mode = mode,
    };

    private static readonly FlowEvent Frame = new(EventKind.Frame);
    private static readonly FlowEvent Start = new(EventKind.StartPressed);
    private static readonly FlowEvent Stop = new(EventKind.StopPressed);
    private static readonly FlowEvent ThermalFrame = new(EventKind.Frame, SettingKind.Exposure, StreamKind.Thermal);
    private static readonly FlowEvent Light = new(EventKind.SettingChanged, SettingKind.Light);

    // PPG 안정 대기 프레임 수를 실은 촬영 버튼 이벤트입니다.
    private static FlowEvent StartWithPpgSettle(int frames) => new FlowEvent(EventKind.StartPressed) with { PpgSettleFrames = frames };

    // 이벤트를 차례로 넣으며 상태와 모든 동작을 모읍니다. 동작마다 몇 번째 이벤트에서 나왔는지 남깁니다.
    private sealed class EventLog
    {
        public FlowState State { get; private set; }

        public List<(int At, FlowAction Action)> Actions { get; } = new();

        public int Events { get; private set; }

        public void Feed(FlowEvent flowEvent, FlowRule rule, int times = 1)
        {
            for (int i = 0; i < times; ++i)
            {
                var step = Advance(State, flowEvent, rule);
                State = step.State;
                foreach (var a in step.Actions)
                {
                    Actions.Add((Events, a));
                }
                ++Events;
            }
        }

        public int Count(ActionKind kind)
        {
            int n = 0;
            foreach (var (_, a) in Actions)
            {
                n += a.Kind == kind ? 1 : 0;
            }
            return n;
        }

        // 마지막 녹화 끝 동작입니다.
        public FlowAction LastEnd()
        {
            for (int i = Actions.Count - 1; i >= 0; --i)
            {
                if (Actions[i].Action.Kind == ActionKind.EndRecording)
                {
                    return Actions[i].Action;
                }
            }
            return new FlowAction();
        }

        public int FirstAt(ActionKind kind)
        {
            foreach (var (at, a) in Actions)
            {
                if (a.Kind == kind)
                {
                    return at;
                }
            }
            return -1;
        }
    }

    // 직접 지정에서 설정을 바꾼 뒤 75프레임 지나 촬영 버튼을 누르면, 다음 프레임부터 900장을 쓰고 끝납니다.
    [Fact]
    public void ManualRecordsNineHundredFramesAfterSettle()
    {
        var rule = MakeRule(SettingMode.Manual);
        var run = new EventLog();
        run.Feed(new(EventKind.SettingChanged, SettingKind.Exposure), rule);
        run.Feed(Frame, rule, 100);
        run.Feed(Start, rule);

        run.Feed(Frame, rule, 905);

        Assert.Equal(1, run.Count(ActionKind.BeginRecording));
        Assert.Equal(102, run.FirstAt(ActionKind.BeginRecording));
        Assert.Equal(900, run.Count(ActionKind.WriteFrame));
        Assert.Equal(1, run.Count(ActionKind.EndRecording));
        Assert.Equal(EndReason.Completed, run.LastEnd().Reason);
        // 끝난 뒤 미리보기 목표로 되돌리는 동작이 이어집니다.
        Assert.Equal(ActionKind.ReapplySettings, run.Actions[^1].Action.Kind);
        Assert.Equal(Phase.Preview, run.State.Phase);
        Assert.Equal(900, run.State.WrittenFrames);
    }

    // 쓰기 버퍼가 없어 저장하지 못한 프레임은 목표 프레임 수에 넣지 않습니다(표 2: 파일에 쓴 프레임만 셈).
    // 녹화 중 3장을 저장하지 못하면 3장 더 받아 파일에 900장을 채웁니다.
    [Fact]
    public void UnstoredFramesDoNotCountTowardTarget()
    {
        var rule = MakeRule(SettingMode.Manual);
        var unstored = new FlowEvent(EventKind.Frame, SettingKind.Exposure, StreamKind.Rgb, false);
        var run = new EventLog();
        run.Feed(Frame, rule, 100);
        run.Feed(Start, rule);
        run.Feed(Frame, rule, 10);
        run.Feed(unstored, rule, 3);
        run.Feed(Frame, rule, 889);
        Assert.Equal(0, run.Count(ActionKind.EndRecording));
        Assert.Equal(899, run.State.WrittenFrames);

        run.Feed(Frame, rule);

        Assert.Equal(900, run.Count(ActionKind.WriteFrame));
        Assert.Equal(3, run.Count(ActionKind.DropFrame));
        Assert.Equal(1, run.Count(ActionKind.EndRecording));
        Assert.Equal(EndReason.Completed, run.LastEnd().Reason);
        Assert.Equal(900, run.State.WrittenFrames);
    }

    // 안정화가 끝난 첫 프레임을 저장하지 못해도 녹화는 시작하고, 그 프레임은 세지 않습니다.
    [Fact]
    public void UnstoredFirstFrameStartsRecordingWithoutCounting()
    {
        var rule = MakeRule(SettingMode.Manual) with { TargetFrames = 2 };
        var unstored = new FlowEvent(EventKind.Frame, SettingKind.Exposure, StreamKind.Rgb, false);
        var run = new EventLog();
        run.Feed(Frame, rule, 100);
        run.Feed(Start, rule);

        run.Feed(unstored, rule);
        Assert.Equal(Phase.Recording, run.State.Phase);
        Assert.Equal(0, run.State.WrittenFrames);
        Assert.Equal(1, run.Count(ActionKind.BeginRecording));
        Assert.Equal(1, run.Count(ActionKind.DropFrame));
        Assert.Equal(0, run.Count(ActionKind.WriteFrame));

        run.Feed(Frame, rule, 2);
        Assert.Equal(2, run.Count(ActionKind.WriteFrame));
        Assert.Equal(1, run.Count(ActionKind.EndRecording));
    }

    // 미리보기 중에는 저장 여부와 관계없이 쓰기도 누락도 지시하지 않습니다.
    [Fact]
    public void UnstoredFrameInPreviewDoesNothing()
    {
        var rule = MakeRule(SettingMode.Manual);
        var unstored = new FlowEvent(EventKind.Frame, SettingKind.Exposure, StreamKind.Rgb, false);
        var run = new EventLog();
        run.Feed(unstored, rule, 5);
        Assert.Empty(run.Actions);
    }

    // 설정을 바꾸자마자 촬영 버튼을 누르면 바꾼 뒤 75번째 프레임부터 씁니다.
    [Fact]
    public void ManualWaitsForSettleAfterLastChange()
    {
        var rule = MakeRule(SettingMode.Manual);
        var run = new EventLog();
        run.Feed(new(EventKind.SettingChanged, SettingKind.Gain), rule);
        run.Feed(Start, rule);

        run.Feed(Frame, rule, 80);

        // 이벤트 0: 설정 변경, 1: 촬영 버튼, 2부터 프레임. 75번째 프레임은 이벤트 76입니다.
        Assert.Equal(76, run.FirstAt(ActionKind.BeginRecording));
    }

    // 열화상을 쓰는 직접 지정은 촬영 버튼에서 열화상 준비를 요청하고, 준비가 끝났다는 사건 전에는 녹화하지 않습니다.
    [Fact]
    public void ManualWithThermalWaitsForThermalPrepared()
    {
        var rule = MakeRule(SettingMode.Manual) with { PrepareThermal = true };
        var run = new EventLog();
        run.Feed(Frame, rule, 100);
        run.Feed(Start, rule); // 이벤트 100
        run.Feed(Frame, rule, 50);
        Assert.Equal(0, run.Count(ActionKind.BeginRecording));

        run.Feed(new(EventKind.ThermalPrepared), rule); // 이벤트 151
        run.Feed(Frame, rule, 1);

        Assert.Equal(100, run.FirstAt(ActionKind.PrepareThermal));
        Assert.Equal(1, run.Count(ActionKind.PrepareThermal));
        // 직접 지정의 설정 동작은 그대로입니다: 잠금도, 안정화 재시작도 없습니다.
        Assert.Equal(0, run.Count(ActionKind.LockSettings));
        Assert.Equal(152, run.FirstAt(ActionKind.BeginRecording));
    }

    // 열화상을 쓰는 직접 지정에서도 안정화 프레임 수는 마지막 설정 변경부터 셉니다.
    [Fact]
    public void ManualWithThermalStillWaitsForSettle()
    {
        var rule = MakeRule(SettingMode.Manual) with { PrepareThermal = true };
        var run = new EventLog();
        run.Feed(new(EventKind.SettingChanged, SettingKind.Gain), rule);
        run.Feed(Start, rule);
        run.Feed(new(EventKind.ThermalPrepared), rule); // 이벤트 2

        run.Feed(Frame, rule, 80);

        // 이벤트 0: 설정 변경, 1: 촬영 버튼, 2: 준비 끝, 3부터 프레임. 75번째 프레임은 이벤트 77입니다.
        Assert.Equal(77, run.FirstAt(ActionKind.BeginRecording));
    }

    // 열화상 준비 중에 중지하면 미리보기로 돌아가고, 다음 촬영 버튼에서 준비를 다시 요청합니다.
    [Fact]
    public void ManualStopDuringThermalPrepareClearsWait()
    {
        var rule = MakeRule(SettingMode.Manual) with { PrepareThermal = true };
        var run = new EventLog();
        run.Feed(Frame, rule, 100);
        run.Feed(Start, rule);
        run.Feed(Stop, rule);
        Assert.Equal(Phase.Preview, run.State.Phase);
        Assert.False(run.State.AwaitingThermal);

        run.Feed(Start, rule);
        Assert.Equal(2, run.Count(ActionKind.PrepareThermal));
    }

    // 자동 맞춤은 잠금 요청에서 열화상도 준비하므로 열화상 준비를 따로 요청하지 않습니다.
    [Fact]
    public void AutoWithThermalUsesLockOnly()
    {
        var rule = MakeRule(SettingMode.Auto) with { PrepareThermal = true };
        var run = new EventLog();
        run.Feed(Frame, rule, 10);
        run.Feed(Start, rule);
        run.Feed(new(EventKind.LockApplied), rule);
        run.Feed(Frame, rule, 80);

        Assert.Equal(0, run.Count(ActionKind.PrepareThermal));
        Assert.Equal(1, run.Count(ActionKind.LockSettings));
        Assert.Equal(1, run.Count(ActionKind.BeginRecording));
    }

    // 자동 맞춤에서는 촬영 버튼에서 잠금을 요청하고, 잠금 뒤 75번째 프레임부터 씁니다.
    [Fact]
    public void AutoLocksThenWaitsForSettle()
    {
        var rule = MakeRule(SettingMode.Auto);
        var run = new EventLog();
        run.Feed(Frame, rule, 200);
        run.Feed(Start, rule);
        run.Feed(new(EventKind.LockApplied), rule); // 이벤트 201

        run.Feed(Frame, rule, 80);

        Assert.Equal(200, run.FirstAt(ActionKind.LockSettings));
        Assert.Equal(201 + 75, run.FirstAt(ActionKind.BeginRecording));
    }

    // 잠금이 끝났다는 사건이 오기 전에는 안정화 프레임 수가 지나도 녹화하지 않습니다.
    [Fact]
    public void AutoWaitsForLockAppliedEvent()
    {
        var rule = MakeRule(SettingMode.Auto);
        var run = new EventLog();
        run.Feed(Start, rule);
        run.Feed(Frame, rule, 200);
        Assert.Equal(0, run.Count(ActionKind.BeginRecording));

        run.Feed(new(EventKind.LockApplied), rule); // 이벤트 201
        run.Feed(Frame, rule, 1);

        Assert.Equal(202, run.FirstAt(ActionKind.BeginRecording));
    }

    // 안정화 중에 설정을 바꾸면 그 시점부터 75프레임을 다시 기다립니다.
    [Fact]
    public void ChangeDuringSettlingRestartsWait()
    {
        var rule = MakeRule(SettingMode.Manual);
        var run = new EventLog();
        run.Feed(new(EventKind.SettingChanged, SettingKind.Exposure), rule);
        run.Feed(Start, rule);
        run.Feed(Frame, rule, 50);
        run.Feed(new(EventKind.SettingChanged, SettingKind.Exposure), rule); // 이벤트 52

        run.Feed(Frame, rule, 80);

        Assert.Equal(52 + 75, run.FirstAt(ActionKind.BeginRecording));
    }

    // 안정화 중 중지는 파일을 열기 전이므로 닫는 동작 없이 미리보기로 돌아가고, 잠금은 녹화 끝과 같게 되돌립니다.
    [Fact]
    public void StopDuringSettlingCancelsWithoutFile()
    {
        var rule = MakeRule(SettingMode.Auto);
        var run = new EventLog();
        run.Feed(Start, rule);
        run.Feed(Frame, rule, 10);

        run.Feed(Stop, rule); // 이벤트 11
        run.Feed(Frame, rule, 100);

        Assert.Equal(Phase.Preview, run.State.Phase);
        Assert.Equal(0, run.Count(ActionKind.BeginRecording));
        Assert.Equal(0, run.Count(ActionKind.EndRecording));
        Assert.Equal(1, run.Count(ActionKind.LockSettings));
        Assert.Equal(1, run.Count(ActionKind.ReapplySettings));
        Assert.Equal(11, run.FirstAt(ActionKind.ReapplySettings));
    }

    // 직접 지정에서 열화상 준비를 기다리는 중에 중지해도 준비(자동 교정 멈춤)를 되돌리는 동작이 나옵니다.
    [Fact]
    public void StopWhilePreparingThermalReappliesSettings()
    {
        var rule = MakeRule(SettingMode.Manual) with { PrepareThermal = true };
        var run = new EventLog();
        run.Feed(Start, rule);
        run.Feed(Frame, rule, 5);

        run.Feed(Stop, rule); // 이벤트 6
        run.Feed(new(EventKind.ThermalPrepared), rule);
        run.Feed(Frame, rule, 100);

        Assert.Equal(Phase.Preview, run.State.Phase);
        Assert.Equal(1, run.Count(ActionKind.PrepareThermal));
        Assert.Equal(0, run.Count(ActionKind.BeginRecording));
        Assert.Equal(1, run.Count(ActionKind.ReapplySettings));
        Assert.Equal(6, run.FirstAt(ActionKind.ReapplySettings));
    }

    private sealed record ReturnPath(SettingMode Mode, bool Recorded, IReadOnlyList<(FlowEvent Event, int Times)> AfterStart);

    // 잠금이나 열화상 준비를 요청한 뒤 미리보기로 돌아가는 모든 경로(완료, 녹화 중 중지, 안정화 중 중지, 끊김 뒤 재연결)에서
    // 그 요청보다 뒤에 되돌림이 나옵니다. platform은 이 순서로 자동 교정 멈춤을 겁니다.
    [Fact]
    public void EveryReturnToPreviewAfterHoldReappliesSettings()
    {
        var disconnected = new FlowEvent(EventKind.Disconnected);
        var reconnected = new FlowEvent(EventKind.Reconnected);
        var locked = new FlowEvent(EventKind.LockApplied);
        var paths = new List<ReturnPath>
        {
            new(SettingMode.Auto, true, new[] { (locked, 1), (Frame, 1000) }),
            new(SettingMode.Auto, true, new[] { (locked, 1), (Frame, 200), (Stop, 1) }),
            new(SettingMode.Auto, false, new[] { (Frame, 10), (Stop, 1) }),
            new(SettingMode.Auto, false, new[] { (Frame, 10), (disconnected, 1), (reconnected, 1) }),
            new(SettingMode.Manual, false, new[] { (Stop, 1) }),
            new(SettingMode.Manual, true, new[] { (new FlowEvent(EventKind.ThermalPrepared), 1), (Frame, 300), (Stop, 1) }),
        };
        for (int i = 0; i < paths.Count; ++i)
        {
            var rule = MakeRule(paths[i].Mode) with { PrepareThermal = true };
            var run = new EventLog();
            run.Feed(Start, rule);
            foreach (var (flowEvent, times) in paths[i].AfterStart)
            {
                run.Feed(flowEvent, rule, times);
            }
            run.Feed(Frame, rule, 5);
            Assert.True(run.State.Phase == Phase.Preview, $"경로 {i}: 상태 {run.State.Phase}");
            int ends = run.Count(ActionKind.EndRecording);
            Assert.True(ends == (paths[i].Recorded ? 1 : 0), $"경로 {i}: 녹화 끝 {ends}번");
            int holds = run.Count(ActionKind.LockSettings) + run.Count(ActionKind.PrepareThermal);
            Assert.True(holds == 1, $"경로 {i}: 잠금과 열화상 준비 요청 {holds}번");
            int holdIndex = -1;
            int lastReapply = -1;
            for (int k = 0; k < run.Actions.Count; ++k)
            {
                var kind = run.Actions[k].Action.Kind;
                if (kind == ActionKind.LockSettings || kind == ActionKind.PrepareThermal)
                {
                    holdIndex = k;
                }
                if (kind == ActionKind.ReapplySettings)
                {
                    lastReapply = k;
                }
            }
            Assert.True(lastReapply > holdIndex, $"경로 {i}: 되돌림 {lastReapply}, 요청 {holdIndex}");
        }
    }

    // 녹화 중 중지는 그때까지 쓴 장 수를 남기고 끝냅니다.
    [Fact]
    public void StopDuringRecordingEnds()
    {
        var rule = MakeRule(SettingMode.Manual);
        var run = new EventLog();
        run.Feed(Start, rule);
        run.Feed(Frame, rule, 300);

        run.Feed(Stop, rule);

        Assert.Equal(1, run.Count(ActionKind.EndRecording));
        Assert.Equal(EndReason.Stopped, run.LastEnd().Reason);
        Assert.Equal(300, run.State.WrittenFrames);
        Assert.Equal(Phase.Preview, run.State.Phase);
    }

    // 녹화 중 설정이 바뀌면 앞뒤 프레임의 조건이 달라지므로 녹화를 끝냅니다.
    [Fact]
    public void SettingChangeDuringRecordingEnds()
    {
        var rule = MakeRule(SettingMode.Manual);
        var run = new EventLog();
        run.Feed(Start, rule);
        run.Feed(Frame, rule, 10);

        run.Feed(new(EventKind.SettingChanged, SettingKind.Gain), rule);

        Assert.Equal(ActionKind.EndRecording, run.LastEnd().Kind);
        Assert.Equal(EndReason.SettingChanged, run.LastEnd().Reason);
    }

    // 녹화 중 연결이 끊기면 녹화를 끝내고, 재연결 때 설정값을 다시 설정합니다.
    [Fact]
    public void DisconnectEndsAndReconnectReappliesSettings()
    {
        var rule = MakeRule(SettingMode.Manual);
        var run = new EventLog();
        run.Feed(Start, rule);
        run.Feed(Frame, rule, 10);

        run.Feed(new(EventKind.Disconnected), rule);
        run.Feed(Frame, rule, 5);
        run.Feed(Start, rule);
        Assert.Equal(Phase.Disconnected, run.State.Phase);
        run.Feed(new(EventKind.Reconnected), rule);

        Assert.Equal(1, run.Count(ActionKind.EndRecording));
        Assert.Equal(10, run.Count(ActionKind.WriteFrame));
        Assert.Equal(ActionKind.ReapplySettings, run.Actions[^1].Action.Kind);
        Assert.Equal(Phase.Preview, run.State.Phase);
    }

    // 녹화 중 촬영 버튼을 다시 눌러도 아무 일도 일어나지 않습니다.
    [Fact]
    public void StartDuringRecordingIsIgnored()
    {
        var rule = MakeRule(SettingMode.Auto);
        var run = new EventLog();
        run.Feed(Start, rule);
        run.Feed(new(EventKind.LockApplied), rule);
        run.Feed(Frame, rule, 100);
        int before = run.Actions.Count;

        run.Feed(Start, rule);

        Assert.Equal(before, run.Actions.Count);
        Assert.Equal(Phase.Recording, run.State.Phase);
    }

    // RGB와 열화상을 번갈아 넣으면 열화상은 녹화 중에만 쓰고, 녹화는 RGB 900장에서 끝납니다.
    [Fact]
    public void AuxiliaryFramesWrittenOnlyWhileRecording()
    {
        var rule = MakeRule(SettingMode.Manual);
        FlowState state = default;
        int rgbWrites = 0;
        int thermalWrites = 0;
        int thermalWritesOutside = 0;
        int ends = 0;
        for (int i = 0; i < 1100; ++i)
        {
            if (i == 10)
            {
                state = Advance(state, Start, rule).State;
            }
            foreach (var flowEvent in new[] { Frame, ThermalFrame })
            {
                bool recordingBefore = state.Phase == Phase.Recording;
                var step = Advance(state, flowEvent, rule);
                state = step.State;
                foreach (var a in step.Actions)
                {
                    if (a.Kind == ActionKind.EndRecording)
                    {
                        ++ends;
                    }
                    if (a.Kind != ActionKind.WriteFrame)
                    {
                        continue;
                    }
                    if (flowEvent.Stream == StreamKind.Rgb)
                    {
                        ++rgbWrites;
                    }
                    else
                    {
                        ++thermalWrites;
                        thermalWritesOutside += recordingBefore ? 0 : 1;
                    }
                }
            }
        }

        Assert.Equal(900, rgbWrites);
        Assert.Equal(1, ends);
        Assert.Equal(900, state.WrittenFrames);
        Assert.Equal(Phase.Preview, state.Phase);
        // 녹화를 연 RGB 프레임 뒤의 열화상부터 녹화를 끝낸 RGB 프레임 앞의 열화상까지 899장입니다.
        Assert.Equal(899, thermalWrites);
        Assert.Equal(0, thermalWritesOutside);
    }

    // 열화상 프레임은 안정화 프레임 수와 목표 프레임 수에 들어가지 않습니다.
    [Fact]
    public void AuxiliaryFramesDoNotAdvanceSettleOrCount()
    {
        var rule = MakeRule(SettingMode.Manual);
        var run = new EventLog();
        run.Feed(new(EventKind.SettingChanged, SettingKind.Exposure), rule);
        run.Feed(Start, rule);
        run.Feed(ThermalFrame, rule, 500);
        Assert.Equal(Phase.Settling, run.State.Phase);
        Assert.Empty(run.Actions);

        run.Feed(Frame, rule, 75);
        Assert.Equal(Phase.Recording, run.State.Phase);
        run.Feed(ThermalFrame, rule, 2000);

        Assert.Equal(1, run.State.WrittenFrames);
        Assert.Equal(Phase.Recording, run.State.Phase);
        Assert.Equal(1 + 2000, run.Count(ActionKind.WriteFrame));
        Assert.Equal(0, run.Count(ActionKind.EndRecording));
    }

    // LED 변경은 미리보기에서만 받습니다.
    [Fact]
    public void LightChangeAllowedOnlyInPreview()
    {
        Assert.True(AllowsLightChange(Phase.Preview));
        Assert.False(AllowsLightChange(Phase.Settling));
        Assert.False(AllowsLightChange(Phase.Recording));
        Assert.False(AllowsLightChange(Phase.Disconnected));
    }

    // 미리보기 중 LED를 바꾸고 바로 촬영 버튼을 누르면(직접 지정) LED 변경 뒤 75번째 프레임부터 씁니다.
    [Fact]
    public void LightChangeRestartsSettle()
    {
        var rule = MakeRule(SettingMode.Manual);
        var run = new EventLog();
        run.Feed(Frame, rule, 100);
        run.Feed(Light, rule); // 이벤트 100
        run.Feed(Start, rule); // 이벤트 101

        run.Feed(Frame, rule, 80);

        Assert.Equal(101 + 75, run.FirstAt(ActionKind.BeginRecording));
    }

    // 안정화 중에 LED 변경 사건이 오면 그 시점부터 75프레임을 다시 기다립니다.
    [Fact]
    public void LightChangeDuringSettlingRestartsWait()
    {
        var rule = MakeRule(SettingMode.Manual);
        var run = new EventLog();
        run.Feed(new(EventKind.SettingChanged, SettingKind.Gain), rule);
        run.Feed(Start, rule);
        run.Feed(Frame, rule, 30);
        run.Feed(Light, rule); // 이벤트 32
        run.Feed(Frame, rule, 80);

        Assert.Equal(32 + 75, run.FirstAt(ActionKind.BeginRecording));
    }

    // 녹화 중 LED 변경 사건은 다른 설정 변경처럼 녹화를 끝냅니다.
    [Fact]
    public void LightChangeDuringRecordingEnds()
    {
        var rule = MakeRule(SettingMode.Manual);
        var run = new EventLog();
        run.Feed(Start, rule);
        run.Feed(Frame, rule, 10);
        Assert.Equal(Phase.Recording, run.State.Phase);

        run.Feed(Light, rule);

        Assert.Equal(ActionKind.EndRecording, run.LastEnd().Kind);
        Assert.Equal(EndReason.SettingChanged, run.LastEnd().Reason);
        Assert.Equal(Phase.Preview, run.State.Phase);
    }

    // 자동 맞춤에서 LED 변경이 아직 반영되지 않았으면 반영 프레임까지 잠금을 미루고, 잠금 뒤 75프레임을 기다립니다.
    [Fact]
    public void AutoLocksAfterLightApplied()
    {
        var rule = MakeRule(SettingMode.Auto);
        rule = rule with { Settle = rule.Settle with { LightAppliedFrame = 5 } };
        var run = new EventLog();
        run.Feed(Frame, rule, 10);
        run.Feed(Light, rule); // 이벤트 10
        run.Feed(Start, rule); // 이벤트 11
        Assert.Equal(0, run.Count(ActionKind.LockSettings));
        Assert.Equal(Phase.Settling, run.State.Phase);

        run.Feed(Frame, rule, 10); // 이벤트 12~21. LED 변경 뒤 5번째 프레임은 이벤트 16
        Assert.Equal(1, run.Count(ActionKind.LockSettings));
        Assert.Equal(16, run.FirstAt(ActionKind.LockSettings));

        run.Feed(new(EventKind.LockApplied), rule); // 이벤트 22
        run.Feed(Frame, rule, 80);
        // 잠금은 이벤트 16의 프레임에서 셉니다. 그 뒤 75번째 프레임은 이벤트 16 + 75 + 1(잠금 완료 사건 하나)
        Assert.Equal(16 + 75 + 1, run.FirstAt(ActionKind.BeginRecording));
    }

    // LED 반영 프레임이 이미 지났으면 자동 맞춤은 촬영 버튼에서 바로 잠급니다.
    [Fact]
    public void AutoLocksImmediatelyWhenLightAlreadyApplied()
    {
        var rule = MakeRule(SettingMode.Auto);
        rule = rule with { Settle = rule.Settle with { LightAppliedFrame = 5 } };
        var run = new EventLog();
        run.Feed(Light, rule);
        run.Feed(Frame, rule, 5);
        run.Feed(Start, rule); // 이벤트 6

        Assert.Equal(6, run.FirstAt(ActionKind.LockSettings));
    }

    // 잠금을 미룬 채 중지하면 잠그지 않고 미리보기로 돌아갑니다.
    [Fact]
    public void StopWhileLockDeferredNeverLocks()
    {
        var rule = MakeRule(SettingMode.Auto);
        rule = rule with { Settle = rule.Settle with { LightAppliedFrame = 5 } };
        var run = new EventLog();
        run.Feed(Light, rule);
        run.Feed(Start, rule);
        run.Feed(Frame, rule, 2);
        run.Feed(Stop, rule);

        run.Feed(Frame, rule, 20);

        Assert.Equal(0, run.Count(ActionKind.LockSettings));
        Assert.Equal(Phase.Preview, run.State.Phase);
        Assert.False(run.State.LockDeferred);
    }

    // PPG 안정 대기가 있으면 촬영 버튼 뒤 그 프레임 수째 RGB 프레임에서 녹화를 시작합니다.
    [Fact]
    public void ManualWaitsForPpgSettle()
    {
        var rule = MakeRule(SettingMode.Manual);
        var run = new EventLog();
        run.Feed(StartWithPpgSettle(360), rule); // 이벤트 0
        run.Feed(Frame, rule, 359);
        Assert.Equal(Phase.Settling, run.State.Phase);
        Assert.Equal(1, run.State.FramesUntilPpgSettled);

        run.Feed(Frame, rule); // 이벤트 360

        Assert.Equal(360, run.FirstAt(ActionKind.BeginRecording));
        Assert.Equal(Phase.Recording, run.State.Phase);
    }

    // 열화상을 쓰는 자동 맞춤은 PPG 안정 대기가 75프레임 남을 때 잠그고, 잠금 뒤 75프레임과 PPG 안정 대기가 같은 프레임에서 끝납니다.
    [Fact]
    public void AutoWithThermalDefersLockUntilPpgNearlySettled()
    {
        var rule = MakeRule(SettingMode.Auto) with { PrepareThermal = true };
        var run = new EventLog();
        run.Feed(StartWithPpgSettle(360), rule); // 이벤트 0
        run.Feed(Frame, rule, 284); // 이벤트 1~284
        Assert.Equal(0, run.Count(ActionKind.LockSettings));
        Assert.True(run.State.LockDeferred);

        run.Feed(Frame, rule); // 이벤트 285. PPG 안정 대기가 75프레임 남음
        Assert.Equal(285, run.FirstAt(ActionKind.LockSettings));

        run.Feed(new(EventKind.LockApplied), rule); // 이벤트 286
        run.Feed(Frame, rule, 100);
        // 360번째 RGB 프레임은 잠금 완료 사건 하나 뒤라 이벤트 361입니다
        Assert.Equal(361, run.FirstAt(ActionKind.BeginRecording));
    }

    // 직접 지정의 열화상 준비도 PPG 안정 대기가 75프레임 남을 때 요청합니다.
    [Fact]
    public void ManualDefersThermalPrepareUntilPpgNearlySettled()
    {
        var rule = MakeRule(SettingMode.Manual) with { PrepareThermal = true };
        var run = new EventLog();
        run.Feed(StartWithPpgSettle(360), rule); // 이벤트 0
        run.Feed(Frame, rule, 284);
        Assert.Equal(0, run.Count(ActionKind.PrepareThermal));
        Assert.True(run.State.ThermalDeferred);

        run.Feed(Frame, rule); // 이벤트 285
        Assert.Equal(285, run.FirstAt(ActionKind.PrepareThermal));

        run.Feed(new(EventKind.ThermalPrepared), rule); // 이벤트 286
        run.Feed(Frame, rule, 100);
        Assert.Equal(361, run.FirstAt(ActionKind.BeginRecording));
    }

    // 셔터 교정 프레임 수가 잠금 뒤 안정화 프레임 수보다 길면, 직접 지정의 열화상 준비는 PPG 안정 대기가 그만큼 남을 때 요청하고
    // 교정이 그 안에 끝나면 PPG 안정 대기가 끝난 프레임에서 녹화합니다.
    [Fact]
    public void ManualPreparesThermalCalibrationFramesBeforePpgSettled()
    {
        var rule = MakeRule(SettingMode.Manual) with { PrepareThermal = true, ThermalCalibrationFrames = 147 };
        var run = new EventLog();
        run.Feed(StartWithPpgSettle(450), rule); // 이벤트 0
        run.Feed(Frame, rule, 302); // 이벤트 1~302
        Assert.Equal(0, run.Count(ActionKind.PrepareThermal));

        run.Feed(Frame, rule); // 이벤트 303. PPG 안정 대기가 147프레임 남음
        Assert.Equal(303, run.FirstAt(ActionKind.PrepareThermal));

        run.Feed(Frame, rule, 140); // 이벤트 304~443. 교정 중
        run.Feed(new(EventKind.ThermalPrepared), rule); // 이벤트 444
        run.Feed(Frame, rule, 10);
        // 450번째 RGB 프레임은 준비 완료 사건 하나 뒤라 이벤트 451입니다
        Assert.Equal(451, run.FirstAt(ActionKind.BeginRecording));
    }

    // 자동 맞춤의 잠금도 PPG 안정 대기가 셔터 교정 프레임 수만큼 남을 때 요청하고, 교정이 대기보다 늦게 끝나면 끝난 뒤 녹화합니다.
    [Fact]
    public void AutoLocksCalibrationFramesBeforePpgSettledAndWaitsForLock()
    {
        var rule = MakeRule(SettingMode.Auto) with { PrepareThermal = true, ThermalCalibrationFrames = 147 };
        var run = new EventLog();
        run.Feed(StartWithPpgSettle(450), rule); // 이벤트 0
        run.Feed(Frame, rule, 303); // 이벤트 1~303
        Assert.Equal(303, run.FirstAt(ActionKind.LockSettings));

        run.Feed(Frame, rule, 160); // 이벤트 304~463. 대기는 450번째 프레임에서 끝났지만 잠금(교정)이 끝나지 않음
        Assert.Equal(0, run.Count(ActionKind.BeginRecording));

        run.Feed(new(EventKind.LockApplied), rule); // 이벤트 464
        run.Feed(Frame, rule); // 이벤트 465
        Assert.Equal(465, run.FirstAt(ActionKind.BeginRecording));
    }

    // 열화상 준비를 미룬 채 중지하면 준비하지 않고 미리보기로 돌아갑니다.
    [Fact]
    public void StopWhileThermalDeferredNeverPrepares()
    {
        var rule = MakeRule(SettingMode.Manual) with { PrepareThermal = true };
        var run = new EventLog();
        run.Feed(StartWithPpgSettle(360), rule);
        run.Feed(Frame, rule, 10);
        run.Feed(Stop, rule);
        run.Feed(Frame, rule, 400);

        Assert.Equal(0, run.Count(ActionKind.PrepareThermal));
        Assert.False(run.State.ThermalDeferred);
        Assert.Equal(Phase.Preview, run.State.Phase);
    }

    // 열화상 셔터 교정이 없으면 미룰 까닭이 없어 자동 맞춤은 촬영 버튼에서 잠그고, PPG 안정 대기가 끝난 프레임에서 녹화합니다.
    [Fact]
    public void AutoWithoutThermalLocksAtStartDuringPpgSettle()
    {
        var rule = MakeRule(SettingMode.Auto);
        var run = new EventLog();
        run.Feed(StartWithPpgSettle(360), rule); // 이벤트 0. 잠금 요청
        Assert.Equal(0, run.FirstAt(ActionKind.LockSettings));
        Assert.False(run.State.LockDeferred);

        run.Feed(new(EventKind.LockApplied), rule); // 이벤트 1
        run.Feed(Frame, rule, 400);
        Assert.Equal(361, run.FirstAt(ActionKind.BeginRecording));
    }

    // 미룬 동안 앞 촬영의 늦은 열화상 준비 완료 사건이 와도, 이번 촬영의 준비가 끝나기 전에는 녹화하지 않습니다.
    [Fact]
    public void StaleThermalPreparedDuringDeferralKeepsWaiting()
    {
        var rule = MakeRule(SettingMode.Manual) with { PrepareThermal = true };
        var run = new EventLog();
        run.Feed(StartWithPpgSettle(360), rule); // 이벤트 0
        run.Feed(new(EventKind.ThermalPrepared), rule); // 이벤트 1. 앞 촬영의 준비 완료
        run.Feed(Frame, rule, 400); // 이벤트 2~401. 준비 요청은 285번째 프레임(이벤트 286)
        Assert.Equal(286, run.FirstAt(ActionKind.PrepareThermal));
        Assert.Equal(0, run.Count(ActionKind.BeginRecording));

        run.Feed(new(EventKind.ThermalPrepared), rule); // 이벤트 402
        run.Feed(Frame, rule); // 이벤트 403
        Assert.Equal(403, run.FirstAt(ActionKind.BeginRecording));
    }

    // 같은 사건이 자동 맞춤의 잠금 완료로 와도 이번 촬영의 잠금이 끝나기 전에는 녹화하지 않습니다.
    [Fact]
    public void StaleLockAppliedDuringDeferralKeepsWaiting()
    {
        var rule = MakeRule(SettingMode.Auto) with { PrepareThermal = true };
        var run = new EventLog();
        run.Feed(StartWithPpgSettle(360), rule); // 이벤트 0
        run.Feed(new(EventKind.LockApplied), rule); // 이벤트 1. 앞 촬영의 잠금 완료
        run.Feed(Frame, rule, 400);
        Assert.Equal(286, run.FirstAt(ActionKind.LockSettings));
        Assert.Equal(0, run.Count(ActionKind.BeginRecording));
    }

    // PPG 안정 대기가 잠금 뒤 안정화 프레임 수보다 짧으면 열화상을 써도 촬영 버튼에서 바로 잠그고, 안정화가 끝나야 녹화합니다.
    [Fact]
    public void AutoLocksAtStartWhenPpgSettleIsShort()
    {
        var rule = MakeRule(SettingMode.Auto) with { PrepareThermal = true };
        var startShort = StartWithPpgSettle(10);

        var settleLater = new EventLog();
        settleLater.Feed(startShort, rule); // 이벤트 0. 잠금 요청
        Assert.Equal(0, settleLater.FirstAt(ActionKind.LockSettings));
        settleLater.Feed(new(EventKind.LockApplied), rule);
        settleLater.Feed(Frame, rule, 100);
        Assert.Equal(1 + 75, settleLater.FirstAt(ActionKind.BeginRecording));
    }

    // PPG 안정 대기 중 중지하면 남은 대기를 버리고, 다음 촬영은 그 촬영의 대기만 따릅니다.
    [Fact]
    public void StopDuringPpgSettleClearsWait()
    {
        var rule = MakeRule(SettingMode.Manual);
        var run = new EventLog();
        run.Feed(StartWithPpgSettle(360), rule);
        run.Feed(Frame, rule, 100);
        run.Feed(Stop, rule);
        Assert.Equal(Phase.Preview, run.State.Phase);
        Assert.Equal(0, run.State.FramesUntilPpgSettled);

        run.Feed(Start, rule); // 이벤트 102
        run.Feed(Frame, rule); // 이벤트 103

        Assert.Equal(103, run.FirstAt(ActionKind.BeginRecording));
    }

    // 연결이 끊기면 PPG 안정 대기를 버립니다. 다시 연결된 뒤에는 촬영 버튼을 다시 눌러야 녹화합니다.
    [Fact]
    public void DisconnectDuringPpgSettleClearsWait()
    {
        var rule = MakeRule(SettingMode.Manual);
        var run = new EventLog();
        run.Feed(StartWithPpgSettle(360), rule);
        run.Feed(Frame, rule, 10);
        run.Feed(new(EventKind.Disconnected), rule);

        Assert.Equal(Phase.Disconnected, run.State.Phase);
        Assert.Equal(0, run.State.FramesUntilPpgSettled);
        Assert.Equal(0, run.Count(ActionKind.BeginRecording));
    }

    // 열화상 프레임은 PPG 안정 대기를 줄이지 않습니다. 대기는 RGB 프레임으로만 셉니다.
    [Fact]
    public void PpgSettleCountsRgbFramesOnly()
    {
        var rule = MakeRule(SettingMode.Manual);
        var run = new EventLog();
        run.Feed(StartWithPpgSettle(5), rule);
        run.Feed(ThermalFrame, rule, 20);

        Assert.Equal(5, run.State.FramesUntilPpgSettled);
        Assert.Equal(Phase.Settling, run.State.Phase);
    }
}
