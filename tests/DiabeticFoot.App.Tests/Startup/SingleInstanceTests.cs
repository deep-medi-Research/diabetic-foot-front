using DiabeticFoot.App.Startup;

namespace DiabeticFoot.App.Tests.Startup;

public sealed class SingleInstanceTests
{
    // 다른 실행이 없으면(잠금을 처음 만들면) 바로 시작하고 자기 프로세스 번호를 남깁니다.
    [Fact]
    public void FirstInstanceOwnsDevices()
    {
        var host = new ScriptedHost { Created = true };

        Assert.Equal(InstanceClaim.Alone, SingleInstance.ClaimDevices(host));

        Assert.Equal(["CreateLock", "PublishOwner"], host.Calls);
    }

    // 잠금을 만들 수 없으면 막지 않고 시작합니다.
    [Fact]
    public void StartsWhenLockCannotBeCreated()
    {
        var host = new ScriptedHost { Created = null };

        Assert.Equal(InstanceClaim.Alone, SingleInstance.ClaimDevices(host));

        Assert.Equal(["CreateLock"], host.Calls);
    }

    // 앞 실행이 방금 끝나는 중이면 1.5초 안에 잠금을 넘겨받고 시작합니다.
    [Fact]
    public void TakesOverFromInstanceThatIsExiting()
    {
        var host = new ScriptedHost { Created = false, LockResults = [true] };

        Assert.Equal(InstanceClaim.Alone, SingleInstance.ClaimDevices(host));

        Assert.Equal([TimeSpan.FromMilliseconds(1500)], host.Waits);
        Assert.Equal(["CreateLock", "WaitForLock", "PublishOwner"], host.Calls);
    }

    // 창이 있는 다른 실행이 있으면 그 창을 앞으로 가져오고 이 실행은 끝냅니다. 장치를 가진 실행이 알려져 있으면 그 실행의 창만 찾습니다.
    [Fact]
    public void ShowsWindowOfRunningInstance()
    {
        var host = new ScriptedHost { Created = false, LockResults = [false], Others = [10, 11], Owner = 11, WindowAt = 0 };

        Assert.Equal(InstanceClaim.ShownOther, SingleInstance.ClaimDevices(host));

        Assert.Equal([[11]], host.WindowSearches);
        Assert.Empty(host.Terminated);
        Assert.Equal(0, host.AskCount);
    }

    // 앞 실행이 아직 창을 띄우기 전이면 0.5초씩 기다리며 창을 다시 찾고, 그 사이 잠금이 풀리면 넘겨받습니다.
    [Fact]
    public void WaitsForWindowOrLockWhileOtherStarts()
    {
        var host = new ScriptedHost { Created = false, LockResults = [false, false, false, true], Others = [10], Owner = 0 };

        Assert.Equal(InstanceClaim.Alone, SingleInstance.ClaimDevices(host));

        Assert.Equal([TimeSpan.FromMilliseconds(1500), TimeSpan.FromMilliseconds(500), TimeSpan.FromMilliseconds(500), TimeSpan.FromMilliseconds(500)], host.Waits);
        Assert.Equal(2, host.WindowSearches.Count - 1);
        Assert.Equal(0, host.AskCount);
    }

    // 10초 동안 창이 보이지 않으면 종료 중에 멈춘 실행으로 보고 끝낼지 묻습니다. 아니오를 고르면 이 실행이 끝납니다.
    [Fact]
    public void StopsWhenUserDeclinesToEndStuckInstance()
    {
        var host = new ScriptedHost { Created = false, LockResults = [false], Others = [10, 11], Owner = 0, AskResult = false };

        Assert.Equal(InstanceClaim.Stopped, SingleInstance.ClaimDevices(host));

        Assert.Equal(1 + 21, host.Waits.Count);
        Assert.Equal(21, host.WindowSearches.Count);
        Assert.Equal(1, host.AskCount);
        Assert.Equal([10, 11], host.AskedAbout);
        Assert.Empty(host.Terminated);
        Assert.False(host.NotifiedStillRunning);
    }

