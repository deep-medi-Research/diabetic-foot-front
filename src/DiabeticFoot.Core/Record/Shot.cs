using System.Text;

namespace DiabeticFoot.Core.Record;

/// <summary>
/// 촬영의 조명 정보입니다. 매니페스트의 lighting입니다. 출처(<see cref="LightSource"/>)는 사이드카 light와 같은 값을 씁니다.
/// </summary>
/// <remarks>BrightnessPercent는 표 2의 조명 밝기(LED 보드에 보내는 데 성공한 값)입니다. 앱이 보내지 않았으면 비웁니다.</remarks>
public sealed record LightingInfo
{
    public LightSource Source { get; init; } = LightSource.Unknown;

    /// <summary>조명 제품입니다. 예: "4000K COB LED, 12V".</summary>
    public string Product { get; init; } = "";

    /// <summary>앱이 보낸 밝기(%)입니다. 확인하지 못했으면 비웁니다.</summary>
    public int? BrightnessPercent { get; init; }
}

/// <summary>열화상 장치가 있는데도 이번 촬영에 열화상을 녹화하지 않은 이유입니다.</summary>
public enum ThermalAbsence : byte
{
    /// <summary>열화상을 녹화했거나 이유를 정하지 않았습니다.</summary>
    None,

    /// <summary>실행할 때 열화상을 껐습니다.</summary>
    Disabled,

    /// <summary>이 OS에 열화상 장치 구현이 없습니다.</summary>
    NoDevice,

    /// <summary>촬영 버튼을 누른 시점에 장치를 열지 못했거나 끊겼습니다.</summary>
    NotReceiving,

    /// <summary>촬영 버튼을 누른 시점에 장치를 열고 첫 프레임을 기다리는 중이었습니다(셔터 교정).</summary>
    Preparing,
}

/// <summary>
/// 촬영 전에 한 부위에서 잰 혈압입니다. 0은 입력하지 않음입니다.
/// </summary>
/// <remarks>커프 압력은 발목 혈압으로 계산하는 발목 혈압 단계로 정합니다. 잰 혈압이 없으면 촬영 파일만으로 커프 압력을 알 수 없습니다.</remarks>
public readonly record struct BloodPressure(int SystolicMmhg = 0, int DiastolicMmhg = 0)
{
    /// <summary>받는 혈압 값의 하한(mmHg)입니다. 자릿수를 빠뜨린 입력(예: 80을 8로)을 거부하기 위함입니다.</summary>
    public const int MinMmhg = 20;

    /// <summary>받는 혈압 값의 상한(mmHg)입니다. 혈압계 눈금의 끝(300mmHg)입니다.</summary>
    public const int MaxMmhg = 300;

    /// <summary>
    /// 발목 혈압 단계의 이름입니다. 화면의 버튼 순서와 같습니다.
    /// 0은 0mmHg, V는 발목 이완기 - 50, D는 발목 이완기, M은 (발목 수축기 + 발목 이완기) / 2, S는 발목 수축기입니다.
    /// 관류 분석 프로그램이 내는 커프 단계(cuff_level.level, #72)와 같은 글자이며 대소문자를 구분합니다.
    /// </summary>
    public static readonly IReadOnlyList<string> AnkleLevels = new[] { "0", "V", "D", "M", "S" };
}

/// <summary>녹화 버튼을 누른 때의 두 발 열화상 대비와 사용자 선택입니다. 매니페스트의 foot_warmth입니다.</summary>
/// <param name="Left">환자 왼발 상자의 대비(원시값, ThermalContrast)입니다.</param>
/// <param name="Right">환자 오른발 상자의 대비입니다.</param>
/// <param name="MinWarm">따뜻하다고 본 아래 끝(FootWarmthCheck.WarmContrastMin)입니다.</param>
/// <param name="ColdConfirmed">발이 차갑다는 안내에 사용자가 진행을 골랐으면 true입니다.</param>
public readonly record struct FootWarmthRecord(int Left = 0, int Right = 0, int MinWarm = 0, bool ColdConfirmed = false);

/// <summary>
/// 촬영 한 번을 가리키는 정보입니다.
/// </summary>
/// <remarks>이름, 생년월일, 혈압은 개인정보라 매니페스트에만 남기고 사이드카에는 넣지 않습니다.</remarks>
public sealed record ShotInfo
{
    public string PatientName { get; init; } = "";

    /// <summary>YYYYMMDD입니다.</summary>
    public string BirthDate { get; init; } = "";

    /// <summary>상완에서 잰 혈압입니다.</summary>
    public BloodPressure ArmPressure { get; init; }

    /// <summary>발목에서 잰 혈압입니다.</summary>
    public BloodPressure AnklePressure { get; init; }

    /// <summary>발목 혈압 단계입니다. <see cref="BloodPressure.AnkleLevels"/> 중 하나이고, 비어 있으면 고르지 않은 것입니다.</summary>
    public string AnkleLevel { get; init; } = "";

    /// <summary>촬영 시각(PC 현지 시각)입니다.</summary>
    public int Year { get; init; }

    public int Month { get; init; }

