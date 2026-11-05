using System.Diagnostics;
using DiabeticFoot.App.Analysis;
using DiabeticFoot.App.Preferences;
using DiabeticFoot.App.Tests.Support;
using DiabeticFoot.Platform;

namespace DiabeticFoot.App.Tests.Analysis;

public sealed class AnalysisVariantsViewModelTests
{
    private static readonly RecordingShell Shell = new();

    // 관류 분석 프로그램을 띄우지 않는(Start를 부르지 않는) 판 두 개입니다. 판 순서와 켜고 끄는 규칙만 봅니다.
    private static AnalysisVariantsViewModel TwoLanes(ScriptedAnalysisFiles? files = null)
    {
        var lanes = new[] { "v6", "v7" }.Select(id =>
            new AnalysisViewModel(new ProcessLaunch(), new AnalyzerVariant { Id = id, Label = id }, files ?? new ScriptedAnalysisFiles(), new ManualPollTimer(), Shell));
        return new AnalysisVariantsViewModel(lanes, prestreamAvailable: true);
    }

    // 녹화 중이면 모두 보류하고, 아니면 앞쪽에 켜 둔 판이 분석 중일 때 뒤쪽 판만 보류합니다.
    [Fact]
    public void HoldsLaterVariantsWhileEarlierRuns()
    {
        static IReadOnlyList<bool> Holds(bool recording, bool[] enabled, bool[] active) =>
            AnalysisVariantsViewModel.VariantHolds(recording, enabled, active);

        Assert.Equal([true, true], Holds(true, [true, true], [false, false]));
        Assert.Equal([false, false], Holds(false, [true, true], [false, false]));
        Assert.Equal([false, true], Holds(false, [true, true], [true, false]));
        Assert.Equal([false, false], Holds(false, [true, true], [false, true]));
        Assert.Equal([false, true, true], Holds(false, [true, true, true], [true, true, false]));
        // 꺼 둔 판은 분석 중이어도(끄기 전에 넣은 촬영) 뒤 판을 막지 않습니다.
        Assert.Equal([false, false], Holds(false, [false, true], [true, false]));
    }

    // 판을 끄고 켤 수 있고, 마지막으로 켜 둔 판은 끄지 않습니다.
    [Fact]
    public void KeepsAtLeastOneVariant()
    {
        using var variants = TwoLanes();
        var changed = new List<string?>();
        variants.PropertyChanged += (_, e) => changed.Add(e.PropertyName);

        Assert.Equal(["v6", "v7"], variants.Ids);
        Assert.Equal(2, variants.EnabledCount);
        Assert.True(variants.SetLaneEnabled("v6", false));
        Assert.Equal(1, variants.EnabledCount);
        Assert.False(variants.SetLaneEnabled("v7", false));
        Assert.Equal(1, variants.EnabledCount);
        Assert.False(variants.SetLaneEnabled("v9", true));
        Assert.True(variants.SetLaneEnabled("v6", true));
        Assert.Equal(2, variants.EnabledCount);
        Assert.True(changed.Count(name => name == nameof(AnalysisVariantsViewModel.EnabledCount)) >= 2);
        Assert.NotNull(variants.Lane("v7"));
        Assert.Null(variants.Lane("v9"));
        Assert.Equal("v9", variants.LabelOf("v9"));
    }

    // 기억한 값이 판을 끄고 있으면 그대로 따르고, 모든 판을 끄고 있으면 첫 판을 켭니다.
    [Fact]
    public void ReadsRememberedChoice()
    {
        var remembered = RememberedValues.ThisRunOnly();
        remembered.SetBool("analysis/variant/v6/enabled", false);
        remembered.SetBool("analysis/prestream", false);
        using var variants = TwoLanes();
        variants.UseRemembered(remembered);

        Assert.False(variants.Lane("v6")!.Enabled);
        Assert.True(variants.Lane("v7")!.Enabled);
        Assert.False(variants.Prestream);
        variants.Prestream = true;
        Assert.Equal(true, remembered.GetBool("analysis/prestream"));

        remembered.SetBool("analysis/variant/v7/enabled", false);
        using var allOff = TwoLanes();
        allOff.UseRemembered(remembered);
        Assert.Equal(1, allOff.EnabledCount);
        Assert.True(allOff.Lane("v6")!.Enabled);
    }

