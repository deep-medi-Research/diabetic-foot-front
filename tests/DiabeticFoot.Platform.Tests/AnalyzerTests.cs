using System.Diagnostics;
using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

public class AnalyzerTests
{
    private static string TempDir(string name)
    {
        var dir = Path.Combine(Path.GetTempPath(), "diabeticfoot_test_analyzer", name);
        if (Directory.Exists(dir)) Directory.Delete(dir, recursive: true);
        Directory.CreateDirectory(dir);
        return dir;
    }

    private static ProcessLaunch FakeWorkerLaunch() => FakeWorker.Launch("worker");

    private static Analyzer StartAnalyzer(ProcessLaunch launch, AnalyzerTiming? timing = null)
    {
        var analyzer = Analyzer.Start(launch, timing ?? new AnalyzerTiming(), out var error);
        Assert.True(analyzer is not null, error);
        return analyzer!;
    }

    private static PerfusionRequest RequestNamed(string dir, string name) => new()
    {
        RecordingDir = dir,
        OutputDir = Path.Combine(dir, "perfusion"),
        Name = name,
    };

    // 조건이 참이 될 때까지 상태를 읽습니다. 시간 안에 참이 되지 않으면 마지막 상태로 실패를 남깁니다.
    private static AnalyzerStatus WaitFor(Analyzer analyzer, Func<AnalyzerStatus, bool> done, long timeoutMs = 20_000)
    {
        var elapsed = Stopwatch.StartNew();
        var status = analyzer.Status();
        while (!done(status))
        {
            if (elapsed.ElapsedMilliseconds > timeoutMs)
            {
                Assert.Fail($"시간 안에 기다린 상태가 되지 않았습니다. state={status.State} message={status.Message} finished={status.FinishedCount}");
            }
            Thread.Sleep(20);
            status = analyzer.Status();
        }
        return status;
    }

    private static AnalyzerOutcome WaitFinished(Analyzer analyzer, long count, long timeoutMs = 20_000) =>
        WaitFor(analyzer, s => s.FinishedCount >= count, timeoutMs).LastDone;

    // 성공한 요청은 요약 경로와 요약에 남은 관류 분석 프로그램 버전을 결과에 두고, 출력 줄을 analyzer_log.txt에 남깁니다.
    [Fact]
    public void SucceedsAndKeepsSummaryPath()
    {
        var dir = TempDir("ok");
        using var analyzer = StartAnalyzer(FakeWorkerLaunch());
        WaitFor(analyzer, s => s.State == AnalyzerState.Idle);
        Assert.True(analyzer.Submit(RequestNamed(dir, "ok")));
        var done = WaitFinished(analyzer, 1);
        Assert.True(done.Ok, done.Error);
        Assert.False(done.Cancelled);
        Assert.Equal("ok", done.Name);
        Assert.Equal(Path.Combine(dir, "perfusion") + "/ok_summary.json", done.SummaryPath);
        Assert.True(File.Exists(done.SummaryPath));
        Assert.Equal("fake-1", done.WorkerVersion);
        Assert.Equal("5e5178d", done.UpstreamCommit);
        var log = File.ReadAllText(Path.Combine(dir, "perfusion", "analyzer_log.txt"));
        Assert.Contains("[1/5] Reading videos", log);
        Assert.Contains("\"name\":\"ok\"", log);
        Assert.Equal(AnalyzerState.Idle, analyzer.Status().State);
    }

    // 관류 분석 프로그램이 실패를 알리면 그 오류 문장을 그대로 결과에 둡니다.
    [Fact]
    public void ReportsWorkerFailure()
    {
        var dir = TempDir("fail");
        using var analyzer = StartAnalyzer(FakeWorkerLaunch());
        Assert.True(analyzer.Submit(RequestNamed(dir, "fail")));
        var done = WaitFinished(analyzer, 1);
        Assert.False(done.Ok);
        Assert.False(done.Cancelled);
        Assert.Contains("left.avi 가 없습니다", done.Error);
        Assert.Contains("! left.avi", File.ReadAllText(Path.Combine(dir, "perfusion", "analyzer_log.txt")));
    }