    public int Day { get; init; }

    public int Hour { get; init; }

    public int Minute { get; init; }

    public int Second { get; init; }

    /// <summary>화소 왼쪽(프레임의 x가 작은 쪽)에 찍히는 환자의 발입니다. RGB와 열화상의 좌우 파일 이름을 이 값 하나로 정합니다.</summary>
    public FootSide PixelLeftFoot { get; init; } = FootSide.Right;

    /// <summary>매니페스트의 lighting입니다.</summary>
    public LightingInfo Lighting { get; init; } = new();

    /// <summary>열화상을 녹화하지 않은 이유입니다. 매니페스트의 thermal_absent_reason이 됩니다(열화상 역할이 배치에 없을 때만).</summary>
    public ThermalAbsence ThermalAbsence { get; init; }

    /// <summary>열화상 없이 찍는다는 안내에 사용자가 진행을 골랐습니다. 장치 고장으로 빠진 촬영과 구분하기 위해 매니페스트에 남깁니다.</summary>
    public bool ThermalSkipConfirmed { get; init; }

    /// <summary>
    /// 같은 날, 같은 환자, 같은 발목 혈압 단계에서 몇 번째 촬영인지(1부터)입니다. 0이면 세지 않았습니다. 재측정 기록에 씁니다
    /// (IRB 측정 실패 사유는 앞 촬영 요약의 qc에 있습니다).
    /// </summary>
    public int Attempt { get; init; }

    /// <summary>미리보기 열화상으로 판정한 두 발 대비입니다. 열화상을 녹화하고 판정이 끝났을 때만 있습니다.</summary>
    public FootWarmthRecord? FootWarmth { get; init; }

    /// <summary>찍은 앱의 버전(vX.Y.Z, 개발 빌드는 빈 문자열)입니다. 같은 촬영을 어느 앱으로 찍었는지 촬영 파일만으로 알기 위함입니다.</summary>
    public string AppVersion { get; init; } = "";

    /// <summary>찍은 앱의 커밋입니다.</summary>
    public string AppCommit { get; init; } = "";

    /// <summary>촬영 선언 파일의 이름입니다. 이름만으로는 같은 이름의 파일을 고친 경우를 구분하지 못합니다.</summary>
    public string SpecFile { get; init; } = "";

    /// <summary>촬영 선언 파일의 SHA-256입니다.</summary>
    public string SpecSha256 { get; init; } = "";
}

/// <summary>촬영 폴더 안 파일의 역할입니다.</summary>
public enum FileRole : byte
{
    LeftVideo,
    LeftSidecar,
    RightVideo,
    RightSidecar,
    ThermalRaw,
    ThermalSidecar,

    /// <summary>열화상을 볼 수 있게 옮긴 AVI입니다.</summary>
    ThermalPreview,
    Manifest,

    /// <summary>자르지 않은 전체 화면 영상입니다.</summary>
    FullVideo,

    /// <summary>전체 화면 영상의 사이드카입니다.</summary>
    FullSidecar,

    /// <summary>왼발 열화상 원시값(.npy)입니다. 관류 분석 입력입니다.</summary>
    ThermalLeftArray,

    /// <summary>오른발 열화상 원시값(.npy)입니다. 관류 분석 입력입니다.</summary>
    ThermalRightArray,
}

/// <summary>촬영 폴더 안 파일 하나입니다.</summary>
/// <param name="Role">파일 역할입니다.</param>
/// <param name="Name">촬영 폴더 안의 파일 이름입니다.</param>
public sealed record ShotFile(FileRole Role = FileRole.Manifest, string Name = "");

/// <summary>촬영 폴더와 파일 이름입니다. 경로 구분자는 '/'이고 OS에 맞게 바꾸는 것은 platform이 합니다.</summary>
public sealed record ShotLayout
{
    /// <summary>분석결과를 모으는 폴더 이름입니다. 저장 위치 바로 아래에 둡니다. 경로를 영문으로 두어 압축, 메일, 다른 프로그램에서 이름이 깨지지 않게 합니다(표 11).</summary>
    public const string CollectedResultFolder = "results";

    /// <summary>다시 분석한 결과를 모을 때 쓰는 번호의 상한입니다. 경로 길이 검사도 이 번호까지 셉니다.</summary>
    public const int MaxReanalysisNumber = 99;

    /// <summary>이름_생년월일/발목 혈압 단계/날짜_시각입니다.</summary>
    public string Folder { get; init; } = "";

    /// <summary>폴더의 마지막 이름이자 파일 이름의 앞부분입니다. 보통 날짜_시각(YYYYMMDD_HHMMSS)입니다.</summary>
    public string Stem { get; init; } = "";

    public IReadOnlyList<ShotFile> Files { get; init; } = Array.Empty<ShotFile>();

    /// <summary>역할에 맞는 파일 이름입니다. 없으면 빈 문자열입니다.</summary>
    public string NameOf(FileRole role)
    {
        foreach (ShotFile f in Files)
        {
            if (f.Role == role)
            {
                return f.Name;
            }
        }
        return "";
    }
}

