using DiabeticFoot.App.Analysis;
using DiabeticFoot.App.Tests.Support;
using DiabeticFoot.Platform;

namespace DiabeticFoot.App.Tests.Analysis;

public sealed class AnalysisViewModelTests
{
    // 촬영이 저장되면 가짜 분석기가 요청을 받아 분석하고, 막대는 대기, 분석 중, 완료를 차례로 보입니다.
    [Fact]
    public void AnalyzesShotAndShowsResult()
    {
        using var rig = new FakeAnalyzerRig();
        var lane = rig.Lane;
        var finished = new List<AnalysisFinishedEventArgs>();
        var figures = new List<string>();
        var collected = new List<AnalysisCollectedEventArgs>();
        lane.Finished += (_, e) => finished.Add(e);
        lane.FiguresReady += (_, folder) => figures.Add(folder);
        lane.Collected += (_, e) => collected.Add(e);
        var folder = rig.Shot("102528");

        lane.Start();
        Assert.True(rig.Timer.IsRunning);
        Assert.Equal(TimeSpan.FromMilliseconds(200), rig.Timer.Interval);
        Assert.Equal(AnalysisState.Starting, lane.State);
        Assert.Equal("분석 준비 중", lane.Title);

        rig.PollUntil(() => lane.State == AnalysisState.Idle, "관류 분석 프로그램이 모델을 올릴 때까지");
        Assert.Equal("분석 대기", lane.Title);
        Assert.Equal("촬영이 끝나면 자동으로 분석합니다", lane.Detail);
        Assert.False(lane.CanRetry);

        lane.OnShotSaved(folder, ready: true);
        rig.PollUntil(() => lane.State == AnalysisState.Done, "분석이 끝날 때까지");

        Assert.Equal("분석 완료", lane.Title);
        Assert.Equal("102528", lane.Detail);
        Assert.StartsWith("걸린 시간 ", lane.ElapsedText);
        Assert.EndsWith("102528_summary.json", lane.SummaryPath.Replace('\\', '/'));
        Assert.True(lane.CanRetry);
        Assert.False(lane.CanCancel);
        Assert.Equal(QualityLevel.Check, lane.QualityLevel);
        Assert.Equal("측정 완료 (확인 필요): 조명을 확인하세요", lane.QualityText);
        Assert.True(lane.IsFinished(folder));
        var done = Assert.Single(finished);
        Assert.Equal(new AnalysisFinishedEventArgs(folder, true, lane.SummaryPath), done);
        Assert.Equal([folder], rig.Files.Requested);

        rig.PollUntil(() => collected.Count > 0, "분석결과 폴더 복사가 끝날 때까지");
        Assert.Equal([folder], figures);
        Assert.Equal([(folder, "fake")], rig.Files.Collected);
        Assert.Equal(CollectState.Done, lane.CollectState);
        Assert.Equal("results 폴더로 3개 복사", lane.CollectText);
        Assert.Equal("/저장/results", lane.CollectFolder);
        Assert.True(lane.OpenCollectFolderCommand.CanExecute(null));

        lane.OpenCollectFolderCommand.Execute(null);
        Assert.Equal(["/저장/results"], rig.Shell.Opened);
    }

    // 분석 결과는 촬영 폴더의 perfusion/<판> 아래에 쓰입니다.
    [Fact]
    public void WritesResultsUnderVariantFolder()
    {
        using var rig = new FakeAnalyzerRig();
        var folder = rig.Shot("102528");
        rig.Lane.Start();
        rig.PollUntil(() => rig.Lane.State == AnalysisState.Idle, "모델을 올릴 때까지");

        rig.Lane.OnShotSaved(folder, ready: true);
        rig.PollUntil(() => rig.Lane.State == AnalysisState.Done, "분석이 끝날 때까지");

        Assert.True(File.Exists(Path.Combine(folder, "perfusion", "fake", "102528_summary.json")));
    }

