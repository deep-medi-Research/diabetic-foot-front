using System.Diagnostics;
using System.Text;

namespace DiabeticFoot.Platform;

/// <summary>관류 분석 프로그램 프로세스의 상태입니다.</summary>
public enum AnalyzerState
{
    /// <summary>프로세스를 띄웠고 `[dfr] ready`와 모델 미리 올리기(preload)를 기다림.</summary>
    Starting,
    /// <summary>요청을 받을 수 있음. 대기열이 있으면 보류(SetHold) 중.</summary>
    Idle,
    /// <summary>요청 한 건을 분석 중.</summary>
    Busy,
    /// <summary>띄우지 못했거나 준비 전에 끝남. Submit이나 Restart로 다시 띄움.</summary>
    Failed,
    /// <summary>Stop 뒤.</summary>
    Stopped,
}

/// <summary>마지막으로 끝난 요청 한 건입니다.</summary>
public sealed record AnalyzerOutcome
{
    /// <summary>요청의 결과 이름(촬영 폴더 이름). 끝난 요청이 없으면 빈 문자열입니다.</summary>
    public string Name { get; init; } = "";

    /// <summary>요청의 촬영 폴더입니다.</summary>
    public string RecordingDir { get; init; } = "";

    /// <summary>관류 분석 프로그램이 성공을 알렸고 요약 JSON이 있으면 true입니다.</summary>
    public bool Ok { get; init; }

    /// <summary>사용자가 취소해 끝났으면 true입니다. 이때 Ok는 false입니다.</summary>
    public bool Cancelled { get; init; }

    /// <summary>실패 이유입니다. 성공이면 빈 문자열입니다.</summary>
    public string Error { get; init; } = "";

    /// <summary>요약 JSON 경로(`&lt;output_dir&gt;/&lt;name&gt;_summary.json`)입니다. 실패해도 요청에서 정한 경로를 둡니다.</summary>
    public string SummaryPath { get; init; } = "";

    /// <summary>요청을 보낸 때부터 끝날 때까지의 시간입니다.</summary>
    public long ElapsedMs { get; init; }

    /// <summary>요약 JSON의 dfr.worker_version. 없으면 준비 줄에서 받은 값입니다.</summary>
    public string WorkerVersion { get; init; } = "";

    /// <summary>요약 JSON의 dfr.upstream_commit. 없으면 준비 줄에서 받은 값입니다.</summary>
    public string UpstreamCommit { get; init; } = "";
}

/// <summary>Analyzer.Status의 결과입니다.</summary>
public sealed record AnalyzerStatus
{
    public AnalyzerState State { get; init; } = AnalyzerState.Starting;

    /// <summary>Busy일 때 마지막 진행 줄의 단계 번호입니다. 진행 줄이 아직 없으면 0입니다.</summary>
    public int Step { get; init; }

    /// <summary>Busy일 때 마지막 진행 줄의 전체 단계 수입니다.</summary>
    public int Total { get; init; }

    /// <summary>Busy일 때 진행 줄의 문구, Failed일 때 실패 이유, Starting일 때 지금 기다리는 것입니다.</summary>
    public string Message { get; init; } = "";

    /// <summary>Busy일 때 현재 단계 안의 하위 단계 번호(1부터)입니다. 하위 단계 줄이 없거나 단계가 바뀌면 0입니다.</summary>
    public int SubStep { get; init; }

    public int SubTotal { get; init; }

    /// <summary>마지막 하위 단계 줄의 문구입니다.</summary>
    public string SubMessage { get; init; } = "";

    /// <summary>`[dfr] ready` 뒤 JSON의 값입니다. 버전을 찍지 않는 관류 분석 프로그램이면 빈 문자열입니다.</summary>
    public string WorkerVersion { get; init; } = "";

    public string UpstreamCommit { get; init; } = "";

    /// <summary>Busy일 때 분석 중인 요청의 이름입니다.</summary>
    public string CurrentName { get; init; } = "";

    /// <summary>Busy일 때 요청을 보낸 뒤 지난 시간입니다.</summary>
    public long ElapsedMs { get; init; }

    /// <summary>보내지 않고 기다리는 요청 수입니다. 분석 중인 요청은 세지 않습니다.</summary>
    public int Queued { get; init; }

    /// <summary>SetHold(true) 상태인지입니다.</summary>
    public bool Held { get; init; }