    // 고른 판은 기억한 값에 남기고, 기억한 값이 없으면 모든 판과 스위치가 켜져 있습니다.
    [Fact]
    public void RemembersLaneAndPrestreamChoices()
    {
        var remembered = RememberedValues.ThisRunOnly();
        using var variants = TwoLanes();
        variants.UseRemembered(remembered);

        Assert.Equal(2, variants.EnabledCount);
        Assert.True(variants.Prestream);

        variants.SetLaneEnabled("v7", false);
        variants.Prestream = false;

        Assert.Equal(false, remembered.GetBool("analysis/variant/v7/enabled"));
        Assert.Equal(false, remembered.GetBool("analysis/prestream"));
    }

    // 녹화 중 미리 분석을 쓸 수 없게 띄우면(--no-prestream) 스위치를 바꿀 수 없습니다.
    [Fact]
    public void PrestreamSwitchIsInertWhenUnavailable()
    {
        var lane = new AnalysisViewModel(new ProcessLaunch(), new AnalyzerVariant { Id = "v7" }, new ScriptedAnalysisFiles(), new ManualPollTimer(), Shell);
        using var variants = new AnalysisVariantsViewModel([lane], prestreamAvailable: false);

        variants.Prestream = false;

        Assert.False(variants.PrestreamAvailable);
        Assert.True(variants.Prestream);
    }

    // 판 둘로 촬영 하나를 분석하면 앞 판이 끝난 뒤에 뒤 판이 시작하고, 뒤 판은 기다리는 이유를 보입니다.
    [Fact]
    public void RunsVariantsOneAfterAnother()
    {
        using var rig = new VariantsRig();
        var order = new List<VariantFinishedEventArgs>();
        var collected = new List<VariantCollectedEventArgs>();
        rig.Variants.Finished += (_, e) => order.Add(e);
        rig.Variants.Collected += (_, e) => collected.Add(e);
        var folder = rig.Shot("102528");

        rig.Variants.Start();
        rig.PollUntil(() => rig.Variants.Lanes.All(l => l.State == AnalysisState.Idle), "두 판이 모델을 올릴 때까지");
        Assert.False(rig.Variants.BlocksShot);

        rig.Variants.OnShotSaved(folder, ready: true);
        Assert.True(rig.Variants.BlocksShot);
        rig.PollUntil(() => rig.Variants.Lanes[1].State == AnalysisState.Waiting, "뒤 판이 기다릴 때까지");
        Assert.Equal("A 판 분석이 끝나면 시작합니다", rig.Variants.Lanes[1].Detail);

        rig.PollUntil(() => order.Count == 2, "두 판의 분석이 끝날 때까지");
        Assert.Equal(["fake", "fake2"], order.Select(e => e.Variant));
        Assert.All(order, e => Assert.True(e.Ok));
        Assert.True(rig.Variants.IsFinished(folder));
        rig.PollUntil(() => collected.Count == 2, "두 판의 복사가 끝날 때까지");
        Assert.Equal("A 판: results 폴더로 3개 복사", collected[0].Text);
        Assert.Equal("A 판: results 폴더로 3개 복사\nB 판: results 폴더로 3개 복사", collected[1].Text);
        rig.PollUntil(() => !rig.Variants.BlocksShot, "촬영을 막지 않을 때까지");
        Assert.Equal("", rig.Variants.RemainingText);
    }

