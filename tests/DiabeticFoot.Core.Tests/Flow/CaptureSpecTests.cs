using System.Text;
using DiabeticFoot.Core.Flow;
using DiabeticFoot.Core.Tests.Support;
using DiabeticFoot.Core.Timing;
using static DiabeticFoot.Core.Flow.FlowFunctions;

namespace DiabeticFoot.Core.Tests.Flow;

public class CaptureSpecTests
{
    private static string ReadSpecFile(string name) => TestData.ReadAllText("specs", name);

    // 첫 번째 일치만 바꿉니다(원본의 text.replace(text.find(...), 길이, ...)와 같습니다).
    private static string ReplaceFirst(string text, string old, string replacement)
    {
        int at = text.IndexOf(old, StringComparison.Ordinal);
        Assert.True(at >= 0, $"선언 파일에 {old}가 없습니다");
        return text.Remove(at, old.Length).Insert(at, replacement);
    }

    // 읽기에 실패하면 이유를 메시지로 남기고 멈춥니다(원본의 ASSERT_TRUE(spec) << error).
    private static CaptureSpec Parsed(string text)
    {
        var spec = ParseCaptureSpec(text, out string error);
        Assert.True(spec is not null, error);
        return spec!;
    }

    // 레포의 직접 지정 선언 파일을 읽으면 실측값이 그대로 들어오고 검증을 통과합니다.
    [Fact]
    public void ParsesManualSpecFile()
    {
        var spec = Parsed(ReadSpecFile("see3cam_manual_30fps.json"));

        Assert.Equal("See3CAM_24CUG", spec.Device);
        Assert.Equal(SettingMode.Manual, spec.Settings.Mode);
        Assert.Equal(-5, spec.Settings.ExposureStep);
        Assert.Equal(75, spec.Settle.BlackLevelSettledFrame);
        Assert.Equal(900, spec.TargetFrames);
        Assert.Equal(VideoCodec.Ffv1, spec.Codec);
        Assert.Equal("mkv", ExtensionOf(spec.Codec));
        Assert.Equal(111111, spec.ThermalRecordPeriodUs);
        Assert.Empty(ValidateCaptureSpec(spec));
    }

    // 모르는 코덱 이름은 거부합니다.
    [Fact]
    public void RejectsUnknownCodec()
    {
        string text = ReplaceFirst(ReadSpecFile("see3cam_manual_30fps.json"), "\"ffv1\"", "\"h264\"");

        Assert.Null(ParseCaptureSpec(text, out string error));
        Assert.Contains("codec", error);
    }

    // 자동 맞춤 선언에는 노출 단계가 없어도 됩니다.
    [Fact]
    public void ParsesAutoSpecWithoutExposure()
    {
        var spec = Parsed(ReadSpecFile("see3cam_auto_30fps.json"));

        Assert.Equal(SettingMode.Auto, spec.Settings.Mode);
        Assert.Empty(ValidateCaptureSpec(spec));
    }

    // 기본 선언(1280x720 60 fps 고정 노출)은 30초를 1800장으로 찍고 검증을 통과합니다.
    [Fact]
    public void ParsesDefault720p60SpecFile()
    {
        var spec = Parsed(ReadSpecFile("see3cam_manual_720p60.json"));

        Assert.Equal(1280, spec.Width);
        Assert.Equal(720, spec.Height);
        Assert.Equal(1800, spec.TargetFrames);
        Assert.Equal(SettingMode.Manual, spec.Settings.Mode);
        Assert.Equal(60, spec.Settings.SdkFrameRate);
        Assert.Empty(ValidateCaptureSpec(spec));
    }

    // 패키지에 들어가는 선언 파일은 모두 읽히고 검증을 통과해야 앱이 시작합니다(없앤 키가 남으면 앱이 바로 끝납니다).
    [Fact]
    public void EverySpecFileParses()
    {
        int count = 0;
        foreach (string path in Directory.EnumerateFiles(TestData.Path("specs")))
        {
            if (Path.GetExtension(path) != ".json")
            {
                continue;
            }
            string name = Path.GetFileName(path);
            var spec = ParseCaptureSpec(ReadSpecFile(name), out string error);
            Assert.True(spec is not null, $"{name}: {error}");
            Assert.True(ValidateCaptureSpec(spec!).Count == 0, name);
            ++count;
        }
        Assert.True(count >= 5);
    }

    // 직접 지정인데 노출 단계가 빠지면 거부합니다.
    [Fact]
    public void ManualRequiresExposure()
    {
        string text = ReplaceFirst(ReadSpecFile("see3cam_manual_30fps.json"), "\"exposure_step\": -5,", "");

        Assert.Null(ParseCaptureSpec(text, out string error));
        Assert.Contains("settings.exposure_step", error);
    }