    // 분석이 실패하면 이유의 앞부분을 보이고 다시 분석할 수 있습니다.
    [Fact]
    public void ShowsShortenedErrorWhenAnalysisFails()
    {
        using var rig = new FakeAnalyzerRig();
        var lane = rig.Lane;
        lane.Start();
        rig.PollUntil(() => lane.State == AnalysisState.Idle, "모델을 올릴 때까지");

        lane.OnShotSaved(rig.Shot("fail"), ready: true);
        rig.PollUntil(() => lane.State == AnalysisState.Failed, "실패가 보일 때까지");

        Assert.Equal("분석 실패", lane.Title);
        Assert.Equal("fail: left.avi 가 없습니다: C:\\촬영\\left.avi\n두 번째 줄", lane.Detail);
        Assert.True(lane.CanRetry);
        Assert.Equal(QualityLevel.None, lane.QualityLevel);
        Assert.Equal("", lane.QualityText);
    }

    // 분석 중에 취소하면 막대가 취소로 바뀌고, 관류 분석 프로그램이 다시 떠서 다음 분석을 받을 수 있습니다.
    [Fact]
    public void CancelStopsRunningAnalysis()
    {
        using var rig = new FakeAnalyzerRig();
        var lane = rig.Lane;
        lane.Start();
        rig.PollUntil(() => lane.State == AnalysisState.Idle, "모델을 올릴 때까지");
        lane.OnShotSaved(rig.Shot("hang"), ready: true);
        rig.PollUntil(() => lane.Step == 1, "첫 진행 줄이 보일 때까지");

        Assert.Equal(AnalysisState.Busy, lane.State);
        Assert.True(lane.CanCancel);
        Assert.Equal("분석 중: 영상 읽기와 발 찾기", lane.Title);
        Assert.Equal("영상 읽기와 발 찾기", lane.StageText);
        Assert.Equal(5, lane.Total);
        Assert.Equal("hang", lane.Detail);
        Assert.StartsWith("경과 ", lane.ElapsedText);
        lane.CancelCommand.Execute(null);
        rig.PollUntil(() => lane.State == AnalysisState.Cancelled, "취소가 보일 때까지");

        Assert.Equal("분석 취소", lane.Title);
        Assert.Equal("hang", lane.Detail);
        Assert.False(lane.CanCancel);

        lane.OnShotSaved(rig.Shot("102528"), ready: true);
        rig.PollUntil(() => lane.State == AnalysisState.Done, "취소 뒤 다음 분석이 끝날 때까지");
    }

    // 보류하는 동안에는 모델도 올리지 않고 이유를 보입니다. 앞 판을 기다리는 중이면 그 판의 이름을 씁니다.
    [Fact]
    public void HoldKeepsAnalyzerWaitingUntilReleased()
    {
        using var rig = new FakeAnalyzerRig();
        var lane = rig.Lane;
        lane.SetHold(true);

        lane.Start();
        Assert.Equal(AnalysisState.Starting, lane.State);
        Assert.Equal("녹화가 끝나면 분석 모델을 올립니다", lane.Detail);

        lane.SetHoldReason("v6");
        Assert.Equal("v6 분석이 끝나면 분석 모델을 올립니다", lane.Detail);

        lane.OnShotSaved(rig.Shot("102528"), ready: true);
        rig.PollUntil(() => lane.Queued == 1, "대기열에 들어갈 때까지");
        Assert.Equal(AnalysisState.Starting, lane.State);

        lane.SetHoldReason("");
        lane.SetHold(false);
        rig.PollUntil(() => lane.State == AnalysisState.Done, "보류를 풀고 분석이 끝날 때까지");
        Assert.Equal(0, lane.Queued);
    }

    // 분석 입력 확인에서 막힌 촬영은 분석하지 않고 이유를 보입니다.
    [Fact]
    public void SkipsShotBlockedByInputCheck()
    {
        using var rig = new FakeAnalyzerRig();
        var folder = rig.Shot("102528");

        rig.Lane.OnShotSaved(folder, ready: false);

        Assert.Equal(AnalysisState.Skipped, rig.Lane.State);
        Assert.Equal("분석하지 않음", rig.Lane.Title);
        Assert.Equal("102528: 분석 입력 확인에서 막혔습니다", rig.Lane.Detail);
        Assert.Empty(rig.Files.Requested);
    }

