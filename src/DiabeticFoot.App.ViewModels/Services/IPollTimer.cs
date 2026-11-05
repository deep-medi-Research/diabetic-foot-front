namespace DiabeticFoot.App.Services;

/// <summary>
/// 정해진 간격으로 화면 스레드에서 동작을 부르는 시계입니다. 상태를 주기적으로 읽는 ViewModel이 씁니다.
/// WPF는 DispatcherTimer로 구현하고, 시험은 직접 한 번씩 울립니다.
/// </summary>
public interface IPollTimer
{
    bool IsRunning { get; }

    /// <summary>이미 돌고 있으면 간격과 동작을 새로 정합니다.</summary>
    void Start(TimeSpan interval, Action tick);

    void Stop();
}