/// <summary>매니페스트에 남기는 장치 하나입니다.</summary>
/// <param name="Stream">장치의 스트림 종류입니다.</param>
/// <param name="Name">장치가 알려 준 이름입니다.</param>
public sealed record ShotDevice(StreamKind Stream = StreamKind.Rgb, string Name = "");

/// <summary>촬영 정보 검증에서 찾은 문제입니다.</summary>
public enum ShotProblem : byte
{
    EmptyName,

    /// <summary>Windows 파일 이름에 쓸 수 없는 문자, 제어 문자, 앞의 공백, 끝의 점과 공백입니다.</summary>
    BadNameCharacter,
    BadBirthDate,

    /// <summary>상완 혈압이 없거나 받는 범위 밖이거나 수축기가 이완기보다 크지 않습니다.</summary>
    BadArmPressure,

    /// <summary>발목 혈압입니다. 기준은 상완과 같습니다.</summary>
    BadAnklePressure,

    /// <summary>발목 혈압 단계를 고르지 않았거나 목록에 없습니다.</summary>
    UnknownAnkleLevel,

    /// <summary>발목 혈압 단계의 압력이 0보다 작습니다(발목 이완기가 50mmHg보다 낮을 때의 V).</summary>
    NegativeAnkleLevel,
    BadTime,
}

/// <summary>촬영 폴더 경로를 나눈 결과입니다.</summary>
public sealed record ShotFolderPath
{
    /// <summary>저장 위치입니다. 끝 구분자는 없습니다.</summary>
    public string Root { get; init; } = "";

    /// <summary>Folder와 Stem만 채웁니다. Files는 비어 있습니다.</summary>
    public ShotLayout Layout { get; init; } = new();
}

public static partial class RecordFunctions
{
    /// <summary>
    /// 파일 이름 앞에 시각(<see cref="ShotLayout.Stem"/>)을 넣는 역할인지 정합니다.
    /// </summary>
    /// <param name="role">파일 역할</param>
    /// <returns>시각을 넣으면 true. 열화상 .npy(thermal_left.npy, thermal_right.npy)만 false입니다</returns>
    /// <remarks>관류 분석 코드가 .npy 이름을 고정해 읽으므로 접두 없이 씁니다. 촬영 폴더가 촬영마다 하나라 같은 이름이 생기지 않습니다.</remarks>
    public static bool UsesStemPrefix(FileRole role) => NameRuleOf(role, "").StemPrefix;

    /// <summary>
    /// 관류 분석에 넘기는 파일 역할인지 정합니다.
    /// </summary>
    /// <returns>좌우 영상과 좌우 열화상 .npy이면 true</returns>
    public static bool IsAnalysisInput(FileRole role) =>
        role == FileRole.LeftVideo || role == FileRole.RightVideo || role == FileRole.ThermalLeftArray ||
        role == FileRole.ThermalRightArray;

    /// <summary>
    /// 발목 혈압 단계의 압력(mmHg)을 발목 혈압으로 계산합니다.
    /// </summary>
    /// <param name="level">단계 이름. <see cref="BloodPressure.AnkleLevels"/> 중 하나</param>
    /// <param name="ankle">발목에서 잰 혈압</param>
    /// <returns>압력(mmHg). M은 0.5 단위일 수 있습니다. level이 목록에 없으면 null입니다</returns>
    /// <remarks>계산식은 분석 쪽 요청(2026-10-29, #68)입니다. V는 발목 이완기가 50mmHg보다 낮으면 음수입니다.</remarks>
    public static double? AnkleLevelMmhg(string level, BloodPressure ankle)
    {
        if (level == "0") return 0.0;
        if (level == "V") return ankle.DiastolicMmhg - 50.0;
        if (level == "D") return (double)ankle.DiastolicMmhg;
        if (level == "M") return (ankle.SystolicMmhg + ankle.DiastolicMmhg) / 2.0;
        if (level == "S") return (double)ankle.SystolicMmhg;
        return null;
    }