    // RGB가 저장될 때 넣은 촬영은 같은 촬영의 저장 알림이 다시 넣지 않습니다.
    [Fact]
    public void RgbSavedEnqueuesOnceEvenWhenShotSavedFollows()
    {
        using var rig = new FakeAnalyzerRig();
        var lane = rig.Lane;
        var folder = rig.Shot("102528");
        lane.Start();
        rig.PollUntil(() => lane.State == AnalysisState.Idle, "모델을 올릴 때까지");

        lane.OnRgbSaved(folder, ready: true);
        lane.OnShotSaved(folder, ready: true);
        rig.PollUntil(() => lane.State == AnalysisState.Done, "분석이 끝날 때까지");

        Assert.Equal([folder], rig.Files.Requested);
    }

    // 끈 판은 새 촬영을 넣지 않습니다.
    [Fact]
    public void DisabledLaneIgnoresShots()
    {
        using var rig = new FakeAnalyzerRig();
        rig.Lane.SetEnabled(false);

        rig.Lane.OnRgbSaved(rig.Shot("102528"), ready: true);
        rig.Lane.OnShotSaved(rig.Shot("102528"), ready: true);

        Assert.Empty(rig.Files.Requested);
        Assert.False(rig.Lane.Enabled);
    }

    // 관류 분석 프로그램 파일이 없으면 이유를 보이고, 촬영은 막지 않습니다. 다시 분석을 누르면 파일을 다시 확인합니다.
    [Fact]
    public void ReportsMissingProgramAndRechecksOnRetry()
    {
        var launch = new ProcessLaunch { Program = "/없는/python", Arguments = ["/없는/dfr_worker.py"] };
        using var rig = new FakeAnalyzerRig(launch);

        rig.Lane.Start();

        Assert.Equal(AnalysisState.Missing, rig.Lane.State);
        Assert.Equal("관류 분석 프로그램 없음", rig.Lane.Title);
        Assert.Equal("파이썬 없음: /없는/python\n촬영은 할 수 있고 분석만 하지 않습니다", rig.Lane.Detail);
        Assert.True(rig.Lane.CanRetry);

        rig.Lane.OnShotSaved(rig.Shot("102528"), ready: true);
        Assert.Equal(AnalysisState.Missing, rig.Lane.State);
        Assert.Empty(rig.Files.Requested);

        rig.Lane.RetryCommand.Execute(null);
        Assert.Equal(AnalysisState.Missing, rig.Lane.State);
    }

    // 프로세스를 띄우지 못하면 실패로 보이고, 다시 분석하면 다시 띄웁니다.
    [Fact]
    public void ReportsFailedLaunch()
    {
        var script = Path.Combine(AppContext.BaseDirectory, "DiabeticFoot.FakeAnalyzer.dll");
        var launch = new ProcessLaunch { Program = script, Arguments = [script] };
        using var rig = new FakeAnalyzerRig(launch);

        rig.Lane.Start();

        Assert.Equal(AnalysisState.Failed, rig.Lane.State);
        Assert.Equal("분석 실패", rig.Lane.Title);
        Assert.StartsWith("관류 분석 프로그램을 띄우지 못했습니다", rig.Lane.Detail);
        Assert.True(rig.Lane.CanRetry);
    }

    // 앱을 닫으면 분석을 취소하고 이유를 보입니다. 시계도 멈춥니다.
    [Fact]
    public void ShutdownCancelsRunningAnalysis()
    {
        using var rig = new FakeAnalyzerRig();
        var lane = rig.Lane;
        lane.Start();
        rig.PollUntil(() => lane.State == AnalysisState.Idle, "모델을 올릴 때까지");
        lane.OnShotSaved(rig.Shot("hang"), ready: true);
        rig.PollUntil(() => lane.State == AnalysisState.Busy, "분석 중이 보일 때까지");

        lane.Shutdown();

        Assert.False(rig.Timer.IsRunning);
        Assert.Equal(AnalysisState.Cancelled, lane.State);
        Assert.Equal("분석 종료", lane.Title);
        Assert.Equal("앱을 닫아 진행 중인 분석을 취소했습니다", lane.Detail);

        lane.OnShotSaved(rig.Shot("102528"), ready: true);
        Assert.Empty(rig.Files.Requested.Where(f => f.EndsWith("102528")));
    }

