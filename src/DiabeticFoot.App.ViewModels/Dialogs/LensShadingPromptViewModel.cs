using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace DiabeticFoot.App.Dialogs;

/// <summary>렌즈 셰이딩 보정 안내가 열린 이유입니다.</summary>
public enum LensShadingPromptKind
{
    /// <summary>안내가 닫혀 있습니다.</summary>
    None,

    /// <summary>보정 파일이 없어 보정을 권합니다. 열화상을 처음 받을 때 한 번 뜹니다.</summary>
    Missing,

    /// <summary>보정 파일을 불러오지 못해 다시 보정을 권합니다.</summary>
    Unreadable,

    /// <summary>렌즈 보정 버튼을 눌러 렌즈를 가렸는지 묻습니다.</summary>
    Confirm,
}

/// <summary>
/// 열화상 렌즈 셰이딩 보정 안내입니다. 보정 파일이 없을 때, 불러오지 못했을 때, 렌즈 보정 버튼을 눌렀을 때 열립니다.
/// 렌즈를 가리지 않고 보정하면 영상이 보정 전보다 고르지 않게 되므로 기본 선택은 보정하지 않는 쪽입니다.
/// 판을 렌즈와 정면으로 대면 데워진 렌즈가 판에 비쳐 보정값에 섞이므로(실기 2026-10-27) 기울여 대라고 안내합니다.
/// Enter와 Esc 모두 보정하지 않고 닫으며, 보정은 버튼을 직접 눌러야 합니다.
/// </summary>
public sealed partial class LensShadingPromptViewModel : ObservableObject
{
    private readonly Action _correct;
    private readonly Action _cancel;

    /// <param name="correct">렌즈를 가렸다고 골랐습니다. 보정과 저장을 요청합니다</param>
    /// <param name="cancel">안내를 닫습니다. 보정하지 않습니다</param>
    public LensShadingPromptViewModel(Action correct, Action cancel)
    {
        _correct = correct;
        _cancel = cancel;
    }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsOpen), nameof(Recommended), nameof(Heading), nameof(Message), nameof(CancelLabel))]
    private LensShadingPromptKind _kind;

    public bool IsOpen => Kind != LensShadingPromptKind.None;

    /// <summary>앱이 먼저 띄운 안내(보정 파일 없음, 불러오지 못함)는 나중에 하도록 고를 수 있습니다.</summary>
    public bool Recommended => Kind is LensShadingPromptKind.Missing or LensShadingPromptKind.Unreadable;

    public DialogTone Tone => DialogTone.Warning;

    public string Heading => Kind switch
    {
        LensShadingPromptKind.Missing => "렌즈 보정값 없음",
        LensShadingPromptKind.Unreadable => "렌즈 보정값을 불러오지 못함",
        _ => "렌즈 보정",
    };

    public string Message => Kind switch
    {
        LensShadingPromptKind.Missing =>
            "이 열화상 카메라의 렌즈 보정값이 없습니다. 열화상 카메라가 데워진 뒤 렌즈 앞을 판으로 기울여 가리고 보정하십시오. 나중에 하려면 열화상 칸의 렌즈 보정을 누릅니다.",
        LensShadingPromptKind.Unreadable =>
            "저장된 렌즈 보정값을 불러오지 못해 보정 없이 받고 있습니다. 열화상 카메라가 데워진 뒤 렌즈 앞을 판으로 기울여 가리고 다시 보정하십시오. 나중에 하려면 열화상 칸의 렌즈 보정을 누릅니다.",
        _ => "열화상 렌즈 앞을 판으로 기울여 가렸습니까?",
    };

    public string Instructions =>
        "판: 나무나 두꺼운 종이처럼 반사가 없고 열이 잘 퍼지지 않는 실온의 평평한 판을 씁니다.\n" +
        "대는 방법: 판을 렌즈와 정면으로 마주 보지 않게 30도 이상 기울여 화면 전체를 덮습니다. " +
        "렌즈가 보는 면에는 손이나 카메라가 닿지 않게 합니다.\n" +
        "시점: 앱에서 열화상이 보이기 시작하고 40분 이상 지나 카메라가 데워진 뒤에 보정합니다.\n\n" +
        "판을 정면으로 대면 데워진 렌즈가 판에 비치고, 가리지 않으면 그때 보이던 장면이 보정값에 섞여 " +
        "영상이 오히려 고르지 않게 됩니다. 보정값은 저장되어 다음에 앱을 켤 때 그대로 적용됩니다. " +
        "보정하는 약 2초 동안 열화상이 멈춥니다.";

    public string ProceedLabel => "기울여 가렸습니다, 보정";

    public string CancelLabel => Recommended ? "나중에" : "취소";

    [RelayCommand]
    private void Proceed()
    {
        Kind = LensShadingPromptKind.None;
        _correct();
    }

    /// <summary>Enter, Esc, 나중에(취소) 버튼입니다. 이미 닫혀 있으면 아무것도 하지 않습니다.</summary>
    [RelayCommand]
    private void Cancel()
    {
        if (!IsOpen) return;
        Kind = LensShadingPromptKind.None;
        _cancel();
    }
}
