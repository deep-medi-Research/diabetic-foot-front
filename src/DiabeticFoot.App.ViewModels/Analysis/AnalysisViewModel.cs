using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DiabeticFoot.App.Services;
using DiabeticFoot.Platform;

namespace DiabeticFoot.App.Analysis;

/// <summary>
/// 관류 분석 막대의 ViewModel입니다. 연구 코드 판 하나(<see cref="AnalyzerVariant"/>)의 관류 분석 프로그램 하나를 맡습니다.
/// 촬영이 저장되면 분석 입력 확인을 통과한 촬영을 관류 분석 프로그램 대기열에 넣고, 관류 분석 프로그램 상태를 0.2초마다 읽어
/// 화면 문장으로 바꿉니다. 관류 분석 프로그램 파일이 없으면 분석만 하지 않고 촬영은 막지 않습니다.
/// 분석이 성공하면 결과 파일을 저장 위치의 분석결과 폴더로 복사하고(화면 스레드 밖), 그 결과도 막대에 보입니다.
/// 결과는 촬영 폴더의 `perfusion/&lt;판&gt;`에 쓰고, 분석결과 폴더에는 이름 앞부분 뒤에 `_&lt;판&gt;`을 붙여 복사합니다.
/// 판 여러 개의 순서와 사용 여부는 <see cref="AnalysisVariantsViewModel"/>이 정합니다.
/// </summary>
/// <remarks>
/// 화면 스레드에서 만들고 씁니다. 복사와 종료 때 관류 분석 프로그램 정리는 한 번에 하나씩 순서대로 작업 스레드에서 하고,
/// 복사 결과는 부른 스레드의 동기화 문맥(WPF는 화면 스레드)으로 돌아와 반영합니다.
/// </remarks>
public sealed class AnalysisViewModel : ObservableObject, IDisposable
{
    private static readonly TimeSpan PollInterval = TimeSpan.FromMilliseconds(200);

    // 연구 코드의 예외 기록은 수십 줄이므로 화면에는 앞부분만 보이고 전체는 analyzer_log.txt에 남깁니다.
    private const int DetailLines = 3;
    private const int DetailChars = 240;

    // 관류 분석 프로그램(plantar_perfusion.py)의 5단계 이름입니다. 단계 번호는 출력 줄의 [n/5]입니다.
    private static readonly string[] StageNames =
        ["분석 모델 준비", "영상 읽기와 발 찾기", "맥박 신호 추출", "관류 계산", "커프 단계 추정", "결과 파일 저장"];

    // 각 단계가 시작하는 시점의 전체 대비 비율입니다. 2026-10-26 실험 노트북 실제 발 촬영 4건(이진 판정 끔, 1단계부터 26.5초)의
    // 1단계 시작 기준 단계 시각 평균 0, 16.5, 22.5, 23, 24.5초에서 정했습니다. 0단계(모델 올리기)는 앱이 미리 하므로 길이를 0으로 둡니다.
    // 남은 시간 예상에만 쓰므로 다른 PC에서 달라도 분석에는 영향이 없습니다.
    private static readonly double[] StageStart = [0.0, 0.0, 0.62, 0.85, 0.87, 0.92, 1.0];

    // 분석 완료 뒤 그림(figures 줄)을 기다리는 최대 시간입니다. 노트북에서 그림은 약 2.5초 걸립니다(2026-10-26).
    private static readonly TimeSpan FiguresWait = TimeSpan.FromSeconds(30);

    // 진행 비율이 이보다 작으면 남은 시간을 보이지 않습니다. 첫 단계는 영상 읽기라 경과 시간으로 전체를 예상하기 어렵습니다.
    private const double MinProgressForEstimate = 0.1;

    private const int FinishedFoldersKept = 8;

    private readonly ProcessLaunch _launch;
    private readonly AnalyzerTiming _timing;
    private readonly AnalyzerVariant _variant;
    private readonly IAnalysisFiles _files;
    private readonly IPollTimer _timer;
    private readonly IShell _shell;
    private readonly TimeProvider _time;

    private readonly object _backgroundGate = new();
    private Task _background = Task.CompletedTask;

