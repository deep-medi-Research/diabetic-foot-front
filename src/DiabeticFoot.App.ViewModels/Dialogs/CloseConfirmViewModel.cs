using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace DiabeticFoot.App.Dialogs;

/// <summary>
/// 녹화를 준비하거나 쓰는 중에 창을 닫을 때 묻습니다.
/// 닫기를 고르면 녹화를 멈추고 지금까지 찍은 것을 저장한 뒤 닫습니다. Esc는 계속 촬영입니다.
/// </summary>
public sealed partial class CloseConfirmViewModel : ObservableObject
{
    private readonly Action _shutdown;
    private bool _shotInProgress;

    /// <param name="shutdown">종료를 시작합니다. 녹화를 멈추고 저장한 뒤 카메라를 되돌립니다</param>
    public CloseConfirmViewModel(Action shutdown)
    {
        _shutdown = shutdown;
    }

    [ObservableProperty]
    private bool _isOpen;

    public string Title => "촬영 중입니다";

    public string Message => "닫으면 녹화를 멈추고 지금까지 찍은 영상과 파일을 저장한 뒤 닫습니다. 목표 장 수보다 짧은 촬영으로 남습니다.";

    public string KeepLabel => "계속 촬영";

    public string StopLabel => "멈추고 닫기";

    /// <summary>
    /// 안정화나 녹화가 이어지는 중인지입니다. 확인을 띄운 사이 녹화가 끝나면 물을 이유가 없으므로 안내를 닫습니다.
    /// </summary>
    public bool ShotInProgress
    {
        get => _shotInProgress;
        set
        {
            _shotInProgress = value;
            if (!value) IsOpen = false;
        }
    }

    /// <summary>안내를 엽니다.</summary>
    public void Open() => IsOpen = true;

    /// <summary>Esc와 계속 촬영 버튼입니다.</summary>
    [RelayCommand]
    private void Keep() => IsOpen = false;

    [RelayCommand]
    private void StopAndClose()
    {
        IsOpen = false;
        _shutdown();
    }
}