    // 오타로 생긴 모르는 키는 조용히 무시하지 않고 거부합니다.
    [Fact]
    public void RejectsUnknownKey()
    {
        string text = ReplaceFirst(ReadSpecFile("see3cam_manual_30fps.json"), "\"target_frames\"", "\"target_frame\"");

        Assert.Null(ParseCaptureSpec(text, out string error));
        Assert.NotEmpty(error);
    }

    // 정수가 아닌 숫자와 깨진 문서는 위치와 함께 거부합니다.
    [Fact]
    public void RejectsFractionAndBrokenJson()
    {
        Assert.Null(ParseCaptureSpec("""{"width": 1920.5}""", out string error));
        Assert.Contains("정수", error);
        Assert.Null(ParseCaptureSpec("""{"width": 1920""", out _));
    }

    // 검증은 값의 범위를 봅니다. 홀수 폭은 UYVY 화소 쌍을 깨뜨립니다.
    [Fact]
    public void ValidationFindsBadValues()
    {
        var spec = new CaptureSpec
        {
            Device = "cam",
            Width = 1921,
            Height = 1080,
            Settings = new SettingsSpec { SdkFrameRate = 30, Gain = 4, WhiteBalanceK = 4600 },
            TargetFrames = 900,
            Settle = new SettleRule
            {
                ExposureAppliedFrame = 4,
                GainAppliedFrame = 3,
                WhiteBalanceAppliedFrame = 3,
                BlackLevelSettledFrame = 0,
            },
            ClipLevel = 250,
            BufferFrames = 60,
        };

        var problems = ValidateCaptureSpec(spec);

        Assert.Equal(2, problems.Count);
        Assert.Equal(SpecProblem.BadSize, problems[0]);
        Assert.Equal(SpecProblem.BadSettleRule, problems[1]);
    }

    // 검사 기준 목록(criteria)은 2026-10-30부터 받지 않습니다. 그 키가 남은 선언 파일은 모르는 키로 거부합니다.
    [Fact]
    public void RejectsCriteriaKey()
    {
        string text = ReplaceFirst(
            ReadSpecFile("see3cam_auto_30fps.json"),
            "\"codec\": \"ffv1\",",
            "\"codec\": \"ffv1\", \"criteria\": [0, 40, 80, 120],");

        Assert.Null(ParseCaptureSpec(text, out string error));
        Assert.Contains("criteria", error);
    }

    // 열화상 기록 주기는 필수 키이고 0(모두 기록)을 명시할 수 있으며, 음수는 검증에서 거부합니다.
    [Fact]
    public void ThermalRecordPeriodIsRequiredAndNonNegative()
    {
        string text = ReadSpecFile("see3cam_auto_30fps.json");
        string missing = ReplaceFirst(text, "\"thermal_record_period_us\": 111111,", "");
        Assert.Null(ParseCaptureSpec(missing, out string error));
        Assert.Contains("thermal_record_period_us", error);

        string zero = ReplaceFirst(text, "111111", "0");
        var spec = Parsed(zero);
        Assert.Equal(0, spec.ThermalRecordPeriodUs);
        Assert.Empty(ValidateCaptureSpec(spec));

        spec = spec with { ThermalRecordPeriodUs = -1 };
        var problems = ValidateCaptureSpec(spec);
        var problem = Assert.Single(problems);
        Assert.Equal(SpecProblem.BadThermalRecordPeriod, problem);
    }

    // int32 칸에 들지 않는 정수는 잘라 넣지 않고 거부합니다. 4294969216을 자르면 1920이 되어 검증을 통과합니다.
    [Fact]
    public void RejectsIntegerOutsideInt32()
    {
        string text = ReadSpecFile("see3cam_manual_30fps.json");

        string wide = ReplaceFirst(text, "\"width\": 1920", "\"width\": 4294969216");
        Assert.Null(ParseCaptureSpec(wide, out string error));
        Assert.Contains("width", error);

        string gain = ReplaceFirst(text, "\"gain\": 4", "\"gain\": -2147483649");
        Assert.Null(ParseCaptureSpec(gain, out error));
        Assert.Contains("settings.gain", error);

        string edge = ReplaceFirst(text, "\"gain\": 4", "\"gain\": 2147483647");
        var spec = Parsed(edge);
        Assert.Equal(2147483647, spec.Settings.Gain);
    }

