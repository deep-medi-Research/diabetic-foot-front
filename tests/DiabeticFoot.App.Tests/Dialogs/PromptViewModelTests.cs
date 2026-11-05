using DiabeticFoot.App.Dialogs;

namespace DiabeticFoot.App.Tests.Dialogs;

public sealed class PromptViewModelTests
{
    // 열화상 안내는 이유에 따라 제목, 질문, 점 색이 달라지고 닫혀 있으면 보이지 않습니다.
    [Fact]
    public void ThermalPromptTextFollowsKind()
    {
        var prompt = new ThermalPromptViewModel(() => { }, () => { });
        Assert.False(prompt.IsOpen);

        prompt.Kind = ThermalPromptKind.Preparing;
        Assert.True(prompt.IsOpen);
        Assert.Equal("열화상 카메라 준비 중", prompt.Title);
        Assert.Equal("열화상 카메라가 교정을 마치는 중입니다. 열화상 없이 촬영할까요?", prompt.Message);
        Assert.Equal(DialogTone.Warning, prompt.Tone);

        prompt.Kind = ThermalPromptKind.NotReceiving;
        Assert.Equal("열화상 카메라 인식 안 됨", prompt.Title);
        Assert.Equal("열화상 카메라가 인식되지 않았습니다. 열화상 없이 촬영할까요?", prompt.Message);
        Assert.Equal(DialogTone.Error, prompt.Tone);
        Assert.Equal("커프 단계 추정은 RGB 영상만 쓰므로 열화상 없이 촬영해도 분석할 수 있습니다.", prompt.Note);
    }

    // 진행 버튼은 안내를 닫고 열화상 없이 촬영하게 하고, Enter와 Esc가 쓰는 취소는 안내만 닫습니다.
    [Fact]
    public void ThermalPromptButtonsCloseAndCallBack()
    {
        var calls = new List<string>();
        var prompt = new ThermalPromptViewModel(() => calls.Add("proceed"), () => calls.Add("cancel"));

        prompt.Kind = ThermalPromptKind.NotReceiving;
        prompt.ProceedCommand.Execute(null);
        Assert.False(prompt.IsOpen);

        prompt.Kind = ThermalPromptKind.Preparing;
        prompt.CancelCommand.Execute(null);
        Assert.False(prompt.IsOpen);

        // 이미 닫힌 뒤의 취소(Esc로 닫은 뒤 닫힘 알림)는 촬영 쪽을 다시 부르지 않습니다.
        prompt.CancelCommand.Execute(null);

        Assert.Equal(["proceed", "cancel"], calls);
    }

    // 알림은 안내 종류에 달린 속성만 나갑니다.
    [Fact]
    public void ThermalPromptNotifiesDependentProperties()
    {
        var prompt = new ThermalPromptViewModel(() => { }, () => { });
        var changed = new List<string?>();
        prompt.PropertyChanged += (_, e) => changed.Add(e.PropertyName);

        prompt.Kind = ThermalPromptKind.Preparing;

        Assert.Equal(["Kind", "IsOpen", "Title", "Message", "Tone"], changed);
    }

    // 발이 찬 안내는 제목은 촬영 쪽이 정하고, 설명 앞에 발 상태 문장의 괄호 안 값만 붙입니다.
    [Theory]
    [InlineData("왼발이 차갑습니다 (발과 바닥의 열화상 차이 38, 기준 60)", "발과 바닥의 열화상 차이 38, 기준 60. ")]
    [InlineData("왼발이 차갑습니다", "")]
    [InlineData("", "")]
    [InlineData("왼발이 차갑습니다 (닫는 괄호 없음", "")]
    public void ColdFootPromptDetailsKeepOnlyValuesInParentheses(string warmthText, string values)
    {
        var prompt = new ColdFootPromptViewModel(() => { }, () => { });

        prompt.Open("발이 차갑습니다. 이대로 녹화할까요?", warmthText);

        Assert.True(prompt.IsOpen);
        Assert.Equal(
            values + "발이 차가우면 발바닥 혈류(맥박 신호)가 약해 재측정이 될 수 있습니다. 양말을 신고 5~10분 기다린 뒤 측정하기를 권합니다.",
            prompt.Details);
    }

    [Fact]
    public void ColdFootPromptButtonsCloseAndCallBack()
    {
        var calls = new List<string>();
        var prompt = new ColdFootPromptViewModel(() => calls.Add("proceed"), () => calls.Add("cancel"));

        prompt.Open("제목", "문장");
        prompt.ProceedCommand.Execute(null);
        Assert.False(prompt.IsOpen);

        prompt.Open("제목", "문장");
        prompt.CancelCommand.Execute(null);
        Assert.False(prompt.IsOpen);
        prompt.CancelCommand.Execute(null);

        Assert.Equal(["proceed", "cancel"], calls);
        Assert.Equal("이대로 녹화", prompt.ProceedLabel);
        Assert.Equal("기다렸다 찍기", prompt.CancelLabel);
    }

    // 렌즈 보정 안내는 앱이 먼저 띄운 두 가지(파일 없음, 불러오지 못함)에서만 나중에 하도록 고를 수 있습니다.
    [Theory]
    [InlineData(LensShadingPromptKind.Missing, "렌즈 보정값 없음", "나중에", true)]
    [InlineData(LensShadingPromptKind.Unreadable, "렌즈 보정값을 불러오지 못함", "나중에", true)]
    [InlineData(LensShadingPromptKind.Confirm, "렌즈 보정", "취소", false)]
    public void LensShadingPromptTextFollowsKind(LensShadingPromptKind kind, string heading, string cancelLabel, bool recommended)
    {
        var prompt = new LensShadingPromptViewModel(() => { }, () => { }) { Kind = kind };

        Assert.True(prompt.IsOpen);
        Assert.Equal(heading, prompt.Heading);
        Assert.Equal(cancelLabel, prompt.CancelLabel);
        Assert.Equal(recommended, prompt.Recommended);
        Assert.Equal("기울여 가렸습니다, 보정", prompt.ProceedLabel);
        Assert.Contains("30도 이상 기울여", prompt.Instructions);
    }

