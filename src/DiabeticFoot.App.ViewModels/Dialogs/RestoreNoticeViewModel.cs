using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace DiabeticFoot.App.Dialogs;

/// <summary>
/// 앱을 닫을 때 카메라를 실행 전 상태로 되돌리는 동안과, 되돌린 값이 맞지 않을 때의 안내입니다.
/// 같은 카메라를 다른 사람도 쓰므로 맞지 않은 항목은 닫기 전에 사람이 확인하게 합니다.
/// 종료 상태는 촬영 쪽이 <see cref="Update"/>로 알립니다.
/// </summary>
public sealed partial class RestoreNoticeViewModel : ObservableObject
{
    private readonly Action _quit;

    /// <param name="quit">앱을 끝냅니다</param>
    public RestoreNoticeViewModel(Action quit)
    {
        _quit = quit;
    }

    /// <summary>종료를 시작해 카메라를 되돌리는 중입니다.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsOpen))]
    private bool _shuttingDown;

    /// <summary>되돌리기가 끝나 창을 닫아도 됩니다.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsOpen))]
    private bool _closeReady;

    /// <summary>닫기 전에 녹화를 멈추고 촬영을 저장하는 중입니다.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Title), nameof(Message))]
    private bool _savingBeforeClose;

    /// <summary>되돌리지 못한 항목을 사람이 읽을 문장으로 적은 것입니다. 모두 맞으면 빈 문자열입니다.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Failed), nameof(IsOpen), nameof(Title), nameof(Message), nameof(Tone))]
    private string _restoreReport = "";

    /// <summary>되돌린 값이 맞지 않는 항목이 있습니다.</summary>
    public bool Failed => RestoreReport.Length > 0;

    /// <summary>되돌리는 동안과, 끝났지만 맞지 않은 항목이 있어 사람이 확인해야 할 때 보입니다.</summary>
    public bool IsOpen => ShuttingDown || (CloseReady && Failed);

    public DialogTone Tone => Failed ? DialogTone.Error : DialogTone.Warning;

    public string Title => Failed ? "카메라 설정 복원 불일치" : SavingBeforeClose ? "촬영 저장 중" : "카메라 설정 복원 중";

    public string Message => Failed
        ? "카메라 설정 일부가 실행 전 값으로 돌아가지 않았습니다. 다음 사용자에게 알리거나 카메라 설정을 확인하십시오."
        : SavingBeforeClose
            ? "녹화를 멈추고 지금까지 찍은 촬영을 저장하는 중입니다. 저장이 끝나면 카메라 설정을 되돌리고 닫습니다."
            : "카메라 설정을 실행 전 값으로 되돌리는 중입니다.";

    public string CloseLabel => "확인하고 닫기";

    /// <summary>종료 상태를 한 번에 알립니다.</summary>
    public void Update(bool shuttingDown, bool closeReady, bool savingBeforeClose, string restoreReport)
    {
        ShuttingDown = shuttingDown;
        CloseReady = closeReady;
        SavingBeforeClose = savingBeforeClose;
        RestoreReport = restoreReport;
    }

    /// <summary>종료 흐름이 끝났습니다. 문제가 없으면 바로 끝내고, 있으면 안내를 띄우고 사람이 닫기를 누를 때 끝냅니다.</summary>
    public void OnShutdownFinished()
    {
        if (!Failed) _quit();
    }

    [RelayCommand]
    private void Close() => _quit();
}