    // 아래 네 테스트는 C#에서만 필요한 확인입니다. 기대 문장은 원본 ParseCaptureSpec이 같은 입력에 낸 문장입니다.
    // 오류 위치는 문자 수가 아니라 UTF-8 바이트 위치입니다. 한글 두 글자는 6바이트이고 이모지는 4바이트입니다.
    [Fact]
    public void ErrorPositionCountsUtf8Bytes()
    {
        Assert.Null(ParseCaptureSpec("""{"device": "한글", "width": 1920""", out string error));
        Assert.Equal("객체가 ,나 }로 이어지지 않습니다 (34번째 바이트)", error);

        Assert.Null(ParseCaptureSpec("""{"device": "한글" "width": 1}""", out error));
        Assert.Equal("객체가 ,나 }로 이어지지 않습니다 (20번째 바이트)", error);

        Assert.Null(ParseCaptureSpec("""{"device": "😀\u0041"}""", out error));
        Assert.Equal("지원하지 않는 이스케이프입니다 (18번째 바이트)", error);

        Assert.Null(ParseCaptureSpec("{\"width\": 1}  \n 한", out error));
        Assert.Equal("문서 끝 뒤에 값이 더 있습니다 (16번째 바이트)", error);

        Assert.Null(ParseCaptureSpec("""{"a": 1, "a": 2}""", out error));
        Assert.Equal("같은 키가 두 번 나옵니다 (15번째 바이트)", error);

        Assert.Null(ParseCaptureSpec("{\"width\": 1920 ,\n \"k\\\"ey\": [1, 2,]}", out error));
        Assert.Equal("알 수 없는 값입니다 (33번째 바이트)", error);
    }

    // 공백은 C 로캘의 std::isspace와 같은 여섯 문자(공백, \t, \n, \v, \f, \r)만 건너뜁니다.
    [Fact]
    public void SkipsOnlySixAsciiWhitespaceCharacters()
    {
        Assert.Null(ParseCaptureSpec("{\v\"width\"\f:\r1}", out string error));
        Assert.Equal("키가 없습니다: device", error);

        Assert.Null(ParseCaptureSpec("{\u00a0\"width\": 1}", out error));
        Assert.Equal("키는 문자열이어야 합니다 (1번째 바이트)", error);

        Assert.Null(ParseCaptureSpec("{\"width\":\u30001}", out error));
        Assert.Equal("알 수 없는 값입니다 (9번째 바이트)", error);

        Assert.Null(ParseCaptureSpec("\ufeff{}", out error));
        Assert.Equal("알 수 없는 값입니다 (0번째 바이트)", error);
    }

    // 모르는 키가 여럿이면 원본의 std::map처럼 바이트 순서로 앞선 키를 알립니다. UTF-16 순서였다면 이모지가 앞섭니다.
    [Fact]
    public void ReportsFirstUnknownKeyInUtf8ByteOrder()
    {
        string text = ReplaceFirst(
            ReadSpecFile("see3cam_auto_30fps.json"),
            "{\n",
            "{\n  \"😀\": 1, \"\ue000\": 2,\n");

        Assert.Null(ParseCaptureSpec(text, out string error));
        Assert.Equal("모르는 키입니다: \ue000", error);
    }

    // 문자열 값은 UTF-8로 읽은 뒤 되돌리므로 한글과 이모지, 지원하는 이스케이프가 그대로 남습니다.
    [Fact]
    public void KeepsNonAsciiAndEscapesInStringValue()
    {
        string text = ReplaceFirst(
            ReadSpecFile("see3cam_auto_30fps.json"),
            "\"See3CAM_24CUG\"",
            "\"카메라😀\\\"a\\\\b\\/c\\nd\\te\"");

        var spec = Parsed(text);

        Assert.Equal("카메라😀\"a\\b/c\nd\te", spec.Device);
    }

    private static IReadOnlyList<SpecProblem> ProblemsWith(Func<CaptureSpec, CaptureSpec> change) =>
        ValidateCaptureSpec(change(Parsed(ReadSpecFile("see3cam_manual_30fps.json"))));

    // 폭과 높이는 음수, 0, MaxFrameSide 초과를 거부합니다.
    [Fact]
    public void ValidationRejectsImpossibleSize()
    {
        var bad = new[] { SpecProblem.BadSize };
        Assert.Equal(bad, ProblemsWith(s => s with { Width = -1920 }));
        Assert.Equal(bad, ProblemsWith(s => s with { Height = 0 }));
        Assert.Equal(bad, ProblemsWith(s => s with { Width = CaptureSpec.MaxFrameSide + 2 }));
        Assert.Equal(bad, ProblemsWith(s => s with { Height = CaptureSpec.MaxFrameSide + 1 }));
        Assert.Empty(ProblemsWith(s => s with { Width = CaptureSpec.MaxFrameSide, Height = CaptureSpec.MaxFrameSide }));
    }

