using System.Diagnostics;
using DiabeticFoot.App.Analysis;
using DiabeticFoot.App.Tests.Support;
using DiabeticFoot.Platform;

namespace DiabeticFoot.App.Tests.Analysis;

/// <summary>
/// 가짜 분석기(tools/DiabeticFoot.FakeAnalyzer)를 앱과 같은 명령줄로 띄우는 시험 장치입니다.
/// 분석 막대(<see cref="AnalysisViewModel"/>)가 분석 연동 약속의 줄을 읽고 화면 문장으로 바꾸는지 실제 프로세스로 봅니다.
/// </summary>
internal sealed class FakeAnalyzerRig : IDisposable
{
    private readonly TempFolder _root = new();
    private readonly PumpingContext _context = new();

    public FakeAnalyzerRig(ProcessLaunch? launch = null, AnalyzerTiming? timing = null)
    {
        Timer = new ManualPollTimer();
        Shell = new RecordingShell();
        Files = new ScriptedAnalysisFiles();
        Time = new ManualTimeProvider();
        var fakeLaunch = FakeLaunch(out var variant);
        Lane = new AnalysisViewModel(launch ?? fakeLaunch, variant, Files, Timer, Shell, timing, Time);
    }

    public ManualPollTimer Timer { get; }

    public RecordingShell Shell { get; }

    public ScriptedAnalysisFiles Files { get; }

    public ManualTimeProvider Time { get; }

    public AnalysisViewModel Lane { get; }

    /// <summary>앱과 같은 방식(배포 폴더의 실행 파일, 스크립트, `--variant`)으로 가짜 분석기를 띄우는 실행 정보입니다. 단계 대기는 0입니다.</summary>
    public static ProcessLaunch FakeLaunch(out AnalyzerVariant variant)
    {
        var dll = Path.Combine(AppContext.BaseDirectory, "DiabeticFoot.FakeAnalyzer.dll");
        // dotnet test는 자식 프로세스에 DOTNET_HOST_PATH를 줍니다. 없으면 같이 복사된 실행 파일로 띄웁니다.
        var host = Environment.GetEnvironmentVariable("DOTNET_HOST_PATH") ?? Path.ChangeExtension(dll, OperatingSystem.IsWindows() ? ".exe" : null);
        var launch = AnalyzerSetup.Launch(AppContext.BaseDirectory, host, dll);
        variant = AnalyzerSetup.FindVariants(launch.WorkingDir, out _).Single(v => v.Id == "fake");
        launch = AnalyzerSetup.VariantLaunch(launch, variant.Id);
        return launch with { Arguments = [.. launch.Arguments, "--stage-ms", "0"] };
    }

    /// <summary>이름이 <paramref name="name"/>인 촬영 폴더의 경로입니다. 가짜 분석기는 폴더 이름이 fail, hang 등이면 그 경우를 흉내 냅니다.</summary>
    public string Shot(string name) => Path.Combine(_root.Path, "홍길동_19551103", name);

    /// <summary>조건이 될 때까지 시계를 울리고 작업 스레드의 결과를 반영합니다.</summary>
    public void PollUntil(Func<bool> condition, string what, int timeoutMs = 30_000)
    {
        var clock = Stopwatch.StartNew();
        while (!condition())
        {
            if (clock.ElapsedMilliseconds > timeoutMs)
            {
                throw new TimeoutException($"{what} 상태 {Lane.State}, 제목 {Lane.Title}, 상세 {Lane.Detail}");
            }
            Timer.Tick();
            _context.Pump(TimeSpan.FromMilliseconds(10));
        }
    }

    /// <summary>작업 스레드에서 끝난 일이 반영될 때까지 기다립니다.</summary>
    public void Pump(Func<bool> condition, string what, int timeoutMs = 30_000)
    {
        var clock = Stopwatch.StartNew();
        while (!condition())
        {
            if (clock.ElapsedMilliseconds > timeoutMs) throw new TimeoutException(what);
            _context.Pump(TimeSpan.FromMilliseconds(10));
        }
    }

    public void Dispose()
    {
        Lane.Shutdown();
        Lane.Dispose();
        _context.Dispose();
        _root.Dispose();
    }
}