    private Analyzer? _analyzer;
    private string _missing = "";
    private string _lastFolder = "";
    private long _seenFinished;
    private long _seenFigures;
    // 분석은 끝났고 그림을 기다리는 촬영 폴더입니다. 그림까지 끝나야 분석결과 폴더로 복사합니다.
    private string _figuresFolder = "";
    private long _figuresWaitStart;
    private bool _hold;
    // 보류 이유가 앞 판이면 그 판의 화면 이름입니다.
    private string _waitingFor = "";
    // RGB 저장 때 먼저 넣은 촬영. 같은 촬영의 OnShotSaved가 다시 넣지 않게 합니다.
    private string _earlyFolder = "";
    private readonly List<string> _finishedFolders = [];
    private bool _enabled = true;
    private bool _prestream;
    private LastEvent? _event;
    private bool _shutDown;
    private bool _disposed;

    private Display _shown = new(AnalysisState.Idle, "", "");
    private CollectState _collectState;
    private string _collectText = "";
    private string _collectFolder = "";

    /// <param name="launch">이 판을 올리는 관류 분석 프로그램 실행 정보(<see cref="AnalyzerSetup.VariantLaunch"/>의 결과)</param>
    /// <param name="variant">판 정보. Id가 비면 판을 나누지 않은 결과 폴더(`perfusion`)를 씁니다</param>
    /// <param name="files">촬영 폴더와 결과 파일을 다루는 일</param>
    /// <param name="timer">상태를 읽는 시계. 이 객체가 시작하고 멈춥니다</param>
    /// <param name="shell">분석결과 폴더를 여는 일</param>
    /// <param name="timing">관류 분석 프로그램 시간 값. 시험에서만 바꿉니다</param>
    /// <param name="time">경과 시간을 재는 시계. 시험에서만 바꿉니다</param>
    public AnalysisViewModel(ProcessLaunch launch, AnalyzerVariant variant, IAnalysisFiles files, IPollTimer timer, IShell shell,
        AnalyzerTiming? timing = null, TimeProvider? time = null)
    {
        _launch = launch;
        _variant = variant;
        _files = files;
        _timer = timer;
        _shell = shell;
        _timing = timing ?? new AnalyzerTiming();
        _time = time ?? TimeProvider.System;
        RetryCommand = new RelayCommand(Retry, () => CanRetry);
        CancelCommand = new RelayCommand(Cancel, () => CanCancel);
        OpenCollectFolderCommand = new RelayCommand(OpenCollectFolder, () => CollectFolder.Length > 0);
    }

    /// <summary>마지막 촬영(또는 마지막으로 끝난 분석)의 폴더를 다시 대기열에 넣습니다.</summary>
    public IRelayCommand RetryCommand { get; }

    /// <summary>분석 중인 요청을 취소합니다. 관류 분석 프로그램은 다시 띄우고 대기열의 다음 요청을 이어서 분석합니다.</summary>
    public IRelayCommand CancelCommand { get; }

    /// <summary>분석결과 폴더를 파일 탐색기로 엽니다. 경로가 아직 없으면 실행되지 않습니다.</summary>
    public IRelayCommand OpenCollectFolderCommand { get; }

    /// <summary>분석 한 건이 끝났습니다. 폴더는 촬영 폴더입니다.</summary>
    public event EventHandler<AnalysisFinishedEventArgs>? Finished;

    /// <summary>
    /// 끝난 분석의 그림과 관류 지도 GIF를 다 그렸습니다. Finished 뒤에 옵니다. 결과 창은 이때 GIF를 다시 읽습니다.
    /// 관류 분석 프로그램이 그림을 분석 완료 뒤로 미룹니다(<see cref="PerfusionRequest.DeferFigures"/>).
    /// </summary>
    public event EventHandler<string>? FiguresReady;

    /// <summary>분석결과 폴더 복사가 끝났습니다.</summary>
    public event EventHandler<AnalysisCollectedEventArgs>? Collected;

    /// <summary>이 판으로 분석할지입니다. false이면 새 촬영을 넣지 않고 녹화 중 미리 분석도 보내지 않습니다.</summary>
    public bool Enabled
    {
        get => _enabled;
        private set => SetProperty(ref _enabled, value);
    }

    /// <summary>판 이름(<see cref="AnalyzerVariant.Id"/>)입니다.</summary>
    public string VariantId => _variant.Id;

