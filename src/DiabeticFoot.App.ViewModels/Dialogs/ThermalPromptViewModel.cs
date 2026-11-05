using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace DiabeticFoot.App.Dialogs;

/// <summary>열화상 없이 찍을지 묻는 안내가 열린 이유입니다.</summary>
public enum ThermalPromptKind
{
    /// <summary>안내가 닫혀 있습니다.</summary>
    None,

    /// <summary>열화상 카메라를 열고 첫 프레임을 기다리는 중입니다(셔터 교정).</summary>
    Preparing,

    /// <summary>열화상 카메라가 인식되지 않았거나 끊겼습니다.</summary>
    NotReceiving,
}

/// <summary>
/// 촬영 버튼을 눌렀는데 열화상이 수신 중이 아닐 때 열화상 없이 찍을지 묻는 안내입니다.
/// 기본 선택은 취소입니다. Enter와 Esc 모두 취소이고, 진행은 버튼을 직접 눌러야 합니다.
/// 촬영 쪽이 <see cref="Kind"/>를 정해 열고, 버튼을 누르면 안내를 닫은 뒤 넘겨 받은 동작을 부릅니다.
/// </summary>
public sealed partial class ThermalPromptViewModel : ObservableObject
{
    private readonly Action _startWithoutThermal;
    private readonly Action _cancel;

    /// <param name="startWithoutThermal">열화상 없이 촬영을 시작합니다. 매니페스트에 사용자 확인을 남깁니다</param>
    /// <param name="cancel">안내를 닫습니다. 녹화하지 않습니다</param>
    public ThermalPromptViewModel(Action startWithoutThermal, Action cancel)
    {
        _startWithoutThermal = startWithoutThermal;
        _cancel = cancel;
    }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsOpen), nameof(Title), nameof(Message), nameof(Tone))]
    private ThermalPromptKind _kind;

    public bool IsOpen => Kind != ThermalPromptKind.None;

    public string Title => Kind == ThermalPromptKind.Preparing ? "열화상 카메라 준비 중" : "열화상 카메라 인식 안 됨";

    public string Message => Kind == ThermalPromptKind.Preparing
        ? "열화상 카메라가 교정을 마치는 중입니다. 열화상 없이 촬영할까요?"
        : "열화상 카메라가 인식되지 않았습니다. 열화상 없이 촬영할까요?";

    public string Note => "커프 단계 추정은 RGB 영상만 쓰므로 열화상 없이 촬영해도 분석할 수 있습니다.";

    public string ProceedLabel => "열화상 없이 촬영";

    public string CancelLabel => "취소";

    public DialogTone Tone => Kind == ThermalPromptKind.Preparing ? DialogTone.Warning : DialogTone.Error;

    [RelayCommand]
    private void Proceed()
    {
        Kind = ThermalPromptKind.None;
        _startWithoutThermal();
    }

    /// <summary>Enter, Esc, 취소 버튼입니다. 이미 닫혀 있으면 아무것도 하지 않습니다.</summary>
    [RelayCommand]
    private void Cancel()
    {
        if (!IsOpen) return;
        Kind = ThermalPromptKind.None;
        _cancel();
    }
}
