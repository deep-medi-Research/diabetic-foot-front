using System.Globalization;
using DiabeticFoot.App.Preferences;
using DiabeticFoot.App.Shot;
using DiabeticFoot.Core.Pixel;

namespace DiabeticFoot.App.Startup;

/// <summary>상완이나 발목 혈압 인자(예: 120/80)의 두 칸입니다. 없는 칸은 빈 문자열입니다.</summary>
public sealed record PressureArgument(string Systolic, string Diastolic);

/// <summary>
/// 명령줄 인자입니다. 원본 main.cc의 인자와 뜻이 같고, 모두 읽어 두되 장치를 여는 인자는 촬영 ViewModel을 이식한 뒤에 씁니다.
/// 값을 받는 인자는 없으면 null이고, 인자만 주고 값을 빼면 빈 문자열입니다(원본의 Arg와 같음).
/// </summary>
/// <remarks>
/// <code>
/// DiabeticFoot --spec &lt;선언.json&gt; [--out &lt;폴더&gt;] [--fake] [--no-thermal] [--save-full] [--screenshot &lt;png&gt; --after &lt;ms&gt; [--press-start]]
///              [--patient &lt;이름&gt; --birth &lt;0000-00-00&gt;] [--arm-pressure &lt;수축기/이완기&gt;] [--ankle-pressure &lt;수축기/이완기&gt;] [--ankle-level 0|V|D|M|S]
///              [--pixel-left-foot left|right] [--rgb-regions &lt;x,y,w,h,x,y,w,h&gt;] [--rgb-area &lt;x,y,w,h&gt;]
///              [--fake-thermal-absent | --fake-thermal-delay-ms &lt;ms&gt;] [--lens-shading-folder &lt;폴더&gt;]
///              [--light-port &lt;포트&gt; [--light-percent &lt;%&gt;]] [--light-remember &lt;VID:PID:일련번호&gt;] [--ppg-port &lt;포트&gt;] [--ppg-remember &lt;VID:PID:일련번호&gt;]
///              [--palette iron|gray|rainbow|high-contrast] [--fake-free-bytes &lt;바이트&gt;] [--fake-restore-fails] [--close-after &lt;ms&gt;]
///              [--analyzer-python &lt;python.exe&gt; --analyzer-script &lt;dfr_worker.py&gt;] [--analyze &lt;촬영 폴더&gt;] [--no-prestream] [--presegment-threads &lt;n&gt;]
/// DiabeticFoot --result &lt;촬영 폴더&gt; [--result-position &lt;0~1&gt;] [--result-variant &lt;판&gt;] [--screenshot &lt;png&gt; --after &lt;ms&gt;]
/// </code>
/// </remarks>
public sealed record AppOptions
{
    public static readonly IReadOnlyList<string> PaletteNames = ["gray", "iron", "rainbow", "high-contrast"];

    /// <summary>촬영 선언 파일입니다(--spec).</summary>
    public string? Spec { get; init; }

    /// <summary>처음 저장 위치입니다(--out). 화면에서 고른 위치를 기억하고 있으면 그 위치를 씁니다. 설치 바로가기가 문서\DiabeticFoot를 줍니다.</summary>
    public string? Out { get; init; }

    /// <summary>장비 없이 가짜 RGB 카메라, 가짜 열화상 카메라, 가짜 PPG 장치(COM6)로 화면을 띄웁니다(--fake).</summary>
    public bool Fake { get; init; }

    /// <summary>열화상 카메라를 열지 않고 RGB만 녹화합니다(--no-thermal).</summary>
    public bool NoThermal { get; init; }

    /// <summary>좌우 발 영상과 함께 전체 화면 영상도 저장합니다(--save-full).</summary>
    public bool SaveFull { get; init; }

    /// <summary>정한 시간 뒤 화면을 이미지로 저장하고 끝납니다(--screenshot). 화면이 없는 SSH 세션에서 화면을 확인할 때 씁니다.</summary>
    public string? Screenshot { get; init; }

    /// <summary>화면을 저장하기까지 기다리는 시간(--after, 밀리초)입니다.</summary>
    public int AfterMs { get; init; }

    public bool PressStart { get; init; }

    public string? Patient { get; init; }

    public string? Birth { get; init; }

    public PressureArgument? ArmPressure { get; init; }

    public PressureArgument? AnklePressure { get; init; }

    public string? AnkleLevel { get; init; }

    /// <summary>화면 왼쪽 상자의 발입니다(--pixel-left-foot). "left"나 "right"입니다.</summary>
    public string? PixelLeftFoot { get; init; }

    /// <summary>RGB 좌우 영역입니다(--rgb-regions). 격자 맞춤 없이 그대로 검사하므로 영역 오류 표시를 시험할 때도 씁니다.</summary>
    public SideRegions? RgbRegions { get; init; }

