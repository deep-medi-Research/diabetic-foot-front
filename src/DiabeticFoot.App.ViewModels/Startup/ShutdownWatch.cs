using System.Globalization;
using System.Text;

namespace DiabeticFoot.App.Startup;

/// <summary>
/// 앱 종료의 단계 기록과 제한 시간입니다.
/// </summary>
/// <remarks>
/// 종료 중 장치 드라이버 호출(카메라, 열화상)이 돌아오지 않아 창이 닫히지 않거나, 창이 닫힌 뒤 프로세스가 장치를 잡은 채 남은
/// 일이 있었습니다(2026-10-27 노트북, v0.1.8 두 개가 남음). 남은 프로세스는 다음 실행이 카메라를 열지 못하게 합니다.
/// 그래서 단계마다 시각을 shutdown_log.txt에 남기고, 정한 시간 안에 끝나지 않으면 마지막 단계를 남긴 뒤 프로세스를 끝냅니다.
/// 감시는 별도 스레드가 하므로 화면 스레드가 멈춰도 동작합니다.
/// </remarks>
public sealed class ShutdownWatch
{
    /// <summary>기록 파일이 이 크기를 넘으면 비우고 새로 씁니다. 오래 쓰면 커지기 때문입니다.</summary>
    public const long MaxLogBytes = 1024 * 1024;

    private static readonly TimeSpan DefaultPollInterval = TimeSpan.FromMilliseconds(250);

    private readonly object _gate = new();
    private readonly string? _logPath;
    private readonly Action _terminate;
    private readonly TimeProvider _time;
    private readonly TimeSpan _pollInterval;
    private string _lastStep = "";
    private string _limitWhat = "";
    // 0이면 감시하지 않습니다. 감시 스레드가 잠금 없이 읽습니다.
    private long _deadline;
    private Thread? _watcher;

    /// <param name="logPath">기록 파일입니다. null이면 남기지 않습니다. 오래된 파일은 이때 비웁니다</param>
    /// <param name="terminate">제한 시간이 지났을 때 프로세스를 정리 없이 끝내는 동작입니다. 종료 처리 중인 스레드가 드라이버 호출에서 멈춰 있어도 끝나야 합니다</param>
    /// <param name="time">시계. 시험에서만 바꿉니다</param>
    /// <param name="pollInterval">감시 스레드가 시간을 보는 간격입니다. 시험에서만 바꿉니다</param>
    public ShutdownWatch(string? logPath, Action terminate, TimeProvider? time = null, TimeSpan? pollInterval = null)
    {
        _terminate = terminate;
        _time = time ?? TimeProvider.System;
        _pollInterval = pollInterval ?? DefaultPollInterval;
        if (logPath is null) return;
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(logPath)!);
            if (File.Exists(logPath) && new FileInfo(logPath).Length > MaxLogBytes) File.Delete(logPath);
            _logPath = logPath;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            _logPath = null;
        }
    }

    /// <summary>기록 파일의 기본 위치입니다. %LOCALAPPDATA%\deepmedi\hwlab\shutdown_log.txt로, Qt 앱과 같습니다.</summary>
    public static string DefaultLogPath() =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "deepmedi", "hwlab", "shutdown_log.txt");

    /// <summary>지금 제한 시간이 걸려 있으면 true입니다.</summary>
    public bool IsArmed => Interlocked.Read(ref _deadline) != 0;

    /// <summary>단계 하나를 시각과 함께 남깁니다. 어느 스레드에서 불러도 됩니다.</summary>
    public void Log(string step)
    {
        lock (_gate)
        {
            _lastStep = step;
            Append(step);
        }
    }

    /// <summary>
    /// 지금부터 <paramref name="limit"/> 안에 프로세스가 끝나지 않으면, 마지막 단계를 남기고 프로세스를 끝냅니다.
    /// 다시 부르면 제한 시간을 새로 정합니다.
    /// </summary>
    /// <param name="limit">제한 시간</param>
    /// <param name="what">제한 시간을 건 일. 기록에 남습니다</param>
    public void Arm(TimeSpan limit, string what)
    {
        lock (_gate)
        {
            _limitWhat = what;
            _watcher ??= StartWatcher();
        }
        Interlocked.Exchange(ref _deadline, _time.GetTimestamp() + (long)(limit.TotalSeconds * _time.TimestampFrequency));
    }

    /// <summary>제한 시간을 풉니다. 종료가 사람의 확인(복원 불일치 안내)을 기다리는 동안 부릅니다.</summary>
    public void Disarm() => Interlocked.Exchange(ref _deadline, 0);

    /// <summary>제한 시간이 지났으면 기록을 남기고 프로세스를 끝냅니다. 감시 스레드가 주기적으로 부릅니다.</summary>
    /// <returns>끝냈으면 true</returns>
    internal bool CheckDeadline()
    {
        var deadline = Interlocked.Read(ref _deadline);
        if (deadline == 0 || _time.GetTimestamp() < deadline) return false;
        lock (_gate)
        {
            Append($"제한 시간 안에 끝나지 않아 프로세스를 끝냄({_limitWhat}). 마지막 단계: {(_lastStep.Length == 0 ? "-" : _lastStep)}");
        }
        Disarm();
        _terminate();
        return true;
    }

    // 감시 스레드는 앱이 끝나는 중에도 돌아야 하므로 배경 스레드로 둡니다.
    private Thread StartWatcher()
    {
        var thread = new Thread(() =>
        {
            while (true)
            {
                Thread.Sleep(_pollInterval);
                CheckDeadline();
            }
        })
        {
            IsBackground = true,
            Name = "ShutdownWatch",
        };
        thread.Start();
        return thread;
    }

    private void Append(string line)
    {
        if (_logPath is null) return;
        try
        {
            var stamp = _time.GetLocalNow().ToString("yyyy-MM-dd'T'HH:mm:ss.fff", CultureInfo.InvariantCulture);
            File.AppendAllText(_logPath, $"{stamp} {line}\n", new UTF8Encoding(false));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
        }
    }
}