    // 성공 줄이 와도 요약 파일이 없으면 실패입니다.
    [Fact]
    public void TreatsMissingSummaryAsFailure()
    {
        var dir = TempDir("nosummary");
        using var analyzer = StartAnalyzer(FakeWorkerLaunch());
        Assert.True(analyzer.Submit(RequestNamed(dir, "nosummary")));
        var done = WaitFinished(analyzer, 1);
        Assert.False(done.Ok);
        Assert.Contains("요약 파일이 없습니다", done.Error);
    }

    // 진행 줄이 제한 시간 동안 없으면 강제 종료해 실패로 두고, 다시 띄운 관류 분석 프로그램이 다음 요청을 받습니다.
    [Fact]
    public void TimesOutWithoutProgressAndRestarts()
    {
        var dir = TempDir("hang");
        using var analyzer = StartAnalyzer(FakeWorkerLaunch(), new AnalyzerTiming { StageTimeoutMs = 1_000 });
        Assert.True(analyzer.Submit(RequestNamed(dir, "hang")));
        var busy = WaitFor(analyzer, s => s.Step == 1);
        Assert.Equal(AnalyzerState.Busy, busy.State);
        Assert.Equal(5, busy.Total);
        Assert.Equal("hang", busy.CurrentName);
        // 들여 쓴 하위 단계 줄은 하위 단계 값만 바꾸고 진행 단계 번호는 그대로 둡니다.
        var sub = WaitFor(analyzer, s => s.SubStep == 1);
        Assert.Equal(1, sub.Step);
        Assert.Equal(7, sub.SubTotal);
        Assert.Equal("thermal sub-step", sub.SubMessage);
        var done = WaitFinished(analyzer, 1);
        Assert.False(done.Ok);
        Assert.False(done.Cancelled);
        Assert.Contains("진행 줄이 1초 동안 없어", done.Error);
        Assert.True(done.ElapsedMs >= 1_000, done.ElapsedMs.ToString());
        Assert.True(analyzer.Submit(RequestNamed(dir, "ok")));
        var next = WaitFinished(analyzer, 2);
        Assert.True(next.Ok, next.Error);
    }

    // 취소하면 분석 중인 요청이 취소로 끝나고, 다시 띄운 관류 분석 프로그램이 모델을 미리 올린 뒤 다음 요청을 받습니다.
    [Fact]
    public void CancelKillsAndRestarts()
    {
        var dir = TempDir("cancel");
        using var analyzer = StartAnalyzer(FakeWorkerLaunch());
        Assert.False(analyzer.Cancel());
        Assert.True(analyzer.Submit(RequestNamed(dir, "hang")));
        WaitFor(analyzer, s => s.Step == 1);
        Assert.True(analyzer.Cancel());
        var done = WaitFinished(analyzer, 1, 5_000);
        Assert.False(done.Ok);
        Assert.True(done.Cancelled);
        Assert.Equal("hang", done.Name);
        WaitFor(analyzer, s => s.State == AnalyzerState.Idle);
        Assert.True(analyzer.Submit(RequestNamed(dir, "ok")));
        Assert.True(WaitFinished(analyzer, 2).Ok);
    }

    // 두 요청은 먼저 넣은 순서로 한 건씩 분석하고, 분석 중에는 다른 요청을 대기열에 둡니다.
    [Fact]
    public void RunsQueuedRequestsInOrder()
    {
        var dir = TempDir("queue");
        using var analyzer = StartAnalyzer(FakeWorkerLaunch());
        Assert.True(analyzer.Submit(RequestNamed(dir, "slow_a")));
        Assert.True(analyzer.Submit(RequestNamed(dir, "slow_b")));
        var first = WaitFor(analyzer, s => s.State == AnalyzerState.Busy);
        Assert.Equal("slow_a", first.CurrentName);
        Assert.Equal(1, first.Queued);
        var a = WaitFinished(analyzer, 1);
        Assert.Equal("slow_a", a.Name);
        Assert.True(a.Ok, a.Error);
        var b = WaitFinished(analyzer, 2);
        Assert.Equal("slow_b", b.Name);
        Assert.True(b.Ok, b.Error);
        Assert.Equal(0, analyzer.Status().Queued);
    }

