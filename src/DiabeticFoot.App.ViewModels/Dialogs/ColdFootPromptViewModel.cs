using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace DiabeticFoot.App.Dialogs;

/// <summary>
/// 촬영 버튼을 눌렀는데 미리보기 열화상으로 본 발이 차가울 때 이대로 찍을지 묻는 안내입니다.
/// 기본 선택은 기다렸다 찍기(취소)입니다. Enter와 Esc 모두 취소이고, 진행은 버튼을 직접 눌러야 합니다.
/// </summary>
public sealed partial class ColdFootPromptViewModel : ObservableObject
{
    private readonly Action _startWithColdFoot;
    private readonly Action _cancel;

    /// <param name="startWithColdFoot">이대로 녹화를 시작합니다. 매니페스트 foot_warmth에 사용자 확인을 남깁니다</param>
    /// <param name="cancel">안내를 닫습니다. 녹화하지 않습니다</param>
    public ColdFootPromptViewModel(Action startWithColdFoot, Action cancel)
    {
        _startWithColdFoot = startWithColdFoot;
        _cancel = cancel;
    }

    /// <summary>안내의 제목입니다. 닫혀 있으면 빈 문자열입니다.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsOpen))]
    private string _title = "";

    /// <summary>발 상태 문장입니다. 예: "왼발이 차갑습니다 (발과 바닥의 열화상 차이 38, 기준 60)".</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Details))]
    private string _footWarmthText = "";

    public bool IsOpen => Title.Length > 0;

    /// <summary>안내를 엽니다. 발 상태 문장을 먼저 정하므로 열리는 순간부터 설명이 맞습니다.</summary>
    public void Open(string title, string footWarmthText)
    {
        FootWarmthText = footWarmthText;
        Title = title;
    }

    public string Heading => "발이 차가움";

    public DialogTone Tone => DialogTone.Warning;

    /// <summary>제목과 같은 첫 문장은 빼고 괄호 안의 값만 앞에 붙인 설명입니다.</summary>
    public string Details
    {
        get
        {
            var text = FootWarmthText;
            var open = text.IndexOf('(');
            var close = text.LastIndexOf(')');
            var values = open >= 0 && close > open ? text[(open + 1)..close] : "";
            return (values.Length > 0 ? values + ". " : "") +
                   "발이 차가우면 발바닥 혈류(맥박 신호)가 약해 재측정이 될 수 있습니다. 양말을 신고 5~10분 기다린 뒤 측정하기를 권합니다.";
        }
    }

    public string ProceedLabel => "이대로 녹화";

    public string CancelLabel => "기다렸다 찍기";

    [RelayCommand]
    private void Proceed()
    {
        Title = "";
        _startWithColdFoot();
    }

    /// <summary>Enter, Esc, 기다렸다 찍기 버튼입니다. 이미 닫혀 있으면 아무것도 하지 않습니다.</summary>
    [RelayCommand]
    private void Cancel()
    {
        if (!IsOpen) return;
        Title = "";
        _cancel();
    }
}