    /// <summary>끝난 요청 수. 값이 바뀌면 LastDone이 새 결과입니다.</summary>
    public long FinishedCount { get; init; }

    /// <summary>마지막으로 끝난 요청입니다. FinishedCount가 0이면 빈 값입니다.</summary>
    public AnalyzerOutcome LastDone { get; init; } = new();

    /// <summary>받은 `[dfr] presegmented` 줄 수(발 하나에 한 줄)입니다. 다시 띄워도 이어서 셉니다.</summary>
    public long Presegmented { get; init; }

    /// <summary>마지막 `[dfr] presegmented` 줄의 JSON입니다.</summary>
    public string LastPresegment { get; init; } = "";

    /// <summary>받은 `[dfr] figures` 줄 수입니다. 값이 바뀌면 마지막으로 끝난 요청(LastDone)의 그림을 다 그렸습니다.</summary>
    public long FiguresCount { get; init; }

    /// <summary>마지막 `[dfr] figures` 줄의 JSON입니다.</summary>
    public string LastFigures { get; init; } = "";
}

/// <summary>관류 분석 프로그램을 다루는 시간 값입니다. 시험에서 짧게 줍니다.</summary>
public sealed record AnalyzerTiming
{
    /// <summary>
    /// 진행 줄이 이 시간 동안 없으면 관류 분석 프로그램을 강제 종료하고 다시 띄웁니다. 준비 줄과 미리 올리기 응답에도 씁니다.
    /// 실험 노트북에서 한 건 전체가 약 45초이고 단계 사이 간격은 그보다 짧습니다(2026-10-14 현장 PC 실측).
    /// </summary>
    public long StageTimeoutMs { get; init; } = 180_000;

    /// <summary>Stop에서 입력을 닫은 뒤 스스로 끝나기를 기다리는 시간입니다. 넘으면 강제 종료합니다.</summary>
    public long StopWaitMs { get; init; } = 3_000;
}

/// <summary>
/// 관류 분석 프로그램(dfr_worker.py) 프로세스 하나와 요청 대기열입니다.
/// 프로세스를 띄워 둔 채 요청을 먼저 온 순서대로 한 건씩 보냅니다. 내부 스레드 하나가 출력 줄을 읽어 상태를 바꾸고,
/// 호출자는 Status를 주기적으로 읽습니다. 요청 한 건이 끝나는 동안 나온 출력은 모두 그 요청의
/// `&lt;output_dir&gt;/analyzer_log.txt`에 덧붙입니다(연구 코드의 예외 기록을 남기기 위함).
/// <list type="bullet">
/// <item>준비 줄(`[dfr] ready`)을 받으면, 보류 중이 아닐 때 모델 미리 올리기 요청을 보내고 응답을 받은 뒤 요청을 보냅니다.</item>
/// <item>분석 중 진행 줄이 AnalyzerTiming.StageTimeoutMs 동안 없으면 강제 종료하고 그 요청을 실패로 두고 다시 띄웁니다.</item>
/// <item>분석 중이나 대기 중에 프로세스가 끝나면 그 요청을 실패로 두고 다시 띄웁니다. 준비 전에 끝나면 Failed가 됩니다.</item>
/// <item>성공은 완료 줄의 ok와 요약 JSON 파일이 함께 있을 때입니다.</item>
/// </list>
/// 모든 멤버는 여러 스레드에서 불러도 됩니다.
/// </summary>
public sealed class Analyzer : IDisposable
{
    private const int ReadPollMs = 50;
    private const int IdleWaitMs = 100;
    private const int RecentLines = 6;

    // 프로세스 쪽 단계입니다. 공개 상태(AnalyzerState)보다 잘게 나눕니다.
    private enum Stage
    {
        AwaitReady,
        PreloadDue,
        AwaitPreload,
        Ready,
        Busy,
        Failed,
        Stopped,
    }

    private sealed record Pending(PerfusionRequest Request, string Line);

    private readonly ProcessLaunch _launch;
    private readonly AnalyzerTiming _timing;
    private readonly object _gate = new();
    private readonly Queue<Pending> _queue = new();
    private readonly Queue<string> _recent = new();
    private readonly Thread _thread;

