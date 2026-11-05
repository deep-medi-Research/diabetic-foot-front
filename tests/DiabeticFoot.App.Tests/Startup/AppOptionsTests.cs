using DiabeticFoot.App.Preferences;
using DiabeticFoot.App.Shot;
using DiabeticFoot.App.Startup;
using DiabeticFoot.Core.Pixel;

namespace DiabeticFoot.App.Tests.Startup;

public sealed class AppOptionsTests
{
    private static AppOptions Parse(params string[] args)
    {
        var options = AppOptions.Parse(args, out var error);
        Assert.Equal("", error);
        return Assert.IsType<AppOptions>(options);
    }

    // 인자를 하나도 주지 않으면 모두 없는 값입니다. 시험 실행이 아닙니다.
    [Fact]
    public void EmptyArgumentsMeanNothingSet()
    {
        var options = Parse();

        Assert.Null(options.Spec);
        Assert.Null(options.Out);
        Assert.False(options.Fake);
        Assert.False(options.IsTrial);
        Assert.Equal("", options.AnalyzerPython);
        Assert.Null(options.CloseAfterMs);
        Assert.Null(options.FakeFreeBytes);
    }

    // 값을 받는 인자와 켜고 끄는 인자를 원본(main.cc)과 같게 읽습니다.
    [Fact]
    public void ReadsValueAndSwitchArguments()
    {
        var options = Parse(
            "--spec", "specs/see3cam_manual_720p60.json", "--out", @"D:\촬영", "--fake", "--no-thermal", "--save-full",
            "--patient", "홍길동", "--birth", "19551103", "--arm-pressure", "120/80", "--ankle-pressure", "135/84", "--ankle-level", "D",
            "--pixel-left-foot", "right", "--rgb-regions", "0,0,640,720,640,0,640,720", "--rgb-area", "100,200,50,60",
            "--palette", "iron", "--fake-free-bytes", "123456789012", "--close-after", "3000", "--no-prestream",
            "--presegment-threads", "4", "--analyzer-python", "py.exe", "--analyzer-script", "w.py", "--analyze", @"D:\촬영\홍길동_19551103\D\1");

        Assert.Equal("specs/see3cam_manual_720p60.json", options.Spec);
        Assert.Equal(@"D:\촬영", options.Out);
        Assert.True(options.Fake);
        Assert.True(options.NoThermal);
        Assert.True(options.SaveFull);
        Assert.Equal("홍길동", options.Patient);
        Assert.Equal("19551103", options.Birth);
        Assert.Equal(new PressureArgument("120", "80"), options.ArmPressure);
        Assert.Equal(new PressureArgument("135", "84"), options.AnklePressure);
        Assert.Equal("D", options.AnkleLevel);
        Assert.Equal("right", options.PixelLeftFoot);
        Assert.Equal(new SideRegions(new Rect(0, 0, 640, 720), new Rect(640, 0, 640, 720)), options.RgbRegions);
        Assert.Equal(new Rect(100, 200, 50, 60), options.RgbArea);
        Assert.Equal("iron", options.Palette);
        Assert.Equal(123456789012L, options.FakeFreeBytes);
        Assert.Equal(3000, options.CloseAfterMs);
        Assert.True(options.NoPrestream);
        Assert.Equal("4", options.PresegmentThreads);
        Assert.Equal("py.exe", options.AnalyzerPython);
        Assert.Equal("w.py", options.AnalyzerScript);
        Assert.Equal(@"D:\촬영\홍길동_19551103\D\1", options.Analyze);
    }

    // 인자 이름만 주고 값을 빼면 빈 문자열이고, 값 자리에 다음 인자 이름이 와도 그대로 값으로 받습니다(원본의 Arg와 같음).
    [Fact]
    public void ValueWithoutArgumentIsEmpty()
    {
        var options = Parse("--patient", "--fake", "--birth");

        Assert.Equal("--fake", options.Patient);
        Assert.Equal("", options.Birth);
        Assert.True(options.Fake);
    }