    /// <summary>
    /// 촬영 정보가 폴더와 파일 이름, 매니페스트를 만들 수 있는 값인지 검증합니다.
    /// </summary>
    /// <param name="info">촬영 정보</param>
    /// <returns>찾은 문제 목록. 비어 있으면 통과입니다</returns>
    /// <remarks>
    /// 이름에 Windows가 거부하는 문자(&lt; &gt; : " / \ | ? *), 제어 문자, 앞의 공백, 끝의 점과 공백이 있으면 거부합니다.
    /// 기존 앱은 이런 문자를 지우고 저장했지만, 지우면 입력한 이름과 폴더 이름이 달라지므로 거부하고 화면이 이유를 보여 줍니다.
    /// 상완과 발목 혈압은 둘 다 있어야 합니다. 값은 <see cref="BloodPressure.MinMmhg"/> 이상 <see cref="BloodPressure.MaxMmhg"/> 이하이고
    /// 수축기가 이완기보다 커야 합니다.
    /// 발목 혈압 단계는 <see cref="BloodPressure.AnkleLevels"/> 중 하나여야 하고, 발목 혈압으로 계산한 압력이 0 이상이어야 합니다.
    /// </remarks>
    public static IReadOnlyList<ShotProblem> ValidateShot(ShotInfo info)
    {
        var problems = new List<ShotProblem>();
        if (info.PatientName.Length == 0)
        {
            problems.Add(ShotProblem.EmptyName);
        }
        else if (!IsSafeName(info.PatientName))
        {
            problems.Add(ShotProblem.BadNameCharacter);
        }
        if (!IsDigits(info.BirthDate, 8) ||
            !IsValidDate(int.Parse(info.BirthDate.AsSpan(0, 4)), int.Parse(info.BirthDate.AsSpan(4, 2)), int.Parse(info.BirthDate.AsSpan(6, 2))))
        {
            problems.Add(ShotProblem.BadBirthDate);
        }
        if (!IsValidPressure(info.ArmPressure)) problems.Add(ShotProblem.BadArmPressure);
        if (!IsValidPressure(info.AnklePressure)) problems.Add(ShotProblem.BadAnklePressure);
        double? levelMmhg = AnkleLevelMmhg(info.AnkleLevel, info.AnklePressure);
        if (levelMmhg is null)
        {
            problems.Add(ShotProblem.UnknownAnkleLevel);
        }
        else if (IsValidPressure(info.AnklePressure) && levelMmhg.Value < 0)
        {
            problems.Add(ShotProblem.NegativeAnkleLevel);
        }
        if (!IsValidDate(info.Year, info.Month, info.Day) || info.Hour < 0 || info.Hour > 23 || info.Minute < 0 ||
            info.Minute > 59 || info.Second < 0 || info.Second > 59)
        {
            problems.Add(ShotProblem.BadTime);
        }
        return problems;
    }

    /// <summary>
    /// 촬영 폴더와 파일 이름을 만듭니다. 폴더는 이름_생년월일/발목 혈압 단계/날짜_시각입니다(예: 시험_19551103/D/20261022_101454).
    /// </summary>
    /// <param name="info">촬영 정보</param>
    /// <param name="videoExtension">영상 파일 확장자(점 없이). 촬영 선언의 코덱에서 <c>ExtensionOf</c>로 구합니다</param>
    /// <param name="roles">이번 촬영이 만드는 파일의 역할. 매니페스트의 파일 목록이 이 순서를 따릅니다</param>
    /// <returns>폴더와 파일 목록. <see cref="ValidateShot"/>을 통과하지 않거나 videoExtension이 비면 null입니다</returns>
    /// <remarks>
    /// 파일 이름은 날짜_시각_역할.확장자입니다. 영상(전체 화면, 왼발, 오른발)의 확장자는 videoExtension을 쓰고,
    /// 매니페스트는 roles에 없어도 항상 넣습니다. 역할마다의 이름 규칙은 한 함수(NameRuleOf)에 있고,
    /// <see cref="UsesStemPrefix"/>가 false인 역할은 날짜_시각 없이 고정 이름(thermal_left.npy, thermal_right.npy)입니다.
    /// 촬영 데이터를 올리는 NAS 서버의 구조와 같습니다. 기존 앱은 날짜/이름_생년월일/검사 기준/시각이었고(표 11),
    /// 그 구조로 저장한 촬영은 <see cref="SplitShotFolderPath"/>가 함께 읽습니다.
    /// 2026-10-30 전에는 가운데 단계가 검사 기준(수축기 혈압 대비 %, 0, 40, 80, 120)이었습니다. 커프 없이 찍은 촬영은
    /// 전과 같이 0입니다. SplitShotFolderPath는 가운데 단계의 이름을 따지지 않아 두 폴더를 모두 읽습니다.
    /// </remarks>
    public static ShotLayout? LayoutOf(ShotInfo info, string videoExtension, IReadOnlyList<FileRole> roles)
    {
        if (ValidateShot(info).Count != 0 || videoExtension.Length == 0)
        {
            return null;
        }
        string stem = CFormat.Int(info.Year) + CFormat.ZeroPad(info.Month, 2) + CFormat.ZeroPad(info.Day, 2) + "_" +
                      CFormat.ZeroPad(info.Hour, 2) + CFormat.ZeroPad(info.Minute, 2) + CFormat.ZeroPad(info.Second, 2);
        // NAS 서버에 올리는 구조와 같게 둡니다. 다르면 촬영마다 사람이 폴더를 옮겨야 합니다(표 11).
        string folder = info.PatientName + "_" + info.BirthDate + "/" + info.AnkleLevel + "/" + stem;
        // 파일 이름의 left, right는 환자 기준입니다. 영문으로 쓰는 이유는 압축이나 메일에서 한글 파일 이름이 깨지는 환경이 있기 때문입니다.
        var layout = new ShotLayout { Folder = folder, Stem = stem };
        var files = new List<ShotFile>();
        bool hasManifest = false;
        foreach (FileRole role in roles)
        {
            if (files.Exists(f => f.Role == role))
            {
                continue;
            }
            hasManifest = hasManifest || role == FileRole.Manifest;
            NameRule rule = NameRuleOf(role, videoExtension);
            files.Add(new ShotFile(role, rule.StemPrefix ? stem + rule.Text : rule.Text));
        }
        if (!hasManifest)
        {
            files.Add(new ShotFile(FileRole.Manifest, stem + NameRuleOf(FileRole.Manifest, "").Text));
        }
        return layout with { Files = files };
    }