    // 내부 스레드만 바꿉니다. Cancel은 잠금 안에서 Kill만 부릅니다.
    private ChildProcess? _child;
    private Stage _stage = Stage.AwaitReady;
    private Pending? _current;
    private long _sentAt;
    private long _lastActivity;
    private bool _hold;
    private bool _cancelRequested;
    private bool _timedOut;
    private bool _restartRequested;
    private bool _stopRequested;
    private int _step;
    private int _total;
    private string _message = "";
    private int _subStep;
    private int _subTotal;
    private string _subMessage = "";
    private string _workerVersion = "";
    private string _upstreamCommit = "";
    private long _finishedCount;
    private AnalyzerOutcome _lastDone = new();
    private long _presegmented;
    private string _lastPresegment = "";
    private long _figuresCount;
    private string _lastFigures = "";
    private StreamWriter? _log;

    private Analyzer(ProcessLaunch launch, AnalyzerTiming timing)
    {
        _launch = launch;
        _timing = timing;
        _thread = new Thread(Run) { IsBackground = true, Name = "Analyzer" };
    }

    /// <summary>관류 분석 프로그램을 띄웁니다. 프로세스를 띄우지 못하면 null이고 error에 이유를 씁니다.</summary>
    public static Analyzer? Start(ProcessLaunch launch, AnalyzerTiming timing, out string error)
    {
        var analyzer = new Analyzer(launch, timing);
        analyzer.Launch();
        if (analyzer._stage == Stage.Failed)
        {
            error = analyzer._message;
            return null;
        }
        error = "";
        analyzer._thread.Start();
        return analyzer;
    }

    public static Analyzer? Start(ProcessLaunch launch, out string error) => Start(launch, new AnalyzerTiming(), out error);

    /// <summary>
    /// 요청을 대기열 끝에 넣습니다. 요청 줄을 만들 수 없거나 Stop 뒤이면 false입니다. Failed 상태이면 관류 분석 프로그램을 다시 띄웁니다.
    /// 요청에는 결과 폴더와 이름이 있어야 합니다.
    /// </summary>
    public bool Submit(PerfusionRequest request, out string error)
    {
        var line = PerfusionAnalysis.BuildPerfusionRequestLine(request, out error);
        if (line is not null && (request.OutputDir.Length == 0 || request.Name.Length == 0))
        {
            line = null;
            error = "요청에 결과 폴더와 이름이 있어야 합니다";
        }
        if (line is null) return false;
        lock (_gate)
        {
            if (_stopRequested)
            {
                error = "관류 분석 프로그램을 멈췄습니다";
                return false;
            }
            _queue.Enqueue(new Pending(request, line));
            if (_stage == Stage.Failed) _restartRequested = true;
            Monitor.PulseAll(_gate);
            return true;
        }
    }

    public bool Submit(PerfusionRequest request) => Submit(request, out _);

    /// <summary>
    /// 분석 중인 요청을 취소합니다. 관류 분석 프로그램을 강제 종료하고 다시 띄운 뒤 모델을 미리 올립니다.
    /// 대기열의 다른 요청은 그대로 두고, 다시 띄운 관류 분석 프로그램이 이어서 받습니다. 분석 중인 요청이 있었으면 true입니다.
    /// </summary>
    public bool Cancel()
    {
        lock (_gate)
        {
            if (_stage != Stage.Busy || _child is null) return false;
            _cancelRequested = true;
            _child.Kill();
            return true;
        }
    }

    /// <summary>
    /// 새 요청과 모델 미리 올리기를 보내지 않게 하거나 다시 보내게 합니다. 이미 보낸 요청은 계속 분석합니다.
    /// 안정화와 녹화 중에 켭니다. 관류 분석 프로그램 시작(모델 약 400MB 읽기)이 영상 쓰기와 디스크를 나눠 쓰지 않게 하기 위함입니다.
    /// </summary>
    public void SetHold(bool hold)
    {
        lock (_gate)
        {
            _hold = hold;
            Monitor.PulseAll(_gate);
        }
    }

    /// <summary>
    /// 녹화 중인 발 영상의 첫 장을 미리 분할하게 합니다. 보류(SetHold)와 대기열을 거치지 않고 바로 보내는 보류의 유일한 예외입니다.
    /// 녹화 중에 보내므로, 모델 파일을 읽지 않도록 미리 올리기가 끝난 뒤(준비 전이거나 실패, 멈춤 상태가 아닐 때)에만 보냅니다.
    /// 분석 중이면 관류 분석 프로그램이 그 분석을 마친 뒤 읽습니다. 분할 스레드 수는 `--presegment-threads`로 정합니다.
    /// </summary>
    public bool Presegment(string leftVideo, string rightVideo, out string error)
    {
        lock (_gate)
        {
            if (_stopRequested || _child is null || (_stage != Stage.Ready && _stage != Stage.Busy))
            {
                error = "관류 분석 프로그램이 모델을 올린 뒤가 아닙니다";
                return false;
            }
            // 쓰지 못하면 프로세스가 끝난 것이므로 출력이 닫혀 내부 스레드가 처리합니다.
            _child.WriteLine(PerfusionAnalysis.BuildPresegmentLine(leftVideo, rightVideo));
            error = "";
            return true;
        }
    }