    /// <summary>RGB 패널의 관찰 영역입니다(--rgb-area).</summary>
    public Rect? RgbArea { get; init; }

    public bool FakeThermalAbsent { get; init; }

    public int FakeThermalDelayMs { get; init; }

    public string? LensShadingFolder { get; init; }

    public string? LightPort { get; init; }

    public string? LightPercent { get; init; }

    public string? LightRemember { get; init; }

    public string? PpgPort { get; init; }

    public string? PpgRemember { get; init; }

    /// <summary>열화상 색상표입니다(--palette). <see cref="PaletteNames"/> 중 하나입니다.</summary>
    public string? Palette { get; init; }

    public long? FakeFreeBytes { get; init; }

    public bool FakeRestoreFails { get; init; }

    /// <summary>정한 시간 뒤 창을 닫습니다(--close-after, 밀리초). 종료 때 카메라 복원 흐름을 시험합니다.</summary>
    public int? CloseAfterMs { get; init; }

    /// <summary>배포 폴더의 python/ 대신 쓸 관류 분석 프로그램 파이썬입니다(개발용). PATH에서 찾지 않습니다.</summary>
    public string AnalyzerPython { get; init; } = "";

    public string AnalyzerScript { get; init; } = "";

    /// <summary>시작하자마자 그 촬영 폴더를 분석 대기열에 넣습니다(--analyze). 촬영 없이 분석 막대를 시험할 때 씁니다.</summary>
    public string? Analyze { get; init; }

    /// <summary>녹화 중 미리 분석을 쓰지 않고 화면의 스위치도 숨깁니다(--no-prestream).</summary>
    public bool NoPrestream { get; init; }

    /// <summary>녹화 중 첫 장 분할에 쓸 스레드 수입니다(--presegment-threads). 기본 2, 0이면 분석과 같은 모델을 같이 씁니다.</summary>
    public string? PresegmentThreads { get; init; }

    /// <summary>분석이 끝난 촬영 폴더의 결과 창만 띄웁니다(--result, 개발용).</summary>
    public string? Result { get; init; }

    public string? ResultPosition { get; init; }

    public string? ResultVariant { get; init; }

    /// <summary>
    /// 시험 실행인지입니다. 시험 실행은 지난 값을 읽지 않고 바꾼 값도 다음 실행에 남기지 않습니다. 시험에서 바꾼 값이 장비 PC의
    /// 실제 값을 덮어쓰지 않게 하기 위함입니다. --out은 설치 바로가기도 주므로 시험 실행을 가르는 데 쓰지 않습니다.
    /// 시험용으로 준 변환기(--light-remember, --ppg-remember)로 확인한 포트도 실제 실행에 남기지 않습니다.
    /// </summary>
    public bool IsTrial => Fake || Screenshot is not null || CloseAfterMs is not null || LightRemember is not null || PpgRemember is not null;