    /// <summary>화면에 보이는 판 이름입니다.</summary>
    public string VariantLabel => _variant.Label;

    /// <summary>판 한 줄 설명입니다. 예: "규칙 판정, 템플릿 v7".</summary>
    public string VariantSummary => _variant.Summary;

    /// <summary>판의 연구 저장소 커밋과 릴리즈 자산(판정 파일과 템플릿이 올라온 날, SHA-256 앞 8자리)입니다. 판 고르기 줄에 보입니다.</summary>
    public string VariantSource
    {
        get
        {
            static string Asset(AnalyzerVariant.Asset a) => a.Name.Length == 0 ? "" : $"{a.Uploaded} ({Head(a.Sha256, 8)})";
            var parts = new List<string>();
            if (_variant.UpstreamCommit.Length > 0) parts.Add($"연구 {Head(_variant.UpstreamCommit, 7)} {_variant.UpstreamDate}");
            var assets = new List<string>();
            foreach (var (role, asset) in new[] { ("판정 ", _variant.Classifier), ("템플릿 ", _variant.PulseTemplate) })
            {
                var text = Asset(asset);
                if (text.Length > 0) assets.Add(role + text);
            }
            if (assets.Count > 0) parts.Add($"{_variant.ReleaseTag}: {string.Join(", ", assets)}");
            return string.Join("\n", parts);
        }
    }

    public AnalysisState State => _shown.State;

    public string Title => _shown.Title;

    public string Detail => _shown.Detail;

    public string ElapsedText => _shown.Elapsed;

    public int Step => _shown.Step;

    public int Total => _shown.Total;

    /// <summary>분석 중일 때 지금 단계의 이름(한국어)입니다. 단계 수가 5가 아닌 판이면 그 판이 찍은 문구를 그대로 씁니다.</summary>
    public string StageText => _shown.StageText;

    /// <summary>분석 중일 때 전체 진행 비율(0~1)입니다. 단계별 실측 시간 비율과 하위 단계로 나눕니다.</summary>
    public double Progress => _shown.Progress;

    /// <summary>분석 중일 때 남은 시간 예상 문장("약 20초 남음")입니다. 진행 비율이 낮아 예상할 수 없으면 비어 있습니다.</summary>
    public string RemainingText => _shown.Remaining;

    public int Queued => _shown.Queued;

    public bool CanRetry => _shown.CanRetry;

    public bool CanCancel => _shown.CanCancel;

    public string SummaryPath => _shown.SummaryPath;

    /// <summary>마지막으로 끝난 분석의 측정 품질 상태(요약 qc.status)입니다. qc가 없으면 <see cref="Analysis.QualityLevel.None"/>입니다.</summary>
    public QualityLevel QualityLevel => _event is { State: AnalysisState.Done } e ? e.Quality : QualityLevel.None;

    /// <summary>측정 품질 문장입니다. 예: "재측정 필요: 양말을 신고 5~10분 기다려 발을 따뜻하게 한 뒤 재촬영하세요."</summary>
    public string QualityText => _event is { State: AnalysisState.Done } e ? e.QualityText : "";

    public CollectState CollectState
    {
        get => _collectState;
        private set => SetProperty(ref _collectState, value);
    }

    /// <summary>복사 결과 문장입니다. 복사한 파일 수, 재분석 번호, 실패 이유를 담습니다.</summary>
    public string CollectText
    {
        get => _collectText;
        private set => SetProperty(ref _collectText, value);
    }

    /// <summary>분석결과 폴더 경로(OS 구분자)입니다. 복사를 시작하기 전에는 비어 있습니다.</summary>
    public string CollectFolder
    {
        get => _collectFolder;
        private set
        {
            if (SetProperty(ref _collectFolder, value)) OpenCollectFolderCommand.NotifyCanExecuteChanged();
        }
    }

    /// <summary>분석 중이거나 대기열에 촬영이 있으면 true입니다. 다음 판의 분석을 미루는 데 씁니다.</summary>
    public bool Active
    {
        get
        {
            if (_analyzer is null) return false;
            var s = _analyzer.Status();
            return s.State == AnalyzerState.Busy || s.Queued > 0;
        }
    }

