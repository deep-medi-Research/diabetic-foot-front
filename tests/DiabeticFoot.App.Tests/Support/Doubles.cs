using System.Collections.Concurrent;
using DiabeticFoot.App.Analysis;
using DiabeticFoot.App.Services;
using DiabeticFoot.Platform;

namespace DiabeticFoot.App.Tests.Support;

/// <summary>시험이 직접 울리는 시계입니다.</summary>
internal sealed class ManualPollTimer : IPollTimer
{
    private Action? _tick;

    public bool IsRunning { get; private set; }

    public TimeSpan Interval { get; private set; }

    public void Start(TimeSpan interval, Action tick)
    {
        IsRunning = true;
        Interval = interval;
        _tick = tick;
    }

    public void Stop() => IsRunning = false;

    public void Tick()
    {
        if (IsRunning) _tick!();
    }
}

internal sealed class RecordingShell : IShell
{
    public List<string> Opened { get; } = [];

    public void OpenFolder(string path) => Opened.Add(path);
}

/// <summary>시험이 정한 만큼만 흐르는 시계입니다.</summary>
internal sealed class ManualTimeProvider : TimeProvider
{
    private long _milliseconds;

    public override long TimestampFrequency => 1000;

    public override long GetTimestamp() => _milliseconds;

    public void Advance(TimeSpan span) => _milliseconds += (long)span.TotalMilliseconds;
}

/// <summary>
/// 작업 스레드에서 끝난 일이 만든 스레드로 돌아오는 길입니다. WPF의 화면 스레드처럼 시험 스레드가 직접 꺼내 실행합니다.
/// 만든 스레드에서 쓰고 지워야 하므로 시험 안에서 만들고 Dispose로 되돌립니다.
/// </summary>
internal sealed class PumpingContext : SynchronizationContext, IDisposable
{
    private readonly BlockingCollection<(SendOrPostCallback Callback, object? State)> _queue = new();
    private readonly SynchronizationContext? _previous = Current;

    public PumpingContext()
    {
        SetSynchronizationContext(this);
    }

    public override void Post(SendOrPostCallback d, object? state) => _queue.Add((d, state));

    /// <summary>쌓인 일을 실행합니다. 하나도 없으면 최대 <paramref name="wait"/>만큼 기다립니다.</summary>
    public void Pump(TimeSpan wait)
    {
        if (!_queue.TryTake(out var item, wait)) return;
        do
        {
            item.Callback(item.State);
        }
        while (_queue.TryTake(out item));
    }

    public void Dispose() => SetSynchronizationContext(_previous);
}

/// <summary>
/// 촬영 폴더와 결과 파일을 다루는 일의 시험용 구현입니다. 요청은 폴더 이름을 결과 이름으로 쓰고 좌우 영상 파일(내용 없음)을 만듭니다.
/// </summary>
internal sealed class ScriptedAnalysisFiles : IAnalysisFiles
{
    private readonly object _gate = new();
    private readonly List<(string Folder, string Variant)> _collected = [];

    public List<string> Requested { get; } = [];

    public bool DeferFigures { get; set; } = true;

    public MeasurementQuality? Quality { get; set; } = new("check", "측정 완료 (확인 필요)", "조명을 확인하세요");

    public CollectOutcome Outcome { get; set; } = new(true, "results 폴더로 3개 복사", "/저장/results");

    public IReadOnlyList<(string Folder, string Variant)> Collected
    {
        get
        {
            lock (_gate) return _collected.ToList();
        }
    }

    public PerfusionRequest? RequestFor(string shotFolder, out string error)
    {
        Requested.Add(shotFolder);
        error = "";
        var stem = Path.GetFileName(shotFolder.TrimEnd('/', '\\'));
        Directory.CreateDirectory(shotFolder);
        var left = Path.Combine(shotFolder, $"{stem}_left.mkv");
        var right = Path.Combine(shotFolder, $"{stem}_right.mkv");
        File.WriteAllBytes(left, [1]);
        File.WriteAllBytes(right, [1]);
        return new PerfusionRequest
        {
            RecordingDir = shotFolder,
            LeftVideo = left,
            RightVideo = right,
            LeftFrames = 900,
            RightFrames = 900,
            Name = stem,
            DeferFigures = DeferFigures,
        };
    }

    public MeasurementQuality? ReadQuality(string summaryPath) => Quality;

    public CollectOutcome Collect(string shotFolder, string variantId)
    {
        lock (_gate) _collected.Add((shotFolder, variantId));
        return Outcome;
    }
}