    // 예를 고르면 남은 프로세스를 끝내고 15초까지 잠금을 기다려 넘겨받습니다.
    [Fact]
    public void EndsStuckInstanceAndTakesOver()
    {
        var results = Enumerable.Repeat(false, 22).Append(true).ToList();
        var host = new ScriptedHost { Created = false, LockResults = results, Others = [10, 11], Owner = 10, AskResult = true };

        Assert.Equal(InstanceClaim.Alone, SingleInstance.ClaimDevices(host));

        Assert.Equal([10], host.Terminated);
        Assert.Equal(TimeSpan.FromSeconds(15), host.Waits[^1]);
        Assert.Equal("PublishOwner", host.Calls[^1]);
    }

    // 끝낸 뒤에도 잠금이 풀리지 않으면 드라이버가 정리하는 동안 기다리라고 알리고 이 실행이 끝납니다.
    [Fact]
    public void NotifiesWhenStuckInstanceStillHoldsLock()
    {
        var host = new ScriptedHost { Created = false, LockResults = [false], Others = [10], Owner = 0, AskResult = true };

        Assert.Equal(InstanceClaim.Stopped, SingleInstance.ClaimDevices(host));

        Assert.Equal([10], host.Terminated);
        Assert.True(host.NotifiedStillRunning);
    }

    // 같은 이름의 다른 프로세스가 하나도 없으면 기다리지 않고 바로 묻습니다.
    [Fact]
    public void AsksImmediatelyWhenNoOtherProcessIsVisible()
    {
        var host = new ScriptedHost { Created = false, LockResults = [false], Others = [], Owner = 0, AskResult = false };

        SingleInstance.ClaimDevices(host);

        Assert.Single(host.Waits);
        Assert.Single(host.WindowSearches);
        Assert.Equal(1, host.AskCount);
    }

    // 장치를 가진 실행이 아직 살아 있으면 그것만, 아니면 같은 이름의 다른 실행 모두를 후보로 봅니다.
    [Theory]
    [InlineData(new[] { 10, 11 }, 11, new[] { 11 })]
    [InlineData(new[] { 10, 11 }, 99, new[] { 10, 11 })]
    [InlineData(new[] { 10, 11 }, 0, new[] { 10, 11 })]
    [InlineData(new int[0], 11, new int[0])]
    public void ChoosesHolderCandidates(int[] others, int owner, int[] expected)
    {
        var host = new ScriptedHost { Others = others, Owner = owner };

        Assert.Equal(expected, SingleInstance.HolderCandidates(host));
    }

    private sealed class ScriptedHost : IInstanceHost
    {
        private int _next;

        public bool? Created { get; init; }

        public IReadOnlyList<bool> LockResults { get; init; } = [];

        public IReadOnlyList<int> Others { get; init; } = [];

        public int Owner { get; init; }

        /// <summary>창을 찾는 순서(0부터)가 이 값에 이르면 창이 보입니다. 기본은 끝까지 보이지 않습니다.</summary>
        public int WindowAt { get; init; } = int.MaxValue;

        public bool AskResult { get; init; }

        public List<string> Calls { get; } = [];

        public List<TimeSpan> Waits { get; } = [];

        public List<IReadOnlyList<int>> WindowSearches { get; } = [];

        public List<int> Terminated { get; } = [];

        public IReadOnlyList<int> AskedAbout { get; private set; } = [];

        public int AskCount { get; private set; }

        public bool NotifiedStillRunning { get; private set; }

        public bool? CreateLock()
        {
            Calls.Add("CreateLock");
            return Created;
        }

        // 정해 둔 결과를 차례로 돌려주고, 다 쓰면 마지막 값을 반복합니다.
        public bool WaitForLock(TimeSpan timeout)
        {
            Calls.Add("WaitForLock");
            Waits.Add(timeout);
            var result = LockResults.Count == 0 ? false : LockResults[Math.Min(_next, LockResults.Count - 1)];
            _next++;
            return result;
        }

        public void PublishOwner() => Calls.Add("PublishOwner");

        public int ReadOwner() => Owner;

        public IReadOnlyList<int> OtherAppProcesses() => Others;

        public bool ShowWindowOf(IReadOnlyList<int> processIds)
        {
            WindowSearches.Add(processIds);
            return WindowSearches.Count - 1 >= WindowAt;
        }

        public bool AskToEndStuck(IReadOnlyList<int> processIds)
        {
            AskCount++;
            AskedAbout = processIds;
            return AskResult;
        }

        public void Terminate(int processId) => Terminated.Add(processId);

        public void NotifyStillRunning() => NotifiedStillRunning = true;
    }
}