    /// <summary>이 촬영 폴더의 분석이 끝났는지입니다. 결과 창을 "분석 중"으로 열지 정할 때 씁니다.</summary>
    public bool IsFinished(string folder) => folder.Length > 0 && _finishedFolders.Contains(folder);

    /// <summary>
    /// 관류 분석 프로그램 파일을 확인하고 관류 분석 프로그램을 띄운 뒤 0.2초마다 상태를 읽기 시작합니다.
    /// 파일이 없으면 띄우지 않고 "관류 분석 프로그램 없음"을 보입니다. 다시 분석을 누르면 파일을 다시 확인합니다.
    /// </summary>
    public void Start()
    {
        if (_shutDown) return;
        _missing = "";
        if (AnalyzerSetup.FilesProblem(_launch) is { } problem)
        {
            _missing = problem;
        }
        else if (_analyzer is null)
        {
            _analyzer = Analyzer.Start(_launch, _timing, out var error);
            if (_analyzer is not null) _analyzer.SetHold(_hold);
            else SetEvent(new LastEvent(AnalysisState.Failed, "분석 실패", error));
        }
        if (!_timer.IsRunning) _timer.Start(PollInterval, Poll);
        Poll();
    }

    /// <summary>녹화 중 미리 분석을 요청할지 정합니다. 다음 녹화부터 적용합니다.</summary>
    internal void SetPrestream(bool on) => _prestream = on;

    /// <summary>이 판으로 분석할지 정합니다. 이미 대기열에 넣은 촬영과 분석 중인 촬영은 그대로 끝냅니다.</summary>
    internal void SetEnabled(bool on) => Enabled = on;

    /// <summary>안정화와 녹화 중이거나 앞 판이 분석 중이면 true. 관류 분석 프로그램에 새 요청을 보내지 않습니다.</summary>
    internal void SetHold(bool hold)
    {
        _hold = hold;
        _analyzer?.SetHold(hold);
    }

    /// <summary>
    /// 보류 중일 때 보일 이유입니다. 비우면 녹화 때문에 보류한 것으로 보고 "녹화가 끝나면"으로 적습니다.
    /// </summary>
    /// <param name="waitingFor">앞 판의 화면 이름. 이 판이 그 판의 분석이 끝나기를 기다리면 줍니다</param>
    internal void SetHoldReason(string waitingFor)
    {
        if (waitingFor == _waitingFor) return;
        _waitingFor = waitingFor;
        Poll();
    }

    /// <summary>
    /// 녹화 중인 발 영상의 첫 장을 미리 분할하게 합니다. 녹화 중 미리 분석 스위치가 켜져 있을 때만 보냅니다.
    /// 보류의 예외입니다. 관류 분석 프로그램이 모델을 올리기 전이면 보내지 않고, 그 촬영은 분석 때 분할합니다.
    /// </summary>
    internal void OnFirstFramesWritten(string leftVideo, string rightVideo)
    {
        if (!_enabled || !_prestream || _shutDown || _analyzer is null) return;
        if (!_analyzer.Presegment(leftVideo, rightVideo, out var error))
        {
            System.Diagnostics.Trace.TraceInformation("녹화 중 첫 장 미리 분할을 보내지 않음: " + error);
        }
    }

    /// <summary>
    /// 좌우 발 영상이 저장되어 분석을 시작할 수 있습니다. ready면 바로 대기열에 넣고, 같은 촬영의 <see cref="OnShotSaved"/>에서는 다시 넣지 않습니다.
    /// </summary>
    internal void OnRgbSaved(string folder, bool ready)
    {
        if (_shutDown || !ready || !_enabled) return;
        _lastFolder = folder;
        _earlyFolder = folder;
        Enqueue(folder);
    }

