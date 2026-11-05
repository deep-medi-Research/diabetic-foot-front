using DiabeticFoot.App.Camera;

namespace DiabeticFoot.App.Tests.Camera;

public sealed class CameraSettingsDraftViewModelTests
{
    private static readonly CameraSettings Camera = new(false, -9, 4, 4600);

    private readonly List<CameraSettings> _sent = [];
    private readonly CameraSettingsDraftViewModel _draft;

    public CameraSettingsDraftViewModelTests()
    {
        _draft = new CameraSettingsDraftViewModel(Camera, _sent.Add);
    }

    // 처음에는 카메라 값 그대로이고, 고치기 전에는 '적용'을 쓸 수 없습니다.
    [Fact]
    public void StartsEqualToCamera()
    {
        Assert.False(_draft.IsDirty);
        Assert.False(_draft.ApplyCommand.CanExecute(null));
        Assert.Equal(-9, _draft.ExposureStep);
        Assert.False(_draft.Auto);
    }

    // 고친 값은 '적용'을 눌러야 보냅니다. 고친 항목만 카메라 값과 다르다고 알립니다.
    [Fact]
    public void SendsOnlyWhenApplied()
    {
        _draft.Gain = 10;
        _draft.WhiteBalance = 5000;

        Assert.Empty(_sent);
        Assert.True(_draft.IsDirty);
        Assert.True(_draft.GainChanged);
        Assert.True(_draft.WhiteBalanceChanged);
        Assert.False(_draft.ExposureStepChanged);
        Assert.True(_draft.ApplyCommand.CanExecute(null));

        _draft.ApplyCommand.Execute(null);

        Assert.Equal([new CameraSettings(false, -9, 10, 5000)], _sent);
    }

    // 카메라가 고친 값으로 바뀌면(적용 결과를 다시 읽음) 더 이상 고친 값이 아닙니다.
    [Fact]
    public void ApplyingMakesDraftClean()
    {
        _draft.Gain = 10;

        _draft.Applied = new CameraSettings(false, -9, 10, 4600);

        Assert.False(_draft.IsDirty);
        Assert.False(_draft.GainChanged);
        Assert.False(_draft.ApplyCommand.CanExecute(null));
    }

    // 고치지 않은 동안에는 카메라 값을 따라가고, 고친 값이 있으면 그대로 둡니다.
    [Fact]
    public void FollowsCameraOnlyWhileClean()
    {
        _draft.Applied = new CameraSettings(true, -9, 4, 4600);
        Assert.True(_draft.Auto);
        Assert.False(_draft.IsDirty);

        _draft.Gain = 99;
        _draft.Applied = new CameraSettings(true, -6, 4, 4600);

        Assert.Equal(99, _draft.Gain);
        Assert.Equal(-9, _draft.ExposureStep);
        Assert.True(_draft.ExposureStepChanged);
        Assert.True(_draft.IsDirty);
    }

    // 알림은 값이 바뀐 속성만 나갑니다.
    [Fact]
    public void NotifiesOnlyChangedProperties()
    {
        var changed = new List<string?>();
        _draft.PropertyChanged += (_, e) => changed.Add(e.PropertyName);

        _draft.Gain = 10;
        Assert.Equal(["Gain", "GainChanged", "IsDirty"], changed);

        changed.Clear();
        _draft.Gain = 11;
        Assert.Equal(["Gain"], changed);

        changed.Clear();
        _draft.Gain = 4;
        Assert.Equal(["Gain", "GainChanged", "IsDirty"], changed);

        changed.Clear();
        _draft.Gain = 4;
        Assert.Empty(changed);
    }

    // 적용 명령의 사용 가능 여부가 바뀌면 알립니다(버튼이 켜지고 꺼집니다).
    [Fact]
    public void RaisesCanExecuteChanged()
    {
        var raised = 0;
        _draft.ApplyCommand.CanExecuteChanged += (_, _) => raised++;

        _draft.Auto = true;
        _draft.ExposureStep = -5;
        _draft.Auto = false;
        _draft.ExposureStep = -9;

        Assert.Equal(2, raised);
    }
}