    // 분석 중 관류 분석 프로그램이 비정상 종료하면 그 요청을 종료 코드와 마지막 출력으로 실패 처리하고 다시 띄웁니다.
    [Fact]
    public void RestartsAfterWorkerCrash()
    {
        var dir = TempDir("crash");
        using var analyzer = StartAnalyzer(FakeWorkerLaunch());
        Assert.True(analyzer.Submit(RequestNamed(dir, "crash")));
        var crashed = WaitFinished(analyzer, 1);
        Assert.False(crashed.Ok);
        Assert.False(crashed.Cancelled);
        Assert.Contains("비정상 종료", crashed.Error);
        Assert.Contains("종료 코드 3", crashed.Error);
        Assert.Contains("MemoryError", crashed.Error);
        Assert.True(analyzer.Submit(RequestNamed(dir, "ok")));
        var next = WaitFinished(analyzer, 2);
        Assert.Equal("ok", next.Name);
        Assert.True(next.Ok, next.Error);
    }

    // 보류 중에는 요청을 보내지 않고, 보류를 풀면 보냅니다.
    [Fact]
    public void HoldDefersDispatch()
    {
        var dir = TempDir("hold");
        using var analyzer = StartAnalyzer(FakeWorkerLaunch());
        WaitFor(analyzer, s => s.State == AnalyzerState.Idle);
        analyzer.SetHold(true);
        Assert.True(analyzer.Submit(RequestNamed(dir, "ok")));
        Thread.Sleep(400);
        var held = analyzer.Status();
        Assert.Equal(AnalyzerState.Idle, held.State);
        Assert.Equal(1, held.Queued);
        Assert.True(held.Held);
        Assert.Equal(0, held.FinishedCount);
        analyzer.SetHold(false);
        Assert.True(WaitFinished(analyzer, 1).Ok);
    }

    // 녹화 중 첫 장 미리 분할은 보류 중에도 바로 보내고, 대기열의 분석 요청은 그대로 기다립니다.
    [Fact]
    public void PresegmentBypassesHold()
    {
        var dir = TempDir("presegment");
        using var analyzer = StartAnalyzer(FakeWorkerLaunch());
        WaitFor(analyzer, s => s.State == AnalyzerState.Idle);
        analyzer.SetHold(true);
        Assert.True(analyzer.Submit(RequestNamed(dir, "ok")));
        Assert.True(analyzer.Presegment("C:/촬영/a_left.mkv", "C:/촬영/a_right.mkv", out var error), error);
        var status = WaitFor(analyzer, s => s.Presegmented >= 2);
        Assert.Equal("{\"video\": \"right\", \"ok\": true}", status.LastPresegment);
        Assert.Equal(1, status.Queued);
        Assert.Equal(0, status.FinishedCount);
        analyzer.SetHold(false);
        Assert.True(WaitFinished(analyzer, 1).Ok);
    }

    // 그림을 뒤로 미룬 요청은 done 뒤의 figures 줄을 따로 셉니다. done은 그대로 한 건의 끝입니다.
    [Fact]
    public void CountsFiguresAfterDone()
    {
        var dir = TempDir("figures");
        using var analyzer = StartAnalyzer(FakeWorkerLaunch());
        Assert.True(analyzer.Submit(RequestNamed(dir, "ok")));
        Assert.True(WaitFinished(analyzer, 1).Ok);
        var status = WaitFor(analyzer, s => s.FiguresCount >= 1);
        Assert.Equal("{\"ok\": true, \"seconds\": 1.5}", status.LastFigures);
        Assert.Equal(AnalyzerState.Idle, status.State);
    }