    /// <summary>
    /// 촬영 폴더가 저장되었습니다.
    /// </summary>
    /// <param name="folder">촬영 폴더(전체 경로)</param>
    /// <param name="ready">분석 입력 확인을 통과했으면 true. false이면 분석하지 않고 이유를 보입니다</param>
    internal void OnShotSaved(string folder, bool ready)
    {
        if (_shutDown || !_enabled) return;
        if (folder == _earlyFolder)
        {
            // 녹화가 끝나 RGB 확인을 통과했을 때 이미 넣었습니다. 막는 문제는 RGB에서만 나오므로 여기서 결과가 달라지지 않습니다.
            _earlyFolder = "";
            if (ready) return;
        }
        _lastFolder = folder;
        if (!ready)
        {
            // 관류 분석 프로그램 결과보다 입력 확인 결과를 먼저 보입니다. 이유 문장은 촬영 결과 줄에 있습니다.
            SetEvent(new LastEvent(AnalysisState.Skipped, "분석하지 않음", FolderName(folder) + ": 분석 입력 확인에서 막혔습니다"));
            return;
        }
        Enqueue(folder);
    }

    /// <summary>
    /// 앱을 닫을 때 부릅니다. 상태 읽기를 멈추고, 진행 중인 분석과 대기열을 취소하며 관류 분석 프로그램 프로세스를 끝냅니다.
    /// 관류 분석 프로그램 종료(최대 <see cref="AnalyzerTiming.StopWaitMs"/> 뒤 강제 종료)는 화면 스레드 밖에서 하며, 진행 중인 복사가
    /// 끝난 뒤에 합니다. <see cref="Dispose"/>가 끝나기를 기다립니다. 다시 시작할 수 없습니다.
    /// </summary>
    internal void Shutdown()
    {
        if (_shutDown) return;
        _shutDown = true;
        _timer.Stop();
        var active = false;
        if (_analyzer is not null)
        {
            var s = _analyzer.Status();
            active = s.State == AnalyzerState.Busy || s.Queued > 0;
            // Stop은 관류 분석 프로그램이 끝나기를 기다리므로(최대 StopWaitMs 뒤 강제 종료) 화면 스레드에서 하지 않습니다.
            var analyzer = _analyzer;
            _analyzer = null;
            RunInBackground<object?>(() =>
            {
                analyzer.Dispose();
                return null;
            });
        }
        Show(new Display(AnalysisState.Cancelled, "분석 종료", active ? "앱을 닫아 진행 중인 분석을 취소했습니다" : ""));
    }