    // 그림 줄이 오지 않으면 30초 뒤에는 기다리지 않고 분석결과 폴더로 복사합니다.
    [Fact]
    public void CollectsWithoutFiguresLineAfterThirtySeconds()
    {
        using var rig = new FakeAnalyzerRig();
        rig.Files.DeferFigures = false;
        var lane = rig.Lane;
        var collected = new List<AnalysisCollectedEventArgs>();
        lane.Collected += (_, e) => collected.Add(e);
        lane.Start();
        rig.PollUntil(() => lane.State == AnalysisState.Idle, "모델을 올릴 때까지");

        lane.OnShotSaved(rig.Shot("102528"), ready: true);
        rig.PollUntil(() => lane.State == AnalysisState.Done, "분석이 끝날 때까지");
        rig.Timer.Tick();
        Assert.Empty(collected);
        Assert.Equal(CollectState.None, lane.CollectState);

        rig.Time.Advance(TimeSpan.FromSeconds(31));
        rig.PollUntil(() => collected.Count > 0, "시간이 지나 복사가 끝날 때까지");

        Assert.Equal(CollectState.Done, lane.CollectState);
    }

    // 복사에 실패하면 이유를 막대에 보이고 폴더는 열 수 없습니다.
    [Fact]
    public void ShowsCollectFailure()
    {
        using var rig = new FakeAnalyzerRig();
        rig.Files.Outcome = new CollectOutcome(false, "results 폴더로 복사하지 못함: 디스크가 가득 참");
        var lane = rig.Lane;
        lane.Start();
        rig.PollUntil(() => lane.State == AnalysisState.Idle, "모델을 올릴 때까지");

        lane.OnShotSaved(rig.Shot("102528"), ready: true);
        rig.PollUntil(() => lane.CollectState == CollectState.Failed, "복사 실패가 보일 때까지");

        Assert.Equal("results 폴더로 복사하지 못함: 디스크가 가득 참", lane.CollectText);
        Assert.False(lane.OpenCollectFolderCommand.CanExecute(null));
    }

    // 상태가 같으면 알림이 나가지 않습니다. 시계가 0.2초마다 울려도 화면이 다시 그려지지 않게 하기 위함입니다.
    [Fact]
    public void NotifiesNothingWhenStateIsUnchanged()
    {
        using var rig = new FakeAnalyzerRig();
        var lane = rig.Lane;
        lane.Start();
        rig.PollUntil(() => lane.State == AnalysisState.Idle, "모델을 올릴 때까지");
        var changed = new List<string?>();
        lane.PropertyChanged += (_, e) => changed.Add(e.PropertyName);

        rig.Timer.Tick();
        rig.Timer.Tick();

        Assert.Empty(changed);
    }

    // 판 정보는 연구 저장소 커밋과 릴리즈 자산(올라온 날과 SHA-256 앞 8자리)을 줄로 나눠 보입니다.
    [Fact]
    public void ShowsVariantSource()
    {
        var variant = new AnalyzerVariant
        {
            Id = "v7",
            Label = "v7",
            Summary = "규칙 판정, 템플릿 v7",
            UpstreamCommit = "abcdef0123456",
            UpstreamDate = "2026-10-28",
            ReleaseTag = "v7-rel",
            Classifier = new AnalyzerVariant.Asset { Name = "c.pkl", Sha256 = "0123456789abcdef", Uploaded = "2026-10-20" },
            PulseTemplate = new AnalyzerVariant.Asset { Name = "t.npy", Sha256 = "fedcba9876543210", Uploaded = "2026-10-21" },
        };
        using var rig = new FakeAnalyzerRig();
        var lane = new AnalysisViewModel(new ProcessLaunch(), variant, rig.Files, rig.Timer, rig.Shell);

        Assert.Equal("v7", lane.VariantId);
        Assert.Equal("규칙 판정, 템플릿 v7", lane.VariantSummary);
        Assert.Equal("연구 abcdef0 2026-10-28\nv7-rel: 판정 2026-10-20 (01234567), 템플릿 2026-10-21 (fedcba98)", lane.VariantSource);
        Assert.Equal("", new AnalysisViewModel(new ProcessLaunch(), new AnalyzerVariant(), rig.Files, rig.Timer, rig.Shell).VariantSource);
    }
}
