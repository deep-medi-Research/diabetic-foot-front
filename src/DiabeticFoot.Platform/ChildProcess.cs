using System.ComponentModel;
using System.Diagnostics;
using System.Text;

namespace DiabeticFoot.Platform;

/// <summary>자식 프로세스를 띄울 때 넘기는 값입니다.</summary>
public sealed record ProcessLaunch
{
    /// <summary>실행 파일 경로. 경로 구분자가 없으면 PATH에서 찾습니다.</summary>
    public string Program { get; init; } = "";

    /// <summary>프로그램 이름 뒤에 붙는 인자. 공백, 따옴표, 한글을 그대로 넘깁니다.</summary>
    public IReadOnlyList<string> Arguments { get; init; } = [];

    /// <summary>작업 폴더. 비어 있으면 이 프로세스의 작업 폴더를 그대로 씁니다.</summary>
    public string WorkingDir { get; init; } = "";

    /// <summary>상속한 환경 변수에 더하거나 덮어쓸 (이름, 값) 목록입니다.</summary>
    public IReadOnlyList<(string Name, string Value)> ExtraEnv { get; init; } = [];

    /// <summary>
    /// 낮은 우선순위(BelowNormal)로 띄울지. 분석 중에 다음 녹화를 하면 FFV1 쓰기(실험 노트북에서 CPU 코어 약 3.8개)와 CPU를 나눠 쓰므로,
    /// 녹화 쪽 스레드가 지연되지 않게 관류 분석 프로그램 우선순위를 낮춥니다.
    /// </summary>
    public bool LowPriority { get; init; }
}

/// <summary>ReadLine의 결과입니다.</summary>
public enum LineStatus
{
    /// <summary>한 줄을 읽음.</summary>
    Line,
    /// <summary>제한 시간 안에 줄이 오지 않음.</summary>
    Timeout,
    /// <summary>출력이 닫혔고 남은 줄이 없음.</summary>
    Closed,
}

/// <summary>
/// 자기가 띄운 자식 프로세스 하나와 그 표준 입출력입니다.
/// 표준 출력과 표준 오류는 하나의 줄 대기열로 합쳐 받습니다. 출력은 내부 스레드가 계속 읽어 줄 단위로 쌓으므로,
/// 호출자가 ReadLine을 부르지 않아도 자식이 출력 파이프에서 막히지 않습니다.
/// Dispose는 아직 실행 중인 자식을 Kill하고 거둡니다. 강제 종료는 이 객체가 띄운 프로세스와 그 하위 프로세스에만 닿습니다.
/// Dispose 전에는 모든 멤버를 여러 스레드에서 불러도 됩니다.
/// </summary>
/// <remarks>
/// 두 출력은 파이프가 둘이라 줄이 쓰인 순서가 아니라 읽기 스레드에 닿은 순서로 쌓입니다. 몇 ms 안에 이어서 쓴 표준 출력 줄과
/// 표준 오류 줄은 순서가 바뀔 수 있습니다. 관류 분석 프로그램의 진행 줄과 완료 줄은 모두 표준 출력이라 영향이 없습니다.
/// </remarks>
public sealed class ChildProcess : IDisposable
{
    /// <summary>Kill로 끝난 프로세스의 종료 코드입니다. 운영체제와 관계없이 같은 값을 냅니다.</summary>
    public const int KilledExitCode = -9;

    private const int StdoutSource = 0;
    private const int StderrSource = 1;
    private static readonly TimeSpan PumpGrace = TimeSpan.FromSeconds(2);

    private readonly Process _process;
    private readonly WindowsJob? _job;
    private readonly StreamWriter _stdinWriter;
    private readonly Stream _stdin;
    private readonly OutputLines _output = new(2);
    private readonly Thread[] _pumps;
    private readonly object _killLock = new();
    private readonly object _writeLock = new();
    private bool _stdinClosed;
    private volatile bool _killed;
    private int _disposed;

    private ChildProcess(Process process, WindowsJob? job)
    {
        _process = process;
        _job = job;
        Id = process.Id;
        _stdinWriter = process.StandardInput;
        _stdin = _stdinWriter.BaseStream;
        _pumps =
        [
            StartPump(process.StandardOutput.BaseStream, StdoutSource),
            StartPump(process.StandardError.BaseStream, StderrSource),
        ];
    }

    /// <summary>운영체제의 프로세스 번호입니다.</summary>
    public long Id { get; }

    /// <summary>
    /// 자식 프로세스를 띄웁니다. 실행 파일이 없거나 실행하지 못하면 null이고 error에 이유를 씁니다.
    /// </summary>
    public static ChildProcess? Start(ProcessLaunch launch, out string error)
    {
        error = "";
        if (launch.WorkingDir.Length > 0 && !Directory.Exists(launch.WorkingDir))
        {
            error = "작업 폴더가 없습니다: " + launch.WorkingDir;
            return null;
        }
        var info = new ProcessStartInfo(launch.Program)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardInputEncoding = new UTF8Encoding(false),
        };
        if (launch.WorkingDir.Length > 0) info.WorkingDirectory = launch.WorkingDir;
        foreach (var argument in launch.Arguments) info.ArgumentList.Add(argument);
        foreach (var (name, value) in launch.ExtraEnv) info.Environment[name] = value;