    public bool Presegment(string leftVideo, string rightVideo) => Presegment(leftVideo, rightVideo, out _);

    /// <summary>Failed 상태의 관류 분석 프로그램을 다시 띄웁니다. 다른 상태이면 아무것도 하지 않습니다.</summary>
    public void Restart()
    {
        lock (_gate)
        {
            if (_stage != Stage.Failed) return;
            _restartRequested = true;
            Monitor.PulseAll(_gate);
        }
    }

    /// <summary>입력을 닫아 관류 분석 프로그램을 끝냅니다. 분석 중인 요청은 취소로 끝나고 대기열은 비웁니다.</summary>
    public void Stop()
    {
        lock (_gate)
        {
            if (!_stopRequested)
            {
                _stopRequested = true;
                Finish(false, "관류 분석 프로그램을 멈춰 취소했습니다", true);
                _queue.Clear();
                Monitor.PulseAll(_gate);
            }
        }
        if (_thread.IsAlive) _thread.Join();
    }

    /// <summary>Stop을 부르고 내부 스레드를 거둡니다.</summary>
    public void Dispose() => Stop();

    /// <summary>지금 상태입니다.</summary>
    public AnalyzerStatus Status()
    {
        lock (_gate)
        {
            var state = _stage switch
            {
                Stage.AwaitReady or Stage.PreloadDue or Stage.AwaitPreload => AnalyzerState.Starting,
                Stage.Ready => AnalyzerState.Idle,
                Stage.Busy => AnalyzerState.Busy,
                Stage.Failed => AnalyzerState.Failed,
                _ => AnalyzerState.Stopped,
            };
            return new AnalyzerStatus
            {
                State = _stopRequested ? AnalyzerState.Stopped : state,
                Step = _step,
                Total = _total,
                Message = _message,
                SubStep = _subStep,
                SubTotal = _subTotal,
                SubMessage = _subMessage,
                WorkerVersion = _workerVersion,
                UpstreamCommit = _upstreamCommit,
                CurrentName = _current?.Request.Name ?? "",
                ElapsedMs = _current is null ? 0 : MsSince(_sentAt),
                Queued = _queue.Count,
                Held = _hold,
                FinishedCount = _finishedCount,
                LastDone = _lastDone,
                Presegmented = _presegmented,
                LastPresegment = _lastPresegment,
                FiguresCount = _figuresCount,
                LastFigures = _lastFigures,
            };
        }
    }

    private static long MsSince(long since) => (long)Stopwatch.GetElapsedTime(since).TotalMilliseconds;

    private static string ExitText(int? code) => code switch
    {
        null => "종료 코드 모름",
        ChildProcess.KilledExitCode => "강제 종료",
        _ => "종료 코드 " + code,
    };

    private void ClearSubStage()
    {
        _subStep = 0;
        _subTotal = 0;
        _subMessage = "";
    }

    private void Launch()
    {
        _child = ChildProcess.Start(_launch, out var error);
        _recent.Clear();
        _workerVersion = "";
        _upstreamCommit = "";
        _step = 0;
        _total = 0;
        ClearSubStage();
        _lastActivity = Stopwatch.GetTimestamp();
        if (_child is null)
        {
            _stage = Stage.Failed;
            _message = "관류 분석 프로그램을 띄우지 못했습니다: " + error;
            return;
        }
        _stage = Stage.AwaitReady;
        _message = "분석 시작 중";
    }

    private void OpenLog(Pending pending)
    {
        try
        {
            Directory.CreateDirectory(pending.Request.OutputDir);
            var stream = new FileStream(Path.Combine(pending.Request.OutputDir, "analyzer_log.txt"), FileMode.Append, FileAccess.Write, FileShare.Read);
            _log = new StreamWriter(stream, new UTF8Encoding(false)) { AutoFlush = true, NewLine = "\n" };
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            _log = null;
        }
        WriteLog("> " + pending.Line);
    }