    // 분석 중인 판의 이름을 남은 시간 문장에 붙이고, 분석 중에는 촬영을 막습니다.
    [Fact]
    public void BlocksShotAndNamesRunningVariant()
    {
        using var rig = new VariantsRig();
        rig.Variants.Start();
        rig.PollUntil(() => rig.Variants.Lanes.All(l => l.State == AnalysisState.Idle), "두 판이 모델을 올릴 때까지");

        rig.Variants.OnShotSaved(rig.Shot("hang"), ready: true);
        rig.PollUntil(() => rig.Variants.Lanes[0].Step == 1, "앞 판이 분석을 시작할 때까지");

        Assert.True(rig.Variants.BlocksShot);
        Assert.Equal("A 판 분석 중", rig.Variants.RemainingText);

        rig.Variants.Lanes[0].CancelCommand.Execute(null);
        rig.PollUntil(() => rig.Variants.Lanes[0].State == AnalysisState.Cancelled, "취소가 보일 때까지");
    }

    // 끈 판은 촬영을 막지 않고, 켜 둔 판 모두가 분석했을 때만 끝난 촬영으로 칩니다.
    [Fact]
    public void FinishedMeansEveryEnabledLaneFinished()
    {
        using var rig = new VariantsRig();
        rig.Variants.SetLaneEnabled("fake2", false);
        rig.Variants.Start();
        rig.PollUntil(() => rig.Variants.Lanes[0].State == AnalysisState.Idle, "켜 둔 판이 모델을 올릴 때까지");
        var folder = rig.Shot("102528");

        Assert.False(rig.Variants.IsFinished(folder));
        rig.Variants.OnShotSaved(folder, ready: true);
        rig.PollUntil(() => rig.Variants.Lanes[0].State == AnalysisState.Done, "분석이 끝날 때까지");

        Assert.True(rig.Variants.IsFinished(folder));
        Assert.Equal([folder], rig.Files.Requested);
        Assert.False(rig.Variants.Lanes[1].Enabled);
    }

    // 안정화나 녹화 중에는 판 모두가 새 요청을 보내지 않고 이유를 보입니다.
    [Fact]
    public void HoldsEveryLaneWhileRecording()
    {
        using var rig = new VariantsRig();
        rig.Variants.SetHold(true);
        rig.Variants.Start();

        Assert.All(rig.Variants.Lanes, l => Assert.Equal("녹화가 끝나면 분석 모델을 올립니다", l.Detail));

        rig.Variants.SetHold(false);
        rig.PollUntil(() => rig.Variants.Lanes.All(l => l.State == AnalysisState.Idle), "보류를 풀고 모델을 올릴 때까지");
    }

    /// <summary>같은 가짜 분석기를 판 이름만 달리해 둘 띄웁니다(앱이 연구 코드 판마다 띄우는 것과 같은 모양).</summary>
    private sealed class VariantsRig : IDisposable
    {
        private readonly TempFolder _root = new();
        private readonly PumpingContext _context = new();
        private readonly ManualPollTimer[] _timers = [new(), new()];

        public VariantsRig()
        {
            var launch = FakeAnalyzerRig.FakeLaunch(out _);
            var lanes = new[] { ("fake", "A 판"), ("fake2", "B 판") }.Select((v, i) =>
                new AnalysisViewModel(launch, new AnalyzerVariant { Id = v.Item1, Label = v.Item2 }, Files, _timers[i], new RecordingShell()));
            Variants = new AnalysisVariantsViewModel(lanes, prestreamAvailable: true);
        }

        public ScriptedAnalysisFiles Files { get; } = new();

        public AnalysisVariantsViewModel Variants { get; }

        public string Shot(string name) => Path.Combine(_root.Path, "홍길동_19551103", name);

        public void PollUntil(Func<bool> condition, string what, int timeoutMs = 60_000)
        {
            var clock = Stopwatch.StartNew();
            while (!condition())
            {
                if (clock.ElapsedMilliseconds > timeoutMs) throw new TimeoutException(what);
                foreach (var timer in _timers) timer.Tick();
                _context.Pump(TimeSpan.FromMilliseconds(10));
            }
        }

        public void Dispose()
        {
            Variants.Shutdown();
            Variants.Dispose();
            _context.Dispose();
            _root.Dispose();
        }
    }
}
