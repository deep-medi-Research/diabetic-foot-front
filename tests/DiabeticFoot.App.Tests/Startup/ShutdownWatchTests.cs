using DiabeticFoot.App.Startup;
using DiabeticFoot.App.Tests.Support;

namespace DiabeticFoot.App.Tests.Startup;

public sealed class ShutdownWatchTests : IDisposable
{
    private readonly TempFolder _folder = new();
    private readonly ManualTimeProvider _time = new();
    private readonly List<string> _terminations = [];
    private readonly string _log;

    public ShutdownWatchTests()
    {
        _log = Path.Combine(_folder.Path, "로그", "shutdown_log.txt");
    }

    public void Dispose() => _folder.Dispose();

    private ShutdownWatch Watch(TimeSpan? pollInterval = null) =>
        new(_log, () => _terminations.Add("끝냄"), _time, pollInterval ?? TimeSpan.FromHours(1));

    // 단계마다 시각과 함께 한 줄씩 덧붙입니다. 폴더가 없으면 만듭니다.
    [Fact]
    public void AppendsStepsWithTimestamp()
    {
        var watch = Watch();

        watch.Log("창 닫힘, 장치 닫기 시작");
        _time.Advance(TimeSpan.FromMilliseconds(1250));
        watch.Log("정리 끝");

        Assert.Equal(["2026-11-05T09:30:00.000 창 닫힘, 장치 닫기 시작", "2026-11-05T09:30:01.250 정리 끝"], File.ReadAllLines(_log));
    }

    // 오래 쓰면 커지므로 1MB를 넘은 기록 파일은 시작할 때 비우고 새로 씁니다.
    [Fact]
    public void ClearsLogLargerThanOneMegabyte()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(_log)!);
        File.WriteAllBytes(_log, new byte[ShutdownWatch.MaxLogBytes + 1]);

        Watch().Log("처음");

        Assert.Single(File.ReadAllLines(_log));

        Directory.CreateDirectory(Path.Combine(_folder.Path, "작은"));
        var small = Path.Combine(_folder.Path, "작은", "log.txt");
        File.WriteAllText(small, "앞 기록\n");
        new ShutdownWatch(small, () => { }, _time).Log("이어서");
        Assert.Equal(2, File.ReadAllLines(small).Length);
    }

    // 기록 위치가 없어도 동작은 같습니다.
    [Fact]
    public void WorksWithoutLogFile()
    {
        var watch = new ShutdownWatch(null, () => _terminations.Add("끝냄"), _time);

        watch.Log("단계");
        watch.Arm(TimeSpan.FromSeconds(1), "정리");
        _time.Advance(TimeSpan.FromSeconds(2));

        Assert.True(watch.CheckDeadline());
        Assert.Equal(["끝냄"], _terminations);
    }

    // 제한 시간이 지나면 마지막 단계를 남기고 프로세스를 끝냅니다. 한 번만 끝냅니다.
    [Fact]
    public void TerminatesAfterLimitWithLastStep()
    {
        var watch = Watch();
        watch.Log("장치 닫기 시작");
        watch.Arm(TimeSpan.FromSeconds(15), "창을 닫은 뒤 정리 15초");

        _time.Advance(TimeSpan.FromSeconds(14));
        Assert.False(watch.CheckDeadline());
        Assert.True(watch.IsArmed);
        Assert.Empty(_terminations);

        _time.Advance(TimeSpan.FromSeconds(2));
        Assert.True(watch.CheckDeadline());
        Assert.False(watch.CheckDeadline());

        Assert.Equal(["끝냄"], _terminations);
        Assert.False(watch.IsArmed);
        Assert.EndsWith("제한 시간 안에 끝나지 않아 프로세스를 끝냄(창을 닫은 뒤 정리 15초). 마지막 단계: 장치 닫기 시작", File.ReadAllLines(_log)[^1]);
    }

    [Fact]
    public void LastStepIsDashWhenNothingWasLogged()
    {
        var watch = Watch();
        watch.Arm(TimeSpan.FromSeconds(1), "정리");
        _time.Advance(TimeSpan.FromSeconds(2));

        watch.CheckDeadline();

        Assert.EndsWith("마지막 단계: -", File.ReadAllLines(_log)[^1]);
    }

    // 제한 시간을 풀면 끝내지 않고, 다시 걸면 그 시점부터 새로 잽니다.
    [Fact]
    public void DisarmCancelsAndArmRestartsTheLimit()
    {
        var watch = Watch();
        watch.Arm(TimeSpan.FromSeconds(10), "정리");
        _time.Advance(TimeSpan.FromSeconds(8));
        watch.Disarm();
        _time.Advance(TimeSpan.FromSeconds(60));
        Assert.False(watch.CheckDeadline());
        Assert.False(watch.IsArmed);

        watch.Arm(TimeSpan.FromSeconds(10), "정리");
        _time.Advance(TimeSpan.FromSeconds(8));
        watch.Arm(TimeSpan.FromSeconds(10), "다시");
        _time.Advance(TimeSpan.FromSeconds(8));
        Assert.False(watch.CheckDeadline());
        _time.Advance(TimeSpan.FromSeconds(3));
        Assert.True(watch.CheckDeadline());
        Assert.Contains("(다시)", File.ReadAllLines(_log)[^1]);
    }

    // 감시 스레드는 화면 스레드와 관계없이 제한 시간이 지나면 프로세스를 끝냅니다.
    [Fact]
    public void WatcherThreadTerminatesAfterLimit()
    {
        using var terminated = new ManualResetEventSlim();
        var watch = new ShutdownWatch(_log, terminated.Set, _time, TimeSpan.FromMilliseconds(5));

        watch.Arm(TimeSpan.FromSeconds(15), "정리");
        Assert.False(terminated.Wait(50));
        _time.Advance(TimeSpan.FromSeconds(16));

        Assert.True(terminated.Wait(TimeSpan.FromSeconds(10)));
    }
}
