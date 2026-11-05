namespace DiabeticFoot.App.Startup;

/// <summary><see cref="SingleInstance.ClaimDevices"/>의 결과입니다.</summary>
public enum InstanceClaim
{
    /// <summary>다른 실행이 없거나, 멈춘 실행을 끝내고 장치를 넘겨받았습니다. 이어서 시작합니다.</summary>
    Alone,

    /// <summary>창이 있는 다른 실행을 앞으로 가져왔습니다. 이 실행은 끝냅니다.</summary>
    ShownOther,

    /// <summary>멈춘 실행을 끝내지 않기로 했거나 끝나지 않았습니다. 이 실행은 끝냅니다.</summary>
    Stopped,
}

/// <summary>
/// 단일 실행 판단이 쓰는 운영체제 기능입니다. Windows 구현은 App 프로젝트에 있고, 시험은 장면을 정해 둔 가짜로 돌립니다.
/// 같은 장치를 가진 앱끼리 하나의 잠금(이름 붙은 뮤텍스)을 두고 다툽니다.
/// </summary>
public interface IInstanceHost
{
    /// <summary>
    /// 장치 잠금을 만듭니다. 이 실행이 처음 만들어 소유하면 true, 이미 다른 실행이 만든 것이면 false입니다.
    /// 잠금을 만들지 못하면 null이고, 이때는 막지 않고 시작합니다.
    /// </summary>
    bool? CreateLock();

    /// <summary>잠금을 넘겨받을 때까지 최대 <paramref name="timeout"/> 기다립니다. 넘겨받았으면 true입니다. 앞 실행이 비정상 종료해 버려진 잠금도 넘겨받습니다.</summary>
    bool WaitForLock(TimeSpan timeout);

    /// <summary>장치를 얻은 이 실행이 자기 프로세스 번호를 남깁니다.</summary>
    void PublishOwner();

    /// <summary>장치를 가진 실행의 프로세스 번호입니다. 남긴 값이 없으면(이 기능 전 판의 실행) 0입니다.</summary>
    int ReadOwner();

    /// <summary>이 실행 말고 앱 실행 파일 이름을 가진 프로세스들의 번호입니다.</summary>
    IReadOnlyList<int> OtherAppProcesses();

    /// <summary>주어진 프로세스의 보이는 최상위 창을 앞으로 가져옵니다(최소화했으면 복원). 창이 없으면 false입니다.</summary>
    bool ShowWindowOf(IReadOnlyList<int> processIds);

    /// <summary>창을 닫았지만 멈춘 실행이 남아 있다고 알리고 끝낼지 묻습니다. 끝내기로 하면 true입니다.</summary>
    bool AskToEndStuck(IReadOnlyList<int> processIds);

    /// <summary>프로세스를 강제로 끝냅니다.</summary>
    void Terminate(int processId);

    /// <summary>남은 앱이 아직 끝나지 않았다고 알립니다.</summary>
    void NotifyStillRunning();
}

/// <summary>
/// 장치(카메라, 열화상, 직렬 포트)를 여는 실행이 하나만 돌게 합니다.
/// </summary>
/// <remarks>
/// 두 실행이 같은 장치를 열면 한쪽이 실패하거나 드라이버 호출에서 서로 기다리다 멈추는 일이 있었습니다(2026-10-27 노트북,
/// 6초 간격으로 켠 v0.1.8 두 개가 종료 중 멈춤). 개발 빌드와 릴리즈도 같은 이름을 써서 함께 돌지 않습니다.
/// 결과 창만 띄우는 실행(--result)과 가짜 장치 실행(--fake)은 장치를 열지 않으므로 부르지 않습니다.
/// </remarks>
public static class SingleInstance
{
    /// <summary>멈춘 실행을 끝낸 뒤 장치를 넘겨받기를 기다리는 시간입니다.</summary>
    public static readonly TimeSpan TakeOverWait = TimeSpan.FromSeconds(15);

    /// <summary>앞 실행이 방금 끝나는 중일 수 있어 잠깐 기다려 보는 시간입니다.</summary>
    public static readonly TimeSpan QuickWait = TimeSpan.FromMilliseconds(1500);

    /// <summary>바로가기를 두 번 누른 경우처럼 앞 실행이 아직 창을 띄우기 전일 수 있어, 보이는 창을 더 찾는 시간입니다.</summary>
    public static readonly TimeSpan WindowWait = TimeSpan.FromSeconds(10);

    private static readonly TimeSpan WindowStep = TimeSpan.FromMilliseconds(500);

    /// <summary>
    /// 장치를 쓸 권리를 얻습니다. 이미 다른 실행이 있으면 사람에게 묻거나 그 창을 보입니다.
    /// 다른 실행에 보이는 창이 있으면 그 창을 앞으로 가져옵니다. 보이는 창이 없으면(창을 닫았는데 종료 중에 멈춘 실행)
    /// 끝낼지 묻고, 예를 고르면 그 프로세스를 끝낸 뒤 최대 <see cref="TakeOverWait"/> 기다려 장치를 넘겨받습니다.
    /// </summary>
    /// <returns>이 실행을 이어서 시작할지(<see cref="InstanceClaim.Alone"/>)</returns>
    /// <remarks>얻은 권리는 프로세스가 끝날 때 운영체제가 풉니다. 처음 만든 실행이 소유하며, 어떻게 끝나든(강제 종료 포함) 다음 실행이 넘겨받습니다.</remarks>
    public static InstanceClaim ClaimDevices(IInstanceHost host)
    {
        var created = host.CreateLock();
        if (created is null) return InstanceClaim.Alone;
        if (created.Value) return Claimed(host);
        if (host.WaitForLock(QuickWait)) return Claimed(host);

        // 이 시간 동안 창이 없어야 종료 중에 멈춘 실행으로 봅니다.
        IReadOnlyList<int> others = [];
        var shown = false;
        for (var waited = TimeSpan.Zero; waited <= WindowWait; waited += WindowStep)
        {
            others = HolderCandidates(host);
            shown = host.ShowWindowOf(others);
            if (shown || others.Count == 0) break;
            if (host.WaitForLock(WindowStep)) return Claimed(host);
        }
        if (shown) return InstanceClaim.ShownOther;

        if (!host.AskToEndStuck(others)) return InstanceClaim.Stopped;
        foreach (var id in others) host.Terminate(id);
        // 드라이버 호출에 걸린 프로세스는 끝내도 1~2분 남은 적이 있어(2026-10-23 노트북) 정한 시간만 기다립니다.
        if (host.WaitForLock(TakeOverWait)) return Claimed(host);
        host.NotifyStillRunning();
        return InstanceClaim.Stopped;
    }

    /// <summary>
    /// 장치를 가진 실행이 알려져 있고 아직 살아 있으면 그것만, 아니면 이름이 같은 다른 실행 모두입니다.
    /// 결과 창만 띄운 실행(--result)도 앱 이름이 같아 이름으로는 가리지 못하므로 장치를 가진 실행이 남긴 번호를 먼저 봅니다.
    /// </summary>
    internal static IReadOnlyList<int> HolderCandidates(IInstanceHost host)
    {
        var others = host.OtherAppProcesses();
        var owner = host.ReadOwner();
        return others.Contains(owner) ? [owner] : others;
    }

    private static InstanceClaim Claimed(IInstanceHost host)
    {
        host.PublishOwner();
        return InstanceClaim.Alone;
    }
}