    private void WriteLog(string line)
    {
        try
        {
            _log?.WriteLine(line);
        }
        catch (IOException)
        {
        }
    }

    private void CloseLog()
    {
        _log?.Dispose();
        _log = null;
    }

    private string RecentText() => string.Join("\n", _recent);

    // 분석 중인 요청을 끝내고 결과를 남깁니다.
    private void Finish(bool ok, string error, bool cancelled)
    {
        if (_current is null) return;
        var summaryPath = PerfusionAnalysis.PerfusionSummaryPath(_current.Request.OutputDir, _current.Request.Name);
        var workerVersion = _workerVersion;
        var upstreamCommit = _upstreamCommit;
        var elapsed = MsSince(_sentAt);
        if (ok && !File.Exists(summaryPath))
        {
            ok = false;
            error = "관류 분석 프로그램이 성공을 알렸지만 요약 파일이 없습니다: " + summaryPath;
        }
        if (ok)
        {
            // 요약에 남은 버전이 준비 줄보다 정확합니다(관류 분석 프로그램이 결과를 쓰며 남긴 값).
            var stamp = ReadStamp(summaryPath);
            if (stamp.WorkerVersion.Length > 0) workerVersion = stamp.WorkerVersion;
            if (stamp.UpstreamCommit.Length > 0) upstreamCommit = stamp.UpstreamCommit;
        }
        var outcome = new AnalyzerOutcome
        {
            Name = _current.Request.Name,
            RecordingDir = _current.Request.RecordingDir,
            SummaryPath = summaryPath,
            ElapsedMs = elapsed,
            Cancelled = cancelled,
            WorkerVersion = workerVersion,
            UpstreamCommit = upstreamCommit,
            Ok = ok,
            Error = ok ? "" : error,
        };
        if (!ok) WriteLog("! " + outcome.Error);
        CloseLog();
        _lastDone = outcome;
        _finishedCount++;
        _current = null;
        _step = 0;
        _total = 0;
        ClearSubStage();
    }

    private static WorkerStamp ReadStamp(string summaryPath)
    {
        try
        {
            return PerfusionAnalysis.ReadWorkerStamp(File.ReadAllText(summaryPath));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return new WorkerStamp("", "");
        }
    }

    // 잠금 안에서 부릅니다. 보낼 것이 있으면 보냅니다.
    private void Dispatch()
    {
        if (_child is null || _hold) return;
        if (_stage == Stage.PreloadDue)
        {
            _child.WriteLine(PerfusionAnalysis.BuildPreloadLine());
            _stage = Stage.AwaitPreload;
            _message = "분석 모델 올리는 중";
            _lastActivity = Stopwatch.GetTimestamp();
            return;
        }
        if (_stage == Stage.Ready && _queue.Count > 0)
        {
            _current = _queue.Dequeue();
            OpenLog(_current);
            _sentAt = Stopwatch.GetTimestamp();
            _lastActivity = _sentAt;
            _step = 0;
            _total = 0;
            ClearSubStage();
            _message = "";
            _stage = Stage.Busy;
            // 쓰지 못하면 프로세스가 끝난 것이므로 출력이 닫혀 HandleExit가 실패로 처리합니다.
            _child.WriteLine(_current.Line);
        }
    }

    private void HandleLine(string line)
    {
        _recent.Enqueue(line);
        if (_recent.Count > RecentLines) _recent.Dequeue();
        if (_stage == Stage.Busy) WriteLog(line);
        var parsed = PerfusionAnalysis.ParseAnalyzerLine(line);
        switch (parsed.Kind)
        {
            case AnalyzerLineKind.Ready:
                if (_stage != Stage.AwaitReady) break;
                _workerVersion = parsed.WorkerVersion;
                _upstreamCommit = parsed.UpstreamCommit;
                _stage = Stage.PreloadDue;
                _message = "분석 모델 올리기 대기";
                _lastActivity = Stopwatch.GetTimestamp();
                break;
            case AnalyzerLineKind.Preloaded:
            case AnalyzerLineKind.PreloadFailed:
                // 미리 올리기 실패는 분석할 때 다시 올리므로 요청을 막지 않습니다.
                if (_stage != Stage.AwaitPreload) break;
                _stage = Stage.Ready;
                _message = "";
                break;
            case AnalyzerLineKind.Stage:
                if (_stage != Stage.Busy) break;
                _step = parsed.Step;
                _total = parsed.Total;
                _message = parsed.Message;
                ClearSubStage();
                _lastActivity = Stopwatch.GetTimestamp();
                break;
            case AnalyzerLineKind.SubStage:
                // 하위 단계도 진행의 증거이므로 시간 제한의 마지막 활동으로 셉니다.
                if (_stage != Stage.Busy) break;
                _subStep = parsed.Step;
                _subTotal = parsed.Total;
                _subMessage = parsed.Message;
                _lastActivity = Stopwatch.GetTimestamp();
                break;
            case AnalyzerLineKind.Done:
                if (_stage != Stage.Busy) break;
                Finish(parsed.Ok, parsed.Error.Length == 0 ? "관류 분석 프로그램이 이유 없이 실패를 알렸습니다" : parsed.Error, false);
                _stage = Stage.Ready;
                _message = "";
                break;
            case AnalyzerLineKind.Presegmented:
                _presegmented++;
                _lastPresegment = parsed.Message;
                break;
            case AnalyzerLineKind.Figures:
                _figuresCount++;
                _lastFigures = parsed.Message;
                break;
        }
    }

