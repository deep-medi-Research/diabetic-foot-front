using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace DiabeticFoot.App.Camera;

/// <summary>카메라에 보내는 설정 값 묶음입니다.</summary>
/// <param name="Auto">자동 맞춤이면 true, 직접 지정이면 false입니다.</param>
/// <param name="ExposureStep">노출 단계입니다. 직접 지정일 때만 씁니다.</param>
/// <param name="Gain">게인입니다.</param>
/// <param name="WhiteBalance">색온도(K)입니다.</param>
public sealed record CameraSettings(bool Auto, int ExposureStep, int Gain, int WhiteBalance);

/// <summary>
/// 화면에서 고치는 카메라 설정입니다. 고친 값은 화면 최상위에서 들고 있다가 '적용'을 눌러야 카메라에 보냅니다.
/// 카메라에 적용된 값(<see cref="Applied"/>)과 다른 항목은 화면이 주의 색으로 보이고, 하나라도 다르면 '적용' 버튼이 나옵니다.
/// </summary>
/// <remarks>
/// 고치지 않은 동안에는 카메라에 적용된 값이 바뀌면 따라가고, 고친 값이 있으면 그대로 둡니다.
/// 카메라에 실제로 보내는 일은 생성자에 넘긴 동작이 합니다(촬영 ViewModel이 맡습니다).
/// </remarks>
public sealed class CameraSettingsDraftViewModel : ObservableObject
{
    private readonly Action<CameraSettings> _apply;
    private CameraSettings _applied;
    private CameraSettings _draft;

    /// <param name="applied">처음 카메라에 적용된 값</param>
    /// <param name="apply">'적용'을 누르면 부르는 동작. 값을 카메라로 보냅니다</param>
    public CameraSettingsDraftViewModel(CameraSettings applied, Action<CameraSettings> apply)
    {
        _applied = applied;
        _draft = applied;
        _apply = apply;
        ApplyCommand = new RelayCommand(() => _apply(_draft), () => IsDirty);
    }

    /// <summary>고친 값을 카메라에 적용합니다. 카메라 값과 다를 때만 실행됩니다.</summary>
    public IRelayCommand ApplyCommand { get; }

    /// <summary>카메라에 적용된 값입니다. 촬영 ViewModel이 카메라에서 다시 읽은 값으로 바꿉니다.</summary>
    public CameraSettings Applied
    {
        get => _applied;
        set => Set(value, IsDirty ? _draft : value);
    }

    public bool Auto
    {
        get => _draft.Auto;
        set => Set(_applied, _draft with { Auto = value });
    }

    public int ExposureStep
    {
        get => _draft.ExposureStep;
        set => Set(_applied, _draft with { ExposureStep = value });
    }

    public int Gain
    {
        get => _draft.Gain;
        set => Set(_applied, _draft with { Gain = value });
    }

    public int WhiteBalance
    {
        get => _draft.WhiteBalance;
        set => Set(_applied, _draft with { WhiteBalance = value });
    }

    /// <summary>고친 항목이 카메라 값과 다르면 true입니다. 화면이 그 값을 주의 색으로 보입니다.</summary>
    public bool AutoChanged => _draft.Auto != _applied.Auto;

    public bool ExposureStepChanged => _draft.ExposureStep != _applied.ExposureStep;

    public bool GainChanged => _draft.Gain != _applied.Gain;

    public bool WhiteBalanceChanged => _draft.WhiteBalance != _applied.WhiteBalance;

    /// <summary>하나라도 카메라 값과 다르면 true입니다. '적용' 버튼을 보입니다.</summary>
    public bool IsDirty => _draft != _applied;

    // 적용된 값과 고친 값을 함께 바꾸고, 값이 달라진 속성만 알립니다.
    private void Set(CameraSettings applied, CameraSettings draft)
    {
        var (oldApplied, oldDraft) = (_applied, _draft);
        if (oldApplied == applied && oldDraft == draft) return;
        (_applied, _draft) = (applied, draft);
        if (oldApplied != applied) OnPropertyChanged(nameof(Applied));
        if (oldDraft.Auto != draft.Auto) OnPropertyChanged(nameof(Auto));
        if (oldDraft.ExposureStep != draft.ExposureStep) OnPropertyChanged(nameof(ExposureStep));
        if (oldDraft.Gain != draft.Gain) OnPropertyChanged(nameof(Gain));
        if (oldDraft.WhiteBalance != draft.WhiteBalance) OnPropertyChanged(nameof(WhiteBalance));
        if ((oldDraft.Auto != oldApplied.Auto) != AutoChanged) OnPropertyChanged(nameof(AutoChanged));
        if ((oldDraft.ExposureStep != oldApplied.ExposureStep) != ExposureStepChanged) OnPropertyChanged(nameof(ExposureStepChanged));
        if ((oldDraft.Gain != oldApplied.Gain) != GainChanged) OnPropertyChanged(nameof(GainChanged));
        if ((oldDraft.WhiteBalance != oldApplied.WhiteBalance) != WhiteBalanceChanged) OnPropertyChanged(nameof(WhiteBalanceChanged));
        if ((oldDraft != oldApplied) != IsDirty)
        {
            OnPropertyChanged(nameof(IsDirty));
            ApplyCommand.NotifyCanExecuteChanged();
        }
    }
}