        Process? process;
        try
        {
            process = Process.Start(info);
        }
        catch (Win32Exception e)
        {
            error = Describe(e) + ": " + launch.Program;
            return null;
        }
        catch (InvalidOperationException)
        {
            error = "실행 파일을 찾지 못했습니다: " + launch.Program;
            return null;
        }
        if (process is null)
        {
            error = "실행하지 못했습니다: " + launch.Program;
            return null;
        }
        // 실패해도 분석은 돌려야 하므로 우선순위를 낮추지 못한 것은 넘어갑니다.
        if (launch.LowPriority) TrySetBelowNormal(process);
        var job = OperatingSystem.IsWindows() ? WindowsJob.TryAssign(process) : null;
        return new ChildProcess(process, job);
    }

    /// <summary>
    /// 표준 입력에 한 줄을 씁니다. 끝에 '\n'을 붙입니다. 자식이 입력을 닫았거나 끝났으면 false입니다.
    /// 자식이 입력을 읽지 않는 동안 파이프 버퍼(수 KB 이상)를 넘게 쓰면 읽을 때까지 기다립니다.
    /// </summary>
    public bool WriteLine(string line)
    {
        lock (_writeLock)
        {
            if (_stdinClosed) return false;
            try
            {
                _stdin.Write(Encoding.UTF8.GetBytes(line + "\n"));
                _stdin.Flush();
                return true;
            }
            catch (Exception e) when (e is IOException or ObjectDisposedException)
            {
                return false;
            }
        }
    }

    /// <summary>표준 입력을 닫습니다. 자식은 입력 끝(EOF)을 읽습니다.</summary>
    public void CloseInput()
    {
        lock (_writeLock)
        {
            if (_stdinClosed) return;
            _stdinClosed = true;
            try
            {
                _stdinWriter.Close();
            }
            catch (Exception e) when (e is IOException or ObjectDisposedException)
            {
            }
        }
    }

    /// <summary>
    /// 출력 한 줄을 읽습니다. 줄 끝의 '\n'과 '\r'은 제거합니다. 자식이 끝나며 '\n' 없이 남긴 마지막 조각도 한 줄로 돌려줍니다.
    /// timeoutMs가 0이면 기다리지 않습니다.
    /// </summary>
    public LineStatus ReadLine(out string line, long timeoutMs) => _output.Read(out line, timeoutMs);

    /// <summary>
    /// 자식이 끝나기를 기다립니다. timeoutMs가 0이면 기다리지 않고 지금 상태만 봅니다.
    /// 시간 안에 끝나지 않으면 null이고, Kill로 끝났으면 KilledExitCode입니다.
    /// </summary>
    public int? Wait(long timeoutMs)
    {
        var exited = timeoutMs <= 0 ? _process.HasExited : _process.WaitForExit((int)Math.Min(timeoutMs, int.MaxValue - 1));
        if (!exited) return null;
        return _killed ? KilledExitCode : _process.ExitCode;
    }

    /// <summary>자식과 그 하위 프로세스를 강제로 끝냅니다. 이미 끝났으면 아무것도 하지 않습니다.</summary>
    public void Kill()
    {
        lock (_killLock)
        {
            if (Volatile.Read(ref _disposed) == 0) KillRunning();
        }
    }

    /// <summary>실행 중이면 Kill하고, 자식을 거두고, 읽기 스레드를 마칩니다. 쓰기에 막힌 호출자가 풀리도록 입력은 Kill 뒤에 닫습니다.</summary>
    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
        lock (_killLock) KillRunning();
        CloseInput();
        _process.WaitForExit();
        // 작업 개체나 프로세스 그룹 밖에 남은 하위 프로세스가 출력 파이프를 쥐고 있으면 읽기가 끝나지 않으므로 잠시만 기다립니다.
        for (var source = 0; source < _pumps.Length; source++)
        {
            if (!_pumps[source].Join(PumpGrace)) _output.CloseSource(source);
        }
        _process.Dispose();
        if (OperatingSystem.IsWindows()) _job?.Dispose();
    }

    private void KillRunning()
    {
        try
        {
            if (_process.HasExited) return;
            if (!(OperatingSystem.IsWindows() && _job is { } job && job.Terminate(KilledExitCode)))
            {
                _process.Kill(entireProcessTree: true);
            }
            _killed = true;
        }
        catch (Exception e) when (e is InvalidOperationException or Win32Exception or NotSupportedException or AggregateException)
        {
            // 그 사이에 자식이 스스로 끝났으면 종료 코드는 자식이 낸 값 그대로입니다.
        }
    }

    private Thread StartPump(Stream stream, int source)
    {
        var thread = new Thread(() => Pump(stream, source)) { IsBackground = true, Name = $"ChildProcess-{Id}-{source}" };
        thread.Start();
        return thread;
    }

    private void Pump(Stream stream, int source)
    {
        var buffer = new byte[65536];
        try
        {
            int got;
            while ((got = stream.Read(buffer, 0, buffer.Length)) > 0) _output.Append(source, buffer.AsSpan(0, got));
        }
        catch (Exception e) when (e is IOException or ObjectDisposedException)
        {
        }
        finally
        {
            _output.CloseSource(source);
        }
    }

    private static void TrySetBelowNormal(Process process)
    {
        try
        {
            process.PriorityClass = ProcessPriorityClass.BelowNormal;
        }
        catch (Exception e) when (e is InvalidOperationException or Win32Exception or NotSupportedException)
        {
        }
    }

    private static string Describe(Win32Exception e) => e.NativeErrorCode switch
    {
        2 or 3 => "실행 파일을 찾지 못했습니다",
        5 when OperatingSystem.IsWindows() => "실행 권한이 없습니다",
        13 when !OperatingSystem.IsWindows() => "실행 권한이 없습니다",
        _ => $"실행하지 못했습니다({e.Message})",
    };
}