    private void HandleExit(int? code)
    {
        _child?.Dispose();
        _child = null;
        var tail = RecentText();
        var why = ExitText(code) + (tail.Length == 0 ? "" : ", 마지막 출력:\n" + tail);
        var timedOutNow = _timedOut;
        _timedOut = false;
        if (_stage == Stage.Busy)
        {
            if (_cancelRequested)
            {
                Finish(false, "취소했습니다", true);
            }
            else if (timedOutNow)
            {
                Finish(false, "진행 줄이 " + _timing.StageTimeoutMs / 1000 + "초 동안 없어 분석을 중단했습니다", false);
            }
            else
            {
                Finish(false, "관류 분석 프로그램이 비정상 종료했습니다(" + why + ")", false);
            }
            _cancelRequested = false;
            Launch();
            return;
        }
        _cancelRequested = false;
        if (_stage is Stage.AwaitReady or Stage.AwaitPreload)
        {
            // 준비 전에 끝나면 다시 띄워도 같은 이유로 끝나므로 반복하지 않고 사용자에게 보입니다.
            _stage = Stage.Failed;
            _message = timedOutNow
                ? "관류 분석 프로그램이 " + _timing.StageTimeoutMs / 1000 + "초 안에 준비되지 않았습니다"
                : "관류 분석 프로그램이 준비 전에 끝났습니다(" + why + ")";
            return;
        }
        // 대기 중에 끝난 경우(유휴 종료, 비정상 종료)는 다음 요청을 받을 수 있게 다시 띄웁니다.
        Launch();
    }

    private void Run()
    {
        while (true)
        {
            ChildProcess process;
            lock (_gate)
            {
                if (_stopRequested) break;
                if (_child is null)
                {
                    if (_restartRequested)
                    {
                        _restartRequested = false;
                        Launch();
                    }
                    else
                    {
                        Monitor.Wait(_gate, IdleWaitMs);
                    }
                    continue;
                }
                Dispatch();
                var waiting = _stage is Stage.AwaitReady or Stage.AwaitPreload or Stage.Busy;
                if (waiting && !_timedOut && MsSince(_lastActivity) > _timing.StageTimeoutMs)
                {
                    _timedOut = true;
                    _child.Kill();
                }
                process = _child;
            }
            var status = process.ReadLine(out var line, ReadPollMs);
            int? code = status == LineStatus.Closed ? process.Wait(_timing.StopWaitMs) : null;
            lock (_gate)
            {
                if (status == LineStatus.Line) HandleLine(line);
                if (status == LineStatus.Closed && !_stopRequested) HandleExit(code);
            }
        }
        ChildProcess? last;
        bool busy;
        lock (_gate)
        {
            last = _child;
            busy = _stage == Stage.Busy;
        }
        if (last is not null)
        {
            // 입력을 닫으면 관류 분석 프로그램은 다음 요청을 읽다가 끝납니다. 분석 중이면 입력을 읽지 않으므로 기다리지 않고 강제 종료합니다.
            last.CloseInput();
            if (busy || last.Wait(_timing.StopWaitMs) is null) last.Kill();
            last.Wait(_timing.StopWaitMs);
            lock (_gate)
            {
                last.Dispose();
                _child = null;
            }
        }
        lock (_gate)
        {
            CloseLog();
            _stage = Stage.Stopped;
        }
    }
}