    /// <summary>
    /// 같은 폴더가 이미 있을 때 쓸 번호 붙은 배치를 만듭니다.
    /// </summary>
    /// <param name="layout"><see cref="LayoutOf"/>의 결과</param>
    /// <param name="number">2 이상의 번호. 2보다 작으면 layout을 그대로 돌려줍니다</param>
    /// <returns>번호 붙은 배치</returns>
    /// <remarks>
    /// 폴더의 마지막 이름과 날짜_시각으로 시작하는 파일 이름의 앞부분이 날짜_시각_번호가 됩니다
    /// (예: 20260915_142233_2, 20260915_142233_2_full.mkv).
    /// <see cref="UsesStemPrefix"/>가 false인 역할의 이름(thermal_left.npy)은 바꾸지 않습니다.
    /// 기존 앱은 같은 초에 다시 찍으면 앞 촬영을 덮어썼습니다. 이 앱은 덮어쓰지 않고 번호를 붙입니다.
    /// </remarks>
    public static ShotLayout NumberLayout(ShotLayout layout, int number)
    {
        if (number < 2)
        {
            return layout;
        }
        string stem = layout.Stem + "_" + CFormat.Int(number);
        // 직접 만든 배치에서 Stem이 Folder보다 길면 원본은 부호 없는 뺄셈이 감겨 폴더 전체를 그대로 씁니다.
        string parent = layout.Folder.Length >= layout.Stem.Length
            ? layout.Folder.Substring(0, layout.Folder.Length - layout.Stem.Length)
            : layout.Folder;
        string folder = parent + stem;
        var files = new List<ShotFile>();
        foreach (ShotFile f in layout.Files)
        {
            files.Add(UsesStemPrefix(f.Role) ? f with { Name = stem + f.Name.Substring(layout.Stem.Length) } : f);
        }
        return layout with { Stem = stem, Folder = folder, Files = files };
    }