    /// <summary>복사와 관류 분석 프로그램 정리가 끝나기를 기다리고 프로세스를 거둡니다.</summary>
    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _timer.Stop();
        // 복사와 정리가 이 객체로 결과를 보내므로 먼저 끝나기를 기다립니다.
        try
        {
            _background.Wait();
        }
        catch (AggregateException)
        {
        }
        _analyzer?.Dispose();
        _analyzer = null;
    }

    private void Retry()
    {
        if (_shutDown) return;
        if (_analyzer is null)
        {
            Start();
            if (_analyzer is null) return;
        }
        if (_analyzer.Status().State == AnalyzerState.Failed) _analyzer.Restart();
        if (_lastFolder.Length > 0) Enqueue(_lastFolder);
    }

    private void Cancel()
    {
        _analyzer?.Cancel();
        Poll();
    }

    private void OpenCollectFolder()
    {
        if (CollectFolder.Length > 0) _shell.OpenFolder(CollectFolder);
    }

    private void Enqueue(string folder)
    {
        if (_analyzer is null)
        {
            if (_missing.Length == 0) SetEvent(new LastEvent(AnalysisState.Failed, "분석 실패", "관류 분석 프로그램을 띄우지 못했습니다"));
            Poll();
            return;
        }
        var request = _files.RequestFor(folder, out var error);
        if (request is not null) request = request with { OutputDir = PerfusionAnalysis.PerfusionOutputDir(request.RecordingDir, _variant.Id) };
        if (request is null || !_analyzer.Submit(request, out error))
        {
            SetEvent(new LastEvent(AnalysisState.Failed, "분석 실패", error));
            return;
        }
        Poll();
    }

    private async void Collect(string folder)
    {
        SetCollect(CollectState.Copying, "results 폴더로 복사 중", "");
        var variantId = _variant.Id;
        try
        {
            var outcome = await RunInBackground(() => SafeCollect(folder, variantId));
            if (_disposed) return;
            SetCollect(outcome.Ok ? CollectState.Done : CollectState.Failed, outcome.Text, outcome.Folder);
            if (!outcome.Ok) System.Diagnostics.Trace.TraceWarning(outcome.Text);
            Collected?.Invoke(this, new AnalysisCollectedEventArgs(folder, outcome.Ok, outcome.Text, outcome.Folder));
        }
        catch (Exception e) when (!_disposed)
        {
            SetCollect(CollectState.Failed, "results 폴더로 복사하지 못함: " + e.Message, "");
        }
    }

    private CollectOutcome SafeCollect(string folder, string variantId)
    {
        try
        {
            return _files.Collect(folder, variantId);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidOperationException)
        {
            return new CollectOutcome(false, "results 폴더로 복사하지 못함: " + e.Message);
        }
    }

    // 작업을 한 번에 하나씩 순서대로 작업 스레드에서 돌립니다. await한 쪽은 부른 스레드의 동기화 문맥(WPF는 화면 스레드)으로 돌아옵니다.
    private Task<T> RunInBackground<T>(Func<T> work)
    {
        lock (_backgroundGate)
        {
            var next = _background.ContinueWith(_ => work(), CancellationToken.None, TaskContinuationOptions.None, TaskScheduler.Default);
            _background = next;
            return next;
        }
    }

    private void SetCollect(CollectState state, string text, string folder)
    {
        CollectState = state;
        CollectText = text;
        CollectFolder = folder;
    }

    private void SetEvent(LastEvent e)
    {
        var (level, text) = (QualityLevel, QualityText);
        _event = e;
        NotifyQuality(level, text);
        Poll();
    }

    private void NotifyQuality(QualityLevel level, string text)
    {
        if (level != QualityLevel) OnPropertyChanged(nameof(QualityLevel));
        if (text != QualityText) OnPropertyChanged(nameof(QualityText));
    }

    private void Poll()
    {
        if (_shutDown) return;
        if (_missing.Length > 0)
        {
            Show(new Display(AnalysisState.Missing, "관류 분석 프로그램 없음", _missing + "\n촬영은 할 수 있고 분석만 하지 않습니다", CanRetry: true));
            return;
        }
        if (_analyzer is null)
        {
            if (_event is not null) Show(new Display(_event.State, _event.Title, _event.Detail, CanRetry: true));
            return;
        }
        var s = _analyzer.Status();
        if (s.FinishedCount != _seenFinished) OnAnalysisFinished(s);
        if (s.FiguresCount != _seenFigures)
        {
            _seenFigures = s.FiguresCount;
            if (_figuresFolder.Length > 0)
            {
                var folder = _figuresFolder;
                _figuresFolder = "";
                FiguresReady?.Invoke(this, folder);
                Collect(folder);
            }
        }
        // 그림 줄이 오지 않으면(그림을 미루지 않는 관류 분석 프로그램, 그리다 멈춤) 기다리지 않고 복사합니다.
        if (_figuresFolder.Length > 0 && _time.GetElapsedTime(_figuresWaitStart) > FiguresWait)
        {
            var folder = _figuresFolder;
            _figuresFolder = "";
            Collect(folder);
        }
        switch (s.State)
        {
            case AnalyzerState.Busy:
            {
                var progress = ProgressOf(s);
                var stage = StageTextOf(s);
                var title = stage.Length == 0 ? "분석 중" : "분석 중: " + stage;
                // 하위 단계 문구는 연구 스크립트의 영어라 화면에는 번호만 보이고 원문은 analyzer_log.txt에 남깁니다.
                var detail = s.CurrentName;
                if (s.SubTotal > 0) detail += $"  (세부 {s.SubStep}/{s.SubTotal})";
                Show(new Display(AnalysisState.Busy, title, detail, "경과 " + Seconds(s.ElapsedMs), s.Step, s.Total, s.Queued,
                    CanCancel: true, StageText: stage, Progress: progress, Remaining: RemainingOf(s.ElapsedMs, progress)));
                return;
            }
            case AnalyzerState.Starting:
                Show(new Display(AnalysisState.Starting, "분석 준비 중",
                    !s.Held ? s.Message
                    : _waitingFor.Length == 0 ? "녹화가 끝나면 분석 모델을 올립니다"
                    : _waitingFor + " 분석이 끝나면 분석 모델을 올립니다", Queued: s.Queued));
                return;
            case AnalyzerState.Failed:
                Show(new Display(AnalysisState.Failed, "분석 실패", Shorten(s.Message), Queued: s.Queued, CanRetry: true));
                return;
            case AnalyzerState.Stopped:
                Show(new Display(AnalysisState.Failed, "분석 멈춤", ""));
                return;
            case AnalyzerState.Idle:
                break;
        }
        if (s.Queued > 0)
        {
            Show(new Display(AnalysisState.Waiting, "분석 대기",
                !s.Held ? ""
                : _waitingFor.Length == 0 ? "녹화가 끝나면 시작합니다"
                : _waitingFor + " 분석이 끝나면 시작합니다", Queued: s.Queued));
            return;
        }
        if (_event is not null)
        {
            Show(new Display(_event.State, _event.Title, _event.Detail, _event.Elapsed, CanRetry: _lastFolder.Length > 0,
                SummaryPath: _event.SummaryPath));
            return;
        }
        Show(new Display(AnalysisState.Idle, "분석 대기", "촬영이 끝나면 자동으로 분석합니다"));
    }

    private void OnAnalysisFinished(AnalyzerStatus s)
    {
        _seenFinished = s.FinishedCount;
        var d = s.LastDone;
        _lastFolder = d.RecordingDir;
        var (level, text) = (QualityLevel, QualityText);
        if (d.Ok)
        {
            var e = new LastEvent(AnalysisState.Done, "분석 완료", d.Name, "걸린 시간 " + Seconds(d.ElapsedMs), d.SummaryPath);
            if (_files.ReadQuality(d.SummaryPath) is { } quality)
            {
                e = e with
                {
                    Quality = QualityOf(quality.Status),
                    QualityText = quality.StatusKo + (quality.ActionKo.Length == 0 ? "" : ": " + quality.ActionKo),
                };
            }
            _event = e;
        }
        else if (d.Cancelled)
        {
            _event = new LastEvent(AnalysisState.Cancelled, "분석 취소", d.Name);
        }
        else
        {
            _event = new LastEvent(AnalysisState.Failed, "분석 실패", d.Name + ": " + Shorten(d.Error));
        }
        NotifyQuality(level, text);
        _finishedFolders.Remove(_lastFolder);
        _finishedFolders.Add(_lastFolder);
        while (_finishedFolders.Count > FinishedFoldersKept) _finishedFolders.RemoveAt(0);
        Finished?.Invoke(this, new AnalysisFinishedEventArgs(_lastFolder, d.Ok, d.SummaryPath));
        // 그림은 분석 완료 뒤에 그립니다. 그림까지 끝나면(figures 줄) 결과 창을 다시 읽고 분석결과 폴더로 복사합니다.
        if (d.Ok)
        {
            // 앞 촬영의 그림 줄을 받지 못한 채 다음 분석이 끝났으면 앞 촬영부터 복사합니다.
            if (_figuresFolder.Length > 0) Collect(_figuresFolder);
            _figuresFolder = _lastFolder;
            _figuresWaitStart = _time.GetTimestamp();
        }
    }

    // 값이 달라진 속성만 알립니다.
    private void Show(Display next)
    {
        var old = _shown;
        if (old == next) return;
        _shown = next;
        if (old.State != next.State) OnPropertyChanged(nameof(State));
        if (old.Title != next.Title) OnPropertyChanged(nameof(Title));
        if (old.Detail != next.Detail) OnPropertyChanged(nameof(Detail));
        if (old.Elapsed != next.Elapsed) OnPropertyChanged(nameof(ElapsedText));
        if (old.Step != next.Step) OnPropertyChanged(nameof(Step));
        if (old.Total != next.Total) OnPropertyChanged(nameof(Total));
        if (old.StageText != next.StageText) OnPropertyChanged(nameof(StageText));
        if (old.Progress != next.Progress) OnPropertyChanged(nameof(Progress));
        if (old.Remaining != next.Remaining) OnPropertyChanged(nameof(RemainingText));
        if (old.Queued != next.Queued) OnPropertyChanged(nameof(Queued));
        if (old.SummaryPath != next.SummaryPath) OnPropertyChanged(nameof(SummaryPath));
        if (old.CanRetry != next.CanRetry)
        {
            OnPropertyChanged(nameof(CanRetry));
            RetryCommand.NotifyCanExecuteChanged();
        }
        if (old.CanCancel != next.CanCancel)
        {
            OnPropertyChanged(nameof(CanCancel));
            CancelCommand.NotifyCanExecuteChanged();
        }
    }

    // 진행 단계와 하위 단계로 전체 진행 비율을 계산합니다. 5단계가 아니면 단계 수로 균등하게 나눕니다.
    internal static double ProgressOf(AnalyzerStatus s)
    {
        if (s.Total <= 0) return 0;
        var sub = s.SubTotal > 0 ? (double)s.SubStep / s.SubTotal : 0.0;
        if (s.Total == 5 && s.Step is >= 0 and <= 5)
        {
            var start = StageStart[s.Step];
            var end = StageStart[s.Step + 1];
            return start + (end - start) * sub;
        }
        return Math.Min(1.0, (s.Step + sub) / s.Total);
    }

    internal static string StageTextOf(AnalyzerStatus s) =>
        s.Total == 5 && s.Step is >= 0 and <= 5 ? StageNames[s.Step] : s.Message;

    // 남은 시간 예상입니다. 5초 단위로 올려 숫자가 매번 흔들리지 않게 합니다.
    internal static string RemainingOf(long elapsedMs, double progress)
    {
        if (progress < MinProgressForEstimate || progress >= 1.0) return "";
        var leftSeconds = elapsedMs / 1000.0 * (1.0 - progress) / progress;
        var rounded = Math.Max(5, (int)Math.Ceiling(leftSeconds / 5.0) * 5);
        return $"약 {rounded}초 남음";
    }

    internal static string Seconds(long ms) => $"{(ms + 500) / 1000}초";

    // 오류 문구의 앞부분만 보입니다.
    internal static string Shorten(string text)
    {
        var lines = text.Split('\n', StringSplitOptions.RemoveEmptyEntries);
        var cutLines = lines.Length > DetailLines;
        var joined = string.Join("\n", cutLines ? lines[..DetailLines] : lines);
        if (joined.Length > DetailChars) return joined[..DetailChars] + "…";
        return cutLines ? joined + "\n…" : joined;
    }

    internal static string FolderName(string folder) =>
        folder.Replace('\\', '/').Split('/', StringSplitOptions.RemoveEmptyEntries).LastOrDefault() ?? "";

    private static QualityLevel QualityOf(string status) => status switch
    {
        "retake" => QualityLevel.Retake,
        "check" => QualityLevel.Check,
        _ => QualityLevel.Ok,
    };

    private static string Head(string text, int length) => text.Length <= length ? text : text[..length];

    // 대기 중일 때 보이는 마지막 일입니다. 새 일이 생기면 바뀝니다.
    private sealed record LastEvent(AnalysisState State, string Title, string Detail, string Elapsed = "", string SummaryPath = "")
    {
        public QualityLevel Quality { get; init; }

        public string QualityText { get; init; } = "";
    }

    // 막대가 지금 보이는 문장과 값입니다.
    private sealed record Display(
        AnalysisState State,
        string Title,
        string Detail,
        string Elapsed = "",
        int Step = 0,
        int Total = 0,
        int Queued = 0,
        bool CanRetry = false,
        bool CanCancel = false,
        string SummaryPath = "",
        string StageText = "",
        double Progress = 0,
        string Remaining = "");
}

/// <summary>분석 한 건이 끝났습니다.</summary>
/// <param name="Folder">촬영 폴더입니다.</param>
/// <param name="Ok">성공이면 true입니다.</param>
/// <param name="SummaryPath">요약 JSON 경로입니다.</param>
public sealed record AnalysisFinishedEventArgs(string Folder, bool Ok, string SummaryPath);

/// <summary>분석결과 폴더 복사가 끝났습니다.</summary>
/// <param name="Folder">촬영 폴더입니다.</param>
/// <param name="Ok">복사했거나 같은 내용이 이미 있으면 true입니다.</param>
/// <param name="Text">화면 문장입니다.</param>
/// <param name="CollectFolder">분석결과 폴더입니다. 촬영 폴더 경로를 나누지 못했으면 비어 있습니다.</param>
public sealed record AnalysisCollectedEventArgs(string Folder, bool Ok, string Text, string CollectFolder);