    // 모델을 올리기 전에는 미리 분할을 보내지 않습니다. 녹화 중에 모델 파일을 읽게 되기 때문입니다.
    [Fact]
    public void PresegmentNeedsLoadedModel()
    {
        using var analyzer = StartAnalyzer(FakeWorker.Launch("hang"), new AnalyzerTiming { StopWaitMs = 300 });
        Assert.False(analyzer.Presegment("a_left.mkv", "a_right.mkv", out var error));
        Assert.NotEmpty(error);
    }

    // 준비 줄 전에 끝나는 관류 분석 프로그램은 다시 띄우지 않고 실패 상태로 이유를 보입니다. 요청을 넣으면 다시 띄웁니다.
    [Fact]
    public void FailsWhenWorkerExitsBeforeReady()
    {
        using var analyzer = StartAnalyzer(FakeWorker.Launch("exit", "1"));
        var failed = WaitFor(analyzer, s => s.State == AnalyzerState.Failed);
        Assert.Contains("준비 전에 끝났습니다", failed.Message);
        Assert.Contains("종료 코드 1", failed.Message);
        Assert.Contains("Traceback", failed.Message);
        var dir = TempDir("before_ready");
        Assert.True(analyzer.Submit(RequestNamed(dir, "ok")));
        // 같은 명령줄이므로 다시 준비 전에 끝나고, 요청은 대기열에 남습니다.
        Thread.Sleep(300);
        var again = WaitFor(analyzer, s => s.State == AnalyzerState.Failed);
        Assert.Equal(1, again.Queued);
    }

    // 실행 파일이 없으면 Start가 이유와 함께 실패합니다.
    [Fact]
    public void StartFailsWithoutExecutable()
    {
        var launch = new ProcessLaunch { Program = Path.Combine(TempDir("missing"), "no_python.exe") };
        Assert.Null(Analyzer.Start(launch, out var error));
        Assert.Contains("관류 분석 프로그램을 띄우지 못했습니다", error);
    }

    // Stop은 분석 중인 요청을 취소로 끝내고 대기열을 비우며, 그 뒤 요청은 받지 않습니다.
    [Fact]
    public void StopCancelsWork()
    {
        var dir = TempDir("stop");
        using var analyzer = StartAnalyzer(FakeWorkerLaunch());
        Assert.True(analyzer.Submit(RequestNamed(dir, "hang")));
        Assert.True(analyzer.Submit(RequestNamed(dir, "ok")));
        WaitFor(analyzer, s => s.Step == 1);
        var begin = Stopwatch.StartNew();
        analyzer.Stop();
        Assert.True(begin.ElapsedMilliseconds < 2_000, begin.ElapsedMilliseconds.ToString());
        var stopped = analyzer.Status();
        Assert.Equal(AnalyzerState.Stopped, stopped.State);
        Assert.Equal(0, stopped.Queued);
        Assert.True(stopped.LastDone.Cancelled);
        Assert.False(analyzer.Submit(RequestNamed(dir, "ok")));
    }

    // AnalyzerSetup.Launch가 만든 명령줄(스크립트 경로, --idle-exit 0)로 띄운 관류 분석 프로그램과 요청을 주고받습니다.
    [Fact]
    public void RunsFakeWorkerThroughLaunch()
    {
        var dir = TempDir("launch");
        File.WriteAllText(Path.Combine(dir, "fake_worker.py"), "# fake");
        var launch = AnalyzerSetup.Launch(dir, FakeWorker.Path, Path.Combine(dir, "fake_worker.py"));
        Assert.Null(AnalyzerSetup.FilesProblem(launch));
        using var analyzer = StartAnalyzer(launch);
        Assert.True(analyzer.Submit(RequestNamed(dir, "ok")));
        Assert.True(WaitFinished(analyzer, 1).Ok);
    }
}