    // 직접 지정의 노출 단계는 -13~0만 받습니다. 자동 맞춤은 노출 단계를 쓰지 않아 보지 않습니다.
    [Fact]
    public void ValidationChecksExposureOnlyInManual()
    {
        var bad = new[] { SpecProblem.BadExposure };
        Assert.Equal(bad, ProblemsWith(s => s with { Settings = s.Settings with { ExposureStep = -14 } }));
        Assert.Equal(bad, ProblemsWith(s => s with { Settings = s.Settings with { ExposureStep = 1 } }));
        Assert.Empty(ProblemsWith(s => s with { Settings = s.Settings with { ExposureStep = -13 } }));
        Assert.Empty(ProblemsWith(s => s with { Settings = s.Settings with { ExposureStep = 0 } }));
        Assert.Empty(ProblemsWith(s => s with { Settings = s.Settings with { Mode = SettingMode.Auto, ExposureStep = 99 } }));
    }

    // 게인과 색온도는 UVC 제어 값(부호 없는 2바이트)에 들지 않는 값을 거부하고, 색온도 0도 거부합니다.
    [Fact]
    public void ValidationRejectsImpossibleGainAndWhiteBalance()
    {
        Assert.Equal(
            new[] { SpecProblem.BadGain },
            ProblemsWith(s => s with { Settings = s.Settings with { Gain = -1 } }));
        Assert.Equal(
            new[] { SpecProblem.BadGain },
            ProblemsWith(s => s with { Settings = s.Settings with { Gain = 65536 } }));
        Assert.Equal(
            new[] { SpecProblem.BadWhiteBalance },
            ProblemsWith(s => s with { Settings = s.Settings with { WhiteBalanceK = 0 } }));
        Assert.Equal(
            new[] { SpecProblem.BadWhiteBalance },
            ProblemsWith(s => s with { Settings = s.Settings with { WhiteBalanceK = 70000 } }));
        Assert.Empty(ProblemsWith(s => s with { Settings = s.Settings with { Gain = 0, WhiteBalanceK = 65535 } }));
    }

    // C#에서 추가한 확인입니다. 바이트를 그대로 넘기면 UTF-8이 아닌 바이트가 있어도 오류 문장의 바이트 위치가 원본과 같습니다.
    // 기대 문장은 원본 capture_spec.cc를 컴파일해 같은 바이트로 얻은 값입니다.
    [Fact]
    public void ByteInputKeepsErrorPositionsWithInvalidUtf8()
    {
        byte[] bytes = new byte[]
        {
            (byte)'{', (byte)'"', (byte)'d', (byte)'e', (byte)'v', (byte)'i', (byte)'c', (byte)'e', (byte)'"', (byte)':', (byte)' ',
            (byte)'"', 0xC3, 0x28, 0xFF, 0xFE, (byte)'"', (byte)',', (byte)' ',
            (byte)'"', (byte)'w', (byte)'i', (byte)'d', (byte)'t', (byte)'h', (byte)'"', (byte)':', (byte)' ',
            (byte)'1', (byte)'.', (byte)'5', (byte)'}',
        };

        Assert.Null(ParseCaptureSpec(bytes, out string error));
        Assert.Equal("선언 파일의 숫자는 정수만 씁니다 (29번째 바이트)", error);
    }

    // 문자열 값에 든 UTF-8이 아닌 바이트는 C# 문자열에서 U+FFFD가 되지만 읽기는 성공합니다(원본은 바이트를 그대로 둡니다).
    [Fact]
    public void ByteInputReplacesInvalidUtf8InStringValue()
    {
        string text = ReadSpecFile("see3cam_manual_30fps.json");
        string withInvalid = ReplaceFirst(text, "See3CAM_24CUG", "See3@CAM_24CUG");
        byte[] bytes = Encoding.UTF8.GetBytes(withInvalid);
        int at = Array.IndexOf(bytes, (byte)'@');
        bytes[at] = 0xFF;

        var spec = ParseCaptureSpec(bytes, out string error);

        Assert.NotNull(spec);
        Assert.Equal("", error);
        Assert.Equal("See3\uFFFDCAM_24CUG", spec.Device);
    }

    // BOM은 원본처럼 값이 아닌 바이트로 보아 거부합니다. 바이트 입력과 BOM을 지키는 문자열 입력의 결과가 같습니다.
    [Fact]
    public void ByteOrderMarkIsRejectedLikeOriginal()
    {
        string text = ReadSpecFile("see3cam_manual_30fps.json");
        byte[] bytes = new byte[] { 0xEF, 0xBB, 0xBF }.Concat(Encoding.UTF8.GetBytes(text)).ToArray();

        Assert.Null(ParseCaptureSpec(bytes, out string byteError));
        Assert.Equal("알 수 없는 값입니다 (0번째 바이트)", byteError);
        Assert.Null(ParseCaptureSpec("\uFEFF" + text, out string textError));
        Assert.Equal(byteError, textError);
    }
}