    [Fact]
    public void ReadsResultWindowArguments()
    {
        var options = Parse("--result", "촬영 폴더", "--result-position", "0.5", "--result-variant", "v7", "--screenshot", "a.png", "--after", "2000");

        Assert.Equal("촬영 폴더", options.Result);
        Assert.Equal("0.5", options.ResultPosition);
        Assert.Equal("v7", options.ResultVariant);
        Assert.Equal("a.png", options.Screenshot);
        Assert.Equal(2000, options.AfterMs);
    }

    // 형식이 틀린 인자는 읽지 않고 이유를 알립니다(종료 코드 2).
    [Theory]
    [InlineData("--rgb-regions", "1,2,3", "--rgb-regions는 x,y,w,h,x,y,w,h 형식입니다")]
    [InlineData("--rgb-regions", "a,b,c,d,e,f,g,h", "--rgb-regions는 x,y,w,h,x,y,w,h 형식입니다")]
    [InlineData("--rgb-area", "1,2,3,4,5", "--rgb-area는 x,y,w,h 형식입니다")]
    [InlineData("--rgb-area", "", "--rgb-area는 x,y,w,h 형식입니다")]
    [InlineData("--palette", "blue", "--palette는 iron, gray, rainbow, high-contrast 중 하나입니다")]
    public void RejectsMalformedArguments(string name, string value, string message)
    {
        var options = AppOptions.Parse([name, value], out var error);

        Assert.Null(options);
        Assert.Equal(message, error);
    }

    // 시험 실행은 지난 값을 읽지 않고 바꾼 값도 남기지 않습니다. --out은 설치 바로가기도 주므로 시험 실행으로 보지 않습니다.
    [Theory]
    [InlineData("--fake", true)]
    [InlineData("--screenshot", true)]
    [InlineData("--close-after", true)]
    [InlineData("--light-remember", true)]
    [InlineData("--ppg-remember", true)]
    [InlineData("--out", false)]
    [InlineData("--no-thermal", false)]
    [InlineData("--analyze", false)]
    public void DetectsTrialRuns(string argument, bool trial)
    {
        Assert.Equal(trial, Parse(argument, "1").IsTrial);
    }

    // 처음 저장 위치는 기억한 값, --out, 현재 폴더의 captures 순서로 정합니다.
    [Fact]
    public void ResolvesOutDirectory()
    {
        var remembered = RememberedValues.ThisRunOnly();
        Assert.Equal(Path.Combine("/작업", "captures"), Parse().ResolveOutDirectory(remembered, "/작업"));
        Assert.Equal("/지정", Parse("--out", "/지정").ResolveOutDirectory(remembered, "/작업"));

        remembered.SetString(ShotFormViewModel.RootKey, "/기억");
        Assert.Equal("/기억", Parse("--out", "/지정").ResolveOutDirectory(remembered, "/작업"));
    }

    // 환자 정보 인자는 이름과 생년월일을 먼저 채우고 그 뒤에 혈압을 채웁니다(이름이나 생년월일을 바꾸면 혈압이 비워지기 때문입니다).
    [Fact]
    public void AppliesPatientArgumentsToForm()
    {
        var form = new ShotFormViewModel("/없는 폴더", RememberedValues.ThisRunOnly());
        var options = Parse("--ankle-level", "D", "--ankle-pressure", "135/84", "--arm-pressure", "120/80", "--birth", "19551103", "--patient", "홍길동");

        options.ApplyTo(form);

        Assert.Equal("홍길동", form.Name);
        Assert.Equal("1955-11-03", form.Birth);
        Assert.Equal("120", form.ArmSystolic);
        Assert.Equal("84", form.AnkleDiastolic);
        Assert.Equal("D", form.AnkleLevel);
        Assert.True(form.Ready);
    }

    // 혈압 인자에 한 칸만 주면 뒤 칸은 비어 있어 입력할 것으로 남습니다.
    [Fact]
    public void MissingPressurePartStaysEmpty()
    {
        var form = new ShotFormViewModel("/없는 폴더", RememberedValues.ThisRunOnly());

        Parse("--patient", "홍길동", "--birth", "19551103", "--arm-pressure", "120").ApplyTo(form);

        Assert.Equal("120", form.ArmSystolic);
        Assert.Equal("", form.ArmDiastolic);
        Assert.Contains("상완 이완기", form.Missing);
    }
}