    /// <summary>
    /// 촬영 폴더의 매니페스트(JSON)를 만듭니다. 환자 정보, 촬영 시각, 화소 왼쪽 발, 조명, 열화상 녹화 여부, 장치 목록,
    /// 파일 목록, 분석 입력 역할 목록을 한 번만 담습니다.
    /// </summary>
    /// <param name="info">촬영 정보</param>
    /// <param name="layout"><see cref="LayoutOf"/> 또는 <see cref="NumberLayout"/>의 결과</param>
    /// <param name="devices">이번 촬영에 쓴 장치. 스트림 종류(rgb, thermal)와 이름을 남깁니다</param>
    /// <returns>JSON 문자열. 같은 입력에는 같은 바이트를 냅니다. 파일에 쓸 때는 BOM 없는 UTF-8로 인코딩합니다</returns>
    /// <remarks>
    /// 열화상 녹화 여부(thermal_recorded)는 배치에 열화상 원시값 역할이 있는지로 정합니다. 없으면 thermal_absent_reason에
    /// info.ThermalAbsence("disabled", "no_device", "not_receiving", "preparing", 정하지 않았으면 "unknown")와
    /// thermal_skip_confirmed를 씁니다.
    /// 매니페스트는 녹화 전에 쓰므로 파일 목록과 분석 입력 목록은 이 촬영이 만들 파일입니다. 실제로 만들었는지는 파일이
    /// 있는지로 확인합니다(<c>CheckAnalysisInputs</c>).
    /// </remarks>
    public static string BuildManifestJson(ShotInfo info, ShotLayout layout, IReadOnlyList<ShotDevice> devices)
    {
        // 2: pixel_left_foot, analysis_inputs 추가. 3: lighting, thermal_recorded, thermal_absent_reason, thermal_skip_confirmed 추가
        // 4: app(version, commit), spec(file, sha256) 추가. 5: attempt, foot_warmth 추가. 6: blood_pressure(arm, ankle, ankle_level) 추가
        // 7: criterion(검사 기준) 뺌. 커프 압력은 blood_pressure.ankle_level이 남깁니다
        var output = new StringBuilder("{\n  \"schema_version\": 7,\n  \"patient_name\": ");
        AppendQuoted(output, info.PatientName);
        output.Append(",\n  \"birth_date\": ");
        AppendQuoted(output, info.BirthDate);
        output.Append(",\n  \"blood_pressure\": {\"arm\": ");
        AppendPressure(output, info.ArmPressure);
        output.Append(", \"ankle\": ");
        AppendPressure(output, info.AnklePressure);
        output.Append(", \"ankle_level\": {\"name\": ");
        AppendQuoted(output, info.AnkleLevel);
        double? levelMmhg = AnkleLevelMmhg(info.AnkleLevel, info.AnklePressure);
        output.Append(", \"pressure_mmhg\": ").Append(levelMmhg.HasValue ? HalfMmhgText(levelMmhg.Value) : "null").Append("}}");
        string time = CFormat.ZeroPad(info.Year, 4) + "-" + CFormat.ZeroPad(info.Month, 2) + "-" + CFormat.ZeroPad(info.Day, 2) + "T" +
                      CFormat.ZeroPad(info.Hour, 2) + ":" + CFormat.ZeroPad(info.Minute, 2) + ":" + CFormat.ZeroPad(info.Second, 2);
        output.Append(",\n  \"shot_time\": ");
        AppendQuoted(output, time);
        output.Append(",\n  \"attempt\": ");
        output.Append(info.Attempt > 0 ? CFormat.Int(info.Attempt) : "null");
        // 빈 문자열은 모르는 값이라 null로 씁니다.
        void QuotedOrNull(string text)
        {
            if (text.Length == 0)
            {
                output.Append("null");
            }
            else
            {
                AppendQuoted(output, text);
            }
        }
        output.Append(",\n  \"app\": {\"version\": ");
        QuotedOrNull(info.AppVersion);
        output.Append(", \"commit\": ");
        QuotedOrNull(info.AppCommit);
        output.Append("},\n  \"spec\": {\"file\": ");
        QuotedOrNull(info.SpecFile);
        output.Append(", \"sha256\": ");
        QuotedOrNull(info.SpecSha256);
        output.Append('}');
        output.Append(",\n  \"pixel_left_foot\": ");
        AppendQuoted(output, info.PixelLeftFoot == FootSide.Left ? "left" : "right");
        output.Append(",\n  \"lighting\": {\"source\": ");
        AppendQuoted(output, LightSourceName(info.Lighting.Source));
        output.Append(", \"product\": ");
        AppendQuoted(output, info.Lighting.Product);
        output.Append(", \"brightness_percent\": ");
        output.Append(info.Lighting.BrightnessPercent.HasValue ? CFormat.Int(info.Lighting.BrightnessPercent.Value) : "null");
        // 열화상이 없는 촬영을 장치 목록만으로 알아보기 어렵기 때문에 녹화 여부와 이유를 따로 씁니다.
        bool thermalRecorded = layout.NameOf(FileRole.ThermalRaw).Length != 0;
        output.Append("},\n  \"thermal_recorded\": ");
        output.Append(thermalRecorded ? "true" : "false");
        if (thermalRecorded && info.FootWarmth is FootWarmthRecord w)
        {
            output.Append(",\n  \"foot_warmth\": {\"left\": ").Append(CFormat.Int(w.Left))
                .Append(", \"right\": ").Append(CFormat.Int(w.Right))
                .Append(", \"min_warm\": ").Append(CFormat.Int(w.MinWarm))
                .Append(", \"cold_confirmed\": ").Append(w.ColdConfirmed ? "true" : "false").Append('}');
        }
        if (!thermalRecorded)
        {
            output.Append(",\n  \"thermal_absent_reason\": ");
            AppendQuoted(output, AbsenceName(info.ThermalAbsence));
            output.Append(",\n  \"thermal_skip_confirmed\": ");
            output.Append(info.ThermalSkipConfirmed ? "true" : "false");
        }
        output.Append(",\n  \"devices\": [");
        for (int i = 0; i < devices.Count; i++)
        {
            output.Append(i == 0 ? "\n    {\"stream\": " : ",\n    {\"stream\": ");
            AppendQuoted(output, StreamName(devices[i].Stream));
            output.Append(", \"name\": ");
            AppendQuoted(output, devices[i].Name);
            output.Append('}');
        }
        output.Append(devices.Count == 0 ? "]" : "\n  ]");
        output.Append(",\n  \"folder\": ");
        AppendQuoted(output, layout.Folder);
        output.Append(",\n  \"files\": {");
        for (int i = 0; i < layout.Files.Count; i++)
        {
            output.Append(i == 0 ? "\n    " : ",\n    ");
            AppendQuoted(output, RoleName(layout.Files[i].Role));
            output.Append(": ");
            AppendQuoted(output, layout.Files[i].Name);
        }
        output.Append("\n  },\n  \"analysis_inputs\": [");
        bool first = true;
        foreach (ShotFile f in layout.Files)
        {
            if (!IsAnalysisInput(f.Role))
            {
                continue;
            }
            output.Append(first ? "" : ", ");
            first = false;
            AppendQuoted(output, RoleName(f.Role));
        }
        output.Append("]\n}\n");
        return output.ToString();
    }