    [Fact]
    public void LensShadingPromptMessages()
    {
        var prompt = new LensShadingPromptViewModel(() => { }, () => { });

        prompt.Kind = LensShadingPromptKind.Confirm;
        Assert.Equal("열화상 렌즈 앞을 판으로 기울여 가렸습니까?", prompt.Message);
        prompt.Kind = LensShadingPromptKind.Missing;
        Assert.StartsWith("이 열화상 카메라의 렌즈 보정값이 없습니다.", prompt.Message);
        prompt.Kind = LensShadingPromptKind.Unreadable;
        Assert.StartsWith("저장된 렌즈 보정값을 불러오지 못해", prompt.Message);
    }

    [Fact]
    public void LensShadingPromptButtonsCloseAndCallBack()
    {
        var calls = new List<string>();
        var prompt = new LensShadingPromptViewModel(() => calls.Add("correct"), () => calls.Add("cancel"));

        prompt.Kind = LensShadingPromptKind.Confirm;
        prompt.ProceedCommand.Execute(null);
        Assert.False(prompt.IsOpen);

        prompt.Kind = LensShadingPromptKind.Missing;
        prompt.CancelCommand.Execute(null);
        prompt.CancelCommand.Execute(null);

        Assert.Equal(["correct", "cancel"], calls);
    }

    // 닫기 확인에서 계속 촬영은 종료하지 않고, 멈추고 닫기는 종료를 시작합니다.
    [Fact]
    public void CloseConfirmKeepsOrStops()
    {
        var shutdowns = 0;
        var confirm = new CloseConfirmViewModel(() => shutdowns++);
        confirm.ShotInProgress = true;

        confirm.Open();
        Assert.True(confirm.IsOpen);
        confirm.KeepCommand.Execute(null);
        Assert.False(confirm.IsOpen);
        Assert.Equal(0, shutdowns);

        confirm.Open();
        confirm.StopAndCloseCommand.Execute(null);
        Assert.False(confirm.IsOpen);
        Assert.Equal(1, shutdowns);
    }

    // 확인을 띄운 사이 녹화가 끝나면 물을 이유가 없으므로 닫습니다.
    [Fact]
    public void CloseConfirmClosesWhenShotEnds()
    {
        var confirm = new CloseConfirmViewModel(() => { });
        confirm.ShotInProgress = true;
        confirm.Open();

        confirm.ShotInProgress = true;
        Assert.True(confirm.IsOpen);
        confirm.ShotInProgress = false;

        Assert.False(confirm.IsOpen);
    }

    // 복원 안내는 되돌리는 동안과, 끝났지만 맞지 않은 항목이 있을 때만 보입니다.
    [Theory]
    [InlineData(false, false, "", false)]
    [InlineData(true, false, "", true)]
    [InlineData(false, true, "", false)]
    [InlineData(true, true, "", true)]
    [InlineData(false, true, "색온도: 4600 K를 자동으로 되돌리지 못함", true)]
    [InlineData(false, false, "색온도: 4600 K를 자동으로 되돌리지 못함", false)]
    public void RestoreNoticeOpensWhileRestoringOrWhenMismatched(bool shuttingDown, bool closeReady, string report, bool open)
    {
        var notice = new RestoreNoticeViewModel(() => { });

        notice.Update(shuttingDown, closeReady, savingBeforeClose: false, report);

        Assert.Equal(open, notice.IsOpen);
    }

    [Fact]
    public void RestoreNoticeTextFollowsState()
    {
        var notice = new RestoreNoticeViewModel(() => { });

        notice.Update(shuttingDown: true, closeReady: false, savingBeforeClose: true, "");
        Assert.Equal("촬영 저장 중", notice.Title);
        Assert.StartsWith("녹화를 멈추고 지금까지 찍은 촬영을 저장하는 중입니다.", notice.Message);
        Assert.Equal(DialogTone.Warning, notice.Tone);

        notice.Update(shuttingDown: true, closeReady: false, savingBeforeClose: false, "");
        Assert.Equal("카메라 설정 복원 중", notice.Title);
        Assert.Equal("카메라 설정을 실행 전 값으로 되돌리는 중입니다.", notice.Message);

        notice.Update(shuttingDown: false, closeReady: true, savingBeforeClose: false, "색온도 불일치");
        Assert.Equal("카메라 설정 복원 불일치", notice.Title);
        Assert.StartsWith("카메라 설정 일부가 실행 전 값으로 돌아가지 않았습니다.", notice.Message);
        Assert.Equal(DialogTone.Error, notice.Tone);
        Assert.Equal("색온도 불일치", notice.RestoreReport);
    }

    // 종료가 끝났을 때 문제가 없으면 바로 앱을 끝내고, 있으면 사람이 확인하고 닫기를 누를 때 끝냅니다.
    [Fact]
    public void RestoreNoticeQuitsImmediatelyOnlyWhenNothingMismatched()
    {
        var quits = 0;
        var notice = new RestoreNoticeViewModel(() => quits++);

        notice.Update(false, true, false, "");
        notice.OnShutdownFinished();
        Assert.Equal(1, quits);

        notice.Update(false, true, false, "색온도 불일치");
        notice.OnShutdownFinished();
        Assert.Equal(1, quits);

        notice.CloseCommand.Execute(null);
        Assert.Equal(2, quits);
    }
}