    /// <summary>
    /// 명령줄을 읽습니다.
    /// </summary>
    /// <param name="args">실행 파일 이름을 뺀 인자</param>
    /// <param name="error">읽지 못하면 이유입니다. 이 경우 종료 코드는 2입니다</param>
    /// <returns>읽은 인자. 형식이 틀리면 null입니다</returns>
    public static AppOptions? Parse(IReadOnlyList<string> args, out string error)
    {
        error = "";
        bool Has(string name) => args.Contains(name);
        string? Arg(string name)
        {
            var i = IndexOf(args, name);
            if (i < 0) return null;
            return i + 1 < args.Count ? args[i + 1] : "";
        }
        int Int(string name) => int.TryParse(Arg(name), NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out var v) ? v : 0;

        SideRegions? regions = null;
        if (Has("--rgb-regions"))
        {
            regions = ParseRegions(Arg("--rgb-regions") ?? "");
            if (regions is null)
            {
                error = "--rgb-regions는 x,y,w,h,x,y,w,h 형식입니다";
                return null;
            }
        }
        Rect? area = null;
        if (Has("--rgb-area"))
        {
            area = ParseRect(Arg("--rgb-area") ?? "");
            if (area is null)
            {
                error = "--rgb-area는 x,y,w,h 형식입니다";
                return null;
            }
        }
        var palette = Arg("--palette");
        if (palette is not null && !PaletteNames.Contains(palette))
        {
            error = "--palette는 iron, gray, rainbow, high-contrast 중 하나입니다";
            return null;
        }
        return new AppOptions
        {
            Spec = Arg("--spec"),
            Out = Arg("--out"),
            Fake = Has("--fake"),
            NoThermal = Has("--no-thermal"),
            SaveFull = Has("--save-full"),
            Screenshot = Arg("--screenshot"),
            AfterMs = Int("--after"),
            PressStart = Has("--press-start"),
            Patient = Arg("--patient"),
            Birth = Arg("--birth"),
            ArmPressure = Pressure(Arg("--arm-pressure")),
            AnklePressure = Pressure(Arg("--ankle-pressure")),
            AnkleLevel = Arg("--ankle-level"),
            PixelLeftFoot = Arg("--pixel-left-foot"),
            RgbRegions = regions,
            RgbArea = area,
            FakeThermalAbsent = Has("--fake-thermal-absent"),
            FakeThermalDelayMs = Int("--fake-thermal-delay-ms"),
            LensShadingFolder = Arg("--lens-shading-folder"),
            LightPort = Arg("--light-port"),
            LightPercent = Arg("--light-percent"),
            LightRemember = Arg("--light-remember"),
            PpgPort = Arg("--ppg-port"),
            PpgRemember = Arg("--ppg-remember"),
            Palette = palette,
            FakeFreeBytes = long.TryParse(Arg("--fake-free-bytes"), NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out var free) ? free : Has("--fake-free-bytes") ? 0 : null,
            FakeRestoreFails = Has("--fake-restore-fails"),
            CloseAfterMs = Has("--close-after") ? Int("--close-after") : null,
            AnalyzerPython = Arg("--analyzer-python") ?? "",
            AnalyzerScript = Arg("--analyzer-script") ?? "",
            Analyze = Arg("--analyze"),
            NoPrestream = Has("--no-prestream"),
            PresegmentThreads = Arg("--presegment-threads"),
            Result = Arg("--result"),
            ResultPosition = Arg("--result-position"),
            ResultVariant = Arg("--result-variant"),
        };
    }

    /// <summary>
    /// 처음 저장 위치를 정합니다. 화면에서 고른 위치를 기억하고 있으면 그 위치, 없으면 --out, 그것도 없으면 현재 폴더의 captures입니다.
    /// </summary>
    public string ResolveOutDirectory(RememberedValues remembered, string currentDirectory)
    {
        var rememberedRoot = ShotFormViewModel.RememberedRoot(remembered);
        if (rememberedRoot.Length > 0) return rememberedRoot;
        if (!string.IsNullOrEmpty(Out)) return Out;
        return Path.Combine(currentDirectory, "captures");
    }

    /// <summary>
    /// 환자 정보 칸을 인자로 채웁니다. 화면 없이 입력 검사와 저장을 시험할 때 씁니다.
    /// 이름과 생년월일을 바꾸면 혈압 칸이 비워지므로 그 뒤에 채웁니다.
    /// </summary>
    public void ApplyTo(ShotFormViewModel form)
    {
        if (Patient is not null) form.Name = Patient;
        if (Birth is not null) form.Birth = Birth;
        if (ArmPressure is { } arm)
        {
            form.ArmSystolic = arm.Systolic;
            form.ArmDiastolic = arm.Diastolic;
        }
        if (AnklePressure is { } ankle)
        {
            form.AnkleSystolic = ankle.Systolic;
            form.AnkleDiastolic = ankle.Diastolic;
        }
        if (AnkleLevel is not null) form.AnkleLevel = AnkleLevel;
    }

    private static int IndexOf(IReadOnlyList<string> args, string name)
    {
        for (var i = 0; i < args.Count; i++)
        {
            if (args[i] == name) return i;
        }
        return -1;
    }

    private static PressureArgument? Pressure(string? text)
    {
        if (text is null) return null;
        var parts = text.Split('/');
        return new PressureArgument(parts[0], parts.Length > 1 ? parts[1] : "");
    }

    // "x,y,w,h,x,y,w,h"를 좌우 영역으로 읽습니다. 숫자가 여덟 개가 아니면 null입니다.
    private static SideRegions? ParseRegions(string text)
    {
        var numbers = ParseInts(text, 8);
        if (numbers is null) return null;
        return new SideRegions(new Rect(numbers[0], numbers[1], numbers[2], numbers[3]), new Rect(numbers[4], numbers[5], numbers[6], numbers[7]));
    }

    private static Rect? ParseRect(string text)
    {
        var numbers = ParseInts(text, 4);
        return numbers is null ? null : new Rect(numbers[0], numbers[1], numbers[2], numbers[3]);
    }

    private static int[]? ParseInts(string text, int count)
    {
        var parts = text.Split(',');
        if (parts.Length != count) return null;
        var numbers = new int[count];
        for (var i = 0; i < count; i++)
        {
            if (!int.TryParse(parts[i].Trim(), NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out numbers[i])) return null;
        }
        return numbers;
    }
}