    /// <summary>
    /// 촬영 폴더 경로를 저장 위치와 폴더 배치로 나눕니다.
    /// </summary>
    /// <param name="path">촬영 폴더 경로. 구분자는 '/'와 '\\'를 모두 받고, 끝 구분자는 무시합니다</param>
    /// <returns>나눈 결과. Layout.Folder의 구분자는 '/'입니다. 경로의 단계가 배치의 단계보다 적으면 null입니다</returns>
    /// <remarks>
    /// 마지막 단계가 날짜_시각(YYYYMMDD_HHMMSS)이거나 그 뒤에 _번호가 붙은 모양이면 지금 배치(이름_생년월일/기준/날짜_시각)로 보고
    /// 마지막 세 단계를, 아니면 이전 배치(날짜/이름_생년월일/기준/시각)로 보고 마지막 네 단계를 Layout.Folder로, 그 앞을 저장 위치로 봅니다.
    /// 분석이 끝난 촬영의 결과를 분석결과 폴더로 모을 때 씁니다. 그 사이 화면에서 저장 위치를 바꿔도 촬영 폴더의
    /// 저장 위치를 씁니다. 이전 배치도 읽는 이유는 그 배치로 저장한 촬영을 결과 창과 다시 분석이 열기 위함입니다.
    /// </remarks>
    public static ShotFolderPath? SplitShotFolderPath(string path)
    {
        string trimmed = path.TrimEnd('/', '\\');
        var parts = new List<string>();
        int levels = 4;
        int cut = trimmed.Length;
        while (parts.Count < levels)
        {
            int sep = cut == 0 ? -1 : trimmed.LastIndexOfAny(new[] { '/', '\\' }, cut - 1);
            int begin = sep == -1 ? 0 : sep + 1;
            if (begin >= cut)
            {
                return null;  // 빈 단계("a//b")이거나 단계가 모자람
            }
            parts.Add(trimmed.Substring(begin, cut - begin));
            if (parts.Count == 1 && IsDateTimeStem(parts[0]))
            {
                levels = 3;
            }
            if (sep == -1)
            {
                cut = 0;
                break;
            }
            cut = sep;
        }
        if (parts.Count < levels)
        {
            return null;
        }
        string folder = parts[levels - 1];
        for (int i = levels - 1; i > 0; i--)
        {
            folder += "/" + parts[i - 1];
        }
        return new ShotFolderPath
        {
            Root = trimmed.Substring(0, cut),
            Layout = new ShotLayout { Stem = parts[0], Folder = folder },
        };
    }

    /// <summary>
    /// 분석결과 폴더에 모으는 결과 파일 이름의 앞부분을 만듭니다.
    /// </summary>
    /// <param name="layout"><see cref="LayoutOf"/> 또는 <see cref="NumberLayout"/>의 결과</param>
    /// <param name="reanalysis">몇 번째 모음인지. 1 이하이면 번호를 더하지 않고, 2 이상이면 끝에 <c>_reanalysis번호</c>를 더합니다</param>
    /// <returns>앞부분. layout.Folder가 비면 빈 문자열입니다</returns>
    /// <remarks>
    /// 촬영 폴더 경로의 단계(이름_생년월일, 발목 혈압 단계, 날짜_시각, 같은 초 재촬영 번호)를 '_'로 이은 것입니다.
    /// 예: 폴더 시험_19551103/0/20261020_102528이면 시험_19551103_0_20261020_102528, 번호 2이면 끝이 102528_2입니다.
    /// 결과 파일은 이 앞부분 뒤에 관류 분석 프로그램이 정한 뒷부분(_summary.json, _perfusion.gif 등)을 이어 씁니다.
    /// 같은 촬영을 다시 분석해 내용이 다른 결과는 덮어쓰지 않고 앞부분 끝에 _reanalysis2, _reanalysis3을 더한 이름으로 모읍니다.
    /// 날짜_시각만 쓰면 다른 환자의 같은 시각 촬영이 같은 이름이 되므로 폴더 경로 전체를 씁니다.
    /// 이전 배치(날짜/이름_생년월일/기준/시각)로 저장한 촬영은 그 단계 순서대로 날짜_이름_생년월일_기준_시각이 되므로,
    /// results 폴더에는 두 순서의 이름이 함께 있습니다.
    /// </remarks>
    public static string CollectedResultPrefix(ShotLayout layout, int reanalysis = 1)
    {
        string prefix = layout.Folder.Replace('/', '_');
        if (prefix.Length != 0 && reanalysis >= 2)
        {
            prefix += "_reanalysis" + CFormat.Int(reanalysis);
        }
        return prefix;
    }

    private static bool IsDigits(string text, int count)
    {
        if (text.Length != count)
        {
            return false;
        }
        foreach (char c in text)
        {
            if (c < '0' || c > '9')
            {
                return false;
            }
        }
        return true;
    }

    // 지금 배치의 마지막 단계는 날짜_시각(같은 초 재촬영이면 뒤에 _번호)이고, 이전 배치의 마지막 단계는 시각으로 시작합니다.
    private static bool IsDateTimeStem(string stem) =>
        stem.Length >= 15 && IsDigits(stem.Substring(0, 8), 8) && stem[8] == '_' && IsDigits(stem.Substring(9, 6), 6) &&
        (stem.Length == 15 || stem[15] == '_');

    private static bool IsValidDate(int y, int m, int d)
    {
        if (y < 1900 || y > 2999 || m < 1 || m > 12 || d < 1)
        {
            return false;
        }
        ReadOnlySpan<int> days = stackalloc int[] { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
        return d <= days[m - 1] + (m == 2 && leap ? 1 : 0);
    }

    // 이름은 폴더 이름이 되므로 Windows가 거부하는 문자를 막습니다. 한글 같은 비ASCII 문자는 허용합니다.
    private static bool IsSafeName(string name)
    {
        foreach (char c in name)
        {
            if (c < 0x20 || c == 0x7F)
            {
                return false;
            }
            switch (c)
            {
                case '<':
                case '>':
                case ':':
                case '"':
                case '/':
                case '\\':
                case '|':
                case '?':
                case '*':
                    return false;
            }
        }
        return name[^1] != '.' && name[^1] != ' ' && name[0] != ' ';
    }

    private static bool IsValidPressure(BloodPressure pressure) =>
        pressure.DiastolicMmhg >= BloodPressure.MinMmhg && pressure.SystolicMmhg <= BloodPressure.MaxMmhg &&
        pressure.SystolicMmhg > pressure.DiastolicMmhg;

    private static void AppendPressure(StringBuilder output, BloodPressure pressure) =>
        output.Append("{\"systolic_mmhg\": ").Append(CFormat.Int(pressure.SystolicMmhg))
            .Append(", \"diastolic_mmhg\": ").Append(CFormat.Int(pressure.DiastolicMmhg)).Append('}');

    // 발목 혈압 단계의 압력은 0.5 단위입니다. 로캘에 따라 소수점 글자가 바뀌는 서식 대신 정수로 씁니다.
    private static string HalfMmhgText(double mmhg)
    {
        long halves = (long)Math.Round(mmhg * 2, MidpointRounding.AwayFromZero);
        return CFormat.Int(halves / 2) + (halves % 2 != 0 ? ".5" : "");
    }

    private static string RoleName(FileRole role) => role switch
    {
        FileRole.LeftVideo => "left_video",
        FileRole.LeftSidecar => "left_sidecar",
        FileRole.RightVideo => "right_video",
        FileRole.RightSidecar => "right_sidecar",
        FileRole.ThermalRaw => "thermal_raw",
        FileRole.ThermalSidecar => "thermal_sidecar",
        FileRole.ThermalPreview => "thermal_preview",
        FileRole.Manifest => "manifest",
        FileRole.FullVideo => "full_video",
        FileRole.FullSidecar => "full_sidecar",
        FileRole.ThermalLeftArray => "thermal_left_array",
        FileRole.ThermalRightArray => "thermal_right_array",
        _ => "",
    };

    private static string StreamName(StreamKind stream) => stream switch
    {
        StreamKind.Rgb => "rgb",
        StreamKind.Thermal => "thermal",
        _ => "",
    };

    // 역할마다의 파일 이름 규칙입니다. 이름을 바꿀 때는 이 함수만 고칩니다. 영상은 확장자를 코덱에서 받습니다.
    private readonly record struct NameRule(bool StemPrefix, string Text);

    private static NameRule NameRuleOf(FileRole role, string videoExtension) => role switch
    {
        FileRole.FullVideo => new NameRule(true, "_full." + videoExtension),
        FileRole.FullSidecar => new NameRule(true, "_full.json"),
        FileRole.LeftVideo => new NameRule(true, "_left." + videoExtension),
        FileRole.LeftSidecar => new NameRule(true, "_left.json"),
        FileRole.RightVideo => new NameRule(true, "_right." + videoExtension),
        FileRole.RightSidecar => new NameRule(true, "_right.json"),
        FileRole.ThermalRaw => new NameRule(true, "_thermal.raw16"),
        FileRole.ThermalSidecar => new NameRule(true, "_thermal.json"),
        FileRole.ThermalPreview => new NameRule(true, "_thermal.avi"),
        FileRole.Manifest => new NameRule(true, "_shot.json"),
        // 관류 분석 코드가 이 두 이름을 고정해 읽습니다.
        FileRole.ThermalLeftArray => new NameRule(false, "thermal_left.npy"),
        FileRole.ThermalRightArray => new NameRule(false, "thermal_right.npy"),
        _ => new NameRule(true, ""),
    };

    private static string LightSourceName(LightSource source) => source switch
    {
        LightSource.Unknown => "unknown",
        LightSource.External => "external",
        LightSource.App => "app",
        _ => "",
    };

    private static string AbsenceName(ThermalAbsence absence) => absence switch
    {
        ThermalAbsence.None => "unknown",
        ThermalAbsence.Disabled => "disabled",
        ThermalAbsence.NoDevice => "no_device",
        ThermalAbsence.NotReceiving => "not_receiving",
        ThermalAbsence.Preparing => "preparing",
        _ => "",
    };

    private static void AppendQuoted(StringBuilder output, string text)
    {
        output.Append('"');
        foreach (char c in text)
        {
            // 장치 이름은 OS가 준 값이라 제어 문자가 섞여도 JSON이 깨지지 않게 합니다.
            if (c < 0x20)
            {
                output.Append("\\u").Append(CFormat.HexLower(c, 4));
                continue;
            }
            if (c == '"' || c == '\\')
            {
                output.Append('\\');
            }
            output.Append(c);
        }
        output.Append('"');
    }
}
