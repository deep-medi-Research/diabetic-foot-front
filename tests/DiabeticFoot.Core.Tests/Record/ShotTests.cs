using DiabeticFoot.Core.Record;
using static DiabeticFoot.Core.Record.RecordFunctions;

namespace DiabeticFoot.Core.Tests.Record;

public class ShotTests
{
    private static readonly FileRole[] FullFrame = { FileRole.FullVideo, FileRole.FullSidecar };

    private static readonly FileRole[] AllRoles =
    {
        FileRole.LeftVideo, FileRole.LeftSidecar, FileRole.RightVideo, FileRole.RightSidecar,
        FileRole.ThermalRaw, FileRole.ThermalSidecar, FileRole.ThermalPreview, FileRole.Manifest,
    };

    private static readonly FileRole[] AnalysisRoles =
    {
        FileRole.LeftVideo, FileRole.LeftSidecar, FileRole.RightVideo, FileRole.RightSidecar,
        FileRole.ThermalRaw, FileRole.ThermalSidecar, FileRole.ThermalLeftArray, FileRole.ThermalRightArray,
    };

    private static ShotInfo MakeShot() => new()
    {
        PatientName = "홍길동",
        BirthDate = "19551103",
        ArmPressure = new BloodPressure(120, 80),
        AnklePressure = new BloodPressure(130, 85),
        AnkleLevel = "M",
        Year = 2026,
        Month = 9,
        Day = 15,
        Hour = 14,
        Minute = 22,
        Second = 33,
    };

    private static ShotLayout Layout(ShotInfo info, string extension, IReadOnlyList<FileRole> roles)
    {
        ShotLayout? layout = LayoutOf(info, extension, roles);
        Assert.NotNull(layout);
        return layout;
    }

    // NAS 서버와 같은 이름_생년월일/발목 혈압 단계/날짜_시각 폴더를 만듭니다.
    [Fact]
    public void BuildsFolderLikeNas()
    {
        var layout = Layout(MakeShot(), "avi", AllRoles);

        Assert.Equal("홍길동_19551103/M/20260915_142233", layout.Folder);
        Assert.Equal("20260915_142233", layout.Stem);
        Assert.Equal("20260915_142233_left.avi", layout.NameOf(FileRole.LeftVideo));
        Assert.Equal("20260915_142233_thermal.raw16", layout.NameOf(FileRole.ThermalRaw));
        Assert.Equal("20260915_142233_shot.json", layout.NameOf(FileRole.Manifest));
    }

    // 영상 확장자는 코덱에서 받습니다. 기본 코덱 FFV1은 mkv이고, 열화상 확인용 영상은 avi 그대로입니다.
    [Fact]
    public void VideoExtensionFollowsCodec()
    {
        var layout = Layout(MakeShot(), "mkv", AllRoles);

        Assert.Equal("20260915_142233_left.mkv", layout.NameOf(FileRole.LeftVideo));
        Assert.Equal("20260915_142233_right.mkv", layout.NameOf(FileRole.RightVideo));
        Assert.Equal("20260915_142233_thermal.avi", layout.NameOf(FileRole.ThermalPreview));
        Assert.Null(LayoutOf(MakeShot(), "", AllRoles));
    }

    // 전체 화면 녹화는 영상, 사이드카, 매니페스트 세 파일이고, 매니페스트는 요청하지 않아도 들어갑니다.
    [Fact]
    public void FullFrameShotHasThreeFiles()
    {
        var layout = Layout(MakeShot(), "mkv", FullFrame);

        Assert.Equal(3, layout.Files.Count);
        Assert.Equal("20260915_142233_full.mkv", layout.NameOf(FileRole.FullVideo));
        Assert.Equal("20260915_142233_full.json", layout.NameOf(FileRole.FullSidecar));
        Assert.Equal("20260915_142233_shot.json", layout.NameOf(FileRole.Manifest));
        Assert.Equal("", layout.NameOf(FileRole.LeftVideo));
    }

    // 같은 폴더가 있으면 폴더와 모든 파일 이름의 앞부분에 같은 번호를 붙입니다.
    [Fact]
    public void NumberedLayoutRenamesFolderAndFiles()
    {
        var layout = Layout(MakeShot(), "mkv", FullFrame);

        var second = NumberLayout(layout, 2);

        Assert.Equal("홍길동_19551103/M/20260915_142233_2", second.Folder);
        Assert.Equal("20260915_142233_2", second.Stem);
        Assert.Equal("20260915_142233_2_full.mkv", second.NameOf(FileRole.FullVideo));
        Assert.Equal("20260915_142233_2_full.json", second.NameOf(FileRole.FullSidecar));
        Assert.Equal("20260915_142233_2_shot.json", second.NameOf(FileRole.Manifest));
        Assert.Equal(layout.Folder, NumberLayout(layout, 1).Folder);
        Assert.Equal("20260915_142233_12_full.mkv", NumberLayout(layout, 12).NameOf(FileRole.FullVideo));
    }

    // 폴더 이름이 되는 이름에 Windows가 거부하는 문자가 있으면 만들지 않습니다.
    [Theory]
    [InlineData("홍/길동")]
    [InlineData("a:b")]
    [InlineData("홍길동.")]
    [InlineData(" 홍길동")]
    [InlineData("a\tb")]
    [InlineData("a*b")]
    public void RejectsUnsafeName(string name)
    {
        ShotInfo shot = MakeShot() with { PatientName = name };

        var problems = ValidateShot(shot);

        var problem = Assert.Single(problems);
        Assert.Equal(ShotProblem.BadNameCharacter, problem);
        Assert.Null(LayoutOf(shot, "mkv", FullFrame));
    }

    // 없는 날짜는 거부합니다.
    [Fact]
    public void RejectsBadDates()
    {
        ShotInfo shot = MakeShot() with { BirthDate = "19550230", Month = 13 };

        var problems = ValidateShot(shot);

        Assert.Equal(new[] { ShotProblem.BadBirthDate, ShotProblem.BadTime }, problems);
    }

    // 여덟 자리가 아닌 생년월일은 거부합니다.
    [Fact]
    public void RejectsShortBirthDate()
    {
        ShotInfo shot = MakeShot() with { BirthDate = "1955110" };

        Assert.Equal(new[] { ShotProblem.BadBirthDate }, ValidateShot(shot));
    }

    // 폴더의 가운데 단계는 고른 발목 혈압 단계입니다. 커프 없이 찍은 촬영은 2026-10-30 전 검사 기준 폴더와 같은 0입니다.
    [Theory]
    [InlineData("0")]
    [InlineData("V")]
    [InlineData("D")]
    [InlineData("M")]
    [InlineData("S")]
    public void FolderMiddleIsAnkleLevel(string level)
    {
        ShotInfo shot = MakeShot() with { AnkleLevel = level };

        var layout = Layout(shot, "mkv", FullFrame);

        Assert.Equal("홍길동_19551103/" + level + "/20260915_142233", layout.Folder);
    }

    // 혈압은 상완과 발목 모두 있어야 하고, 입력하지 않은 0은 거부합니다.
    [Fact]
    public void RejectsMissingPressure()
    {
        ShotInfo shot = MakeShot() with { ArmPressure = default, AnklePressure = new BloodPressure(130, 0) };

        Assert.Equal(new[] { ShotProblem.BadArmPressure, ShotProblem.BadAnklePressure }, ValidateShot(shot));
        Assert.Null(LayoutOf(shot, "mkv", FullFrame));
    }

    // 혈압은 20~300mmHg이고 수축기가 이완기보다 커야 합니다. 범위의 끝 값은 받습니다.
    [Fact]
    public void ChecksPressureRangeAndOrder()
    {
        ShotInfo shot = MakeShot() with { ArmPressure = new BloodPressure(300, 20), AnklePressure = new BloodPressure(21, 20) };
        Assert.Empty(ValidateShot(shot));

        var arm = new[] { ShotProblem.BadArmPressure };
        foreach (BloodPressure bad in new[] { new BloodPressure(301, 80), new BloodPressure(120, 19), new BloodPressure(80, 80), new BloodPressure(80, 120) })
        {
            shot = shot with { ArmPressure = bad };
            Assert.True(arm.SequenceEqual(ValidateShot(shot)), $"{bad.SystolicMmhg}/{bad.DiastolicMmhg}");
        }
    }

    // 발목 혈압 단계의 압력은 발목 혈압으로 계산합니다. M은 0.5 단위가 될 수 있습니다.
    [Fact]
    public void ComputesAnkleLevelFromAnklePressure()
    {
        var ankle = new BloodPressure(135, 84);

        Assert.Equal(0.0, AnkleLevelMmhg("0", ankle) ?? -1);
        Assert.Equal(34.0, AnkleLevelMmhg("V", ankle) ?? -1);
        Assert.Equal(84.0, AnkleLevelMmhg("D", ankle) ?? -1);
        Assert.Equal(109.5, AnkleLevelMmhg("M", ankle) ?? -1);
        Assert.Equal(135.0, AnkleLevelMmhg("S", ankle) ?? -1);
        Assert.Null(AnkleLevelMmhg("", ankle));
        Assert.Null(AnkleLevelMmhg("d", ankle));
    }

    // 발목 혈압 단계는 골라야 합니다. 발목 이완기가 50mmHg보다 낮아 V가 음수이면 거부합니다.
    [Fact]
    public void RejectsMissingOrNegativeAnkleLevel()
    {
        ShotInfo shot = MakeShot();
        var unknown = new[] { ShotProblem.UnknownAnkleLevel };
        foreach (string level in new[] { "", "x" })
        {
            shot = shot with { AnkleLevel = level };
            Assert.True(unknown.SequenceEqual(ValidateShot(shot)), level);
        }

        shot = shot with { AnkleLevel = "V", AnklePressure = new BloodPressure(120, 50) };
        Assert.Empty(ValidateShot(shot));

        shot = shot with { AnklePressure = new BloodPressure(120, 49) };
        Assert.Equal(new[] { ShotProblem.NegativeAnkleLevel }, ValidateShot(shot));
        Assert.Null(LayoutOf(shot, "mkv", FullFrame));
    }

    // 윤년 2월 29일은 받습니다.
    [Fact]
    public void AcceptsLeapDay()
    {
        ShotInfo shot = MakeShot() with { BirthDate = "20000229" };

        Assert.Empty(ValidateShot(shot));
    }

    // 매니페스트는 공통 정보와 파일 목록을 한 번만 담습니다.
    [Fact]
    public void ManifestListsFilesOnce()
    {
        ShotInfo shot = MakeShot();
        var layout = Layout(shot, "avi", AllRoles);

        string json = BuildManifestJson(shot, layout, new[] { new ShotDevice(StreamKind.Rgb, "See3CAM_24CUG") });

        Assert.Contains("\"patient_name\": \"홍길동\"", json);
        Assert.Contains("\"shot_time\": \"2026-09-15T14:22:33\"", json);
        Assert.Contains("\"right_sidecar\": \"20260915_142233_right.json\"", json);
        // 이름은 patient_name과 folder에만 나옵니다.
        int count = 0;
        for (int at = json.IndexOf("홍길동", StringComparison.Ordinal); at != -1; at = json.IndexOf("홍길동", at + 1, StringComparison.Ordinal)) count++;
        Assert.Equal(2, count);
    }

    // 같은 기준의 몇 번째 촬영인지와 녹화 전 발 대비를 남깁니다. 발 대비는 열화상을 녹화한 촬영에만 씁니다.
    [Fact]
    public void ManifestKeepsAttemptAndFootWarmth()
    {
        ShotInfo shot = MakeShot() with { Attempt = 2, FootWarmth = new FootWarmthRecord(38, 72, 60, true) };
        var withThermal = Layout(shot, "mkv", AllRoles);
        string json = BuildManifestJson(shot, withThermal, new[] { new ShotDevice(StreamKind.Rgb, "See3CAM_24CUG") });
        Assert.Contains("\"attempt\": 2,", json);
        Assert.Contains("\"foot_warmth\": {\"left\": 38, \"right\": 72, \"min_warm\": 60, \"cold_confirmed\": true}", json);
        var rgbOnly = Layout(shot, "mkv", FullFrame);
        Assert.DoesNotContain("foot_warmth", BuildManifestJson(shot, rgbOnly, Array.Empty<ShotDevice>()));
    }

    // 매니페스트의 장치는 스트림 종류와 이름의 목록이고, 전체 바이트가 정해진 형식과 같습니다.
    [Fact]
    public void ManifestBytesWithDeviceList()
    {
        ShotInfo shot = MakeShot();
        var layout = Layout(shot, "mkv", FullFrame);
        var devices = new[] { new ShotDevice(StreamKind.Rgb, "See3CAM_24CUG"), new ShotDevice(StreamKind.Thermal, "TE-EV1") };

        string json = BuildManifestJson(shot, NumberLayout(layout, 2), devices);

        string expected =
            "{\n" +
            "  \"schema_version\": 7,\n" +
            "  \"patient_name\": \"홍길동\",\n" +
            "  \"birth_date\": \"19551103\",\n" +
            "  \"blood_pressure\": {\"arm\": {\"systolic_mmhg\": 120, \"diastolic_mmhg\": 80}, " +
            "\"ankle\": {\"systolic_mmhg\": 130, \"diastolic_mmhg\": 85}, " +
            "\"ankle_level\": {\"name\": \"M\", \"pressure_mmhg\": 107.5}},\n" +
            "  \"shot_time\": \"2026-09-15T14:22:33\",\n" +
            "  \"attempt\": null,\n" +
            "  \"app\": {\"version\": null, \"commit\": null},\n" +
            "  \"spec\": {\"file\": null, \"sha256\": null},\n" +
            "  \"pixel_left_foot\": \"right\",\n" +
            "  \"lighting\": {\"source\": \"unknown\", \"product\": \"\", \"brightness_percent\": null},\n" +
            "  \"thermal_recorded\": false,\n" +
            "  \"thermal_absent_reason\": \"unknown\",\n" +
            "  \"thermal_skip_confirmed\": false,\n" +
            "  \"devices\": [\n" +
            "    {\"stream\": \"rgb\", \"name\": \"See3CAM_24CUG\"},\n" +
            "    {\"stream\": \"thermal\", \"name\": \"TE-EV1\"}\n" +
            "  ],\n" +
            "  \"folder\": \"홍길동_19551103/M/20260915_142233_2\",\n" +
            "  \"files\": {\n" +
            "    \"full_video\": \"20260915_142233_2_full.mkv\",\n" +
            "    \"full_sidecar\": \"20260915_142233_2_full.json\",\n" +
            "    \"manifest\": \"20260915_142233_2_shot.json\"\n" +
            "  },\n" +
            "  \"analysis_inputs\": []\n" +
            "}\n";
        Assert.Equal(expected, json);
        Assert.Equal(json, BuildManifestJson(shot, NumberLayout(layout, 2), devices));
    }

    // 장치 이름의 따옴표와 제어 문자는 JSON 이스케이프로 씁니다.
    [Fact]
    public void ManifestEscapesDeviceName()
    {
        ShotInfo shot = MakeShot();
        var layout = Layout(shot, "mkv", FullFrame);

        string json = BuildManifestJson(shot, layout, new[] { new ShotDevice(StreamKind.Rgb, "cam\"1\"\n") });

        Assert.Contains("\"name\": \"cam\\\"1\\\"\\u000a\"", json);
    }

    // 열화상 .npy는 시각 접두 없이 관류 분석이 읽는 고정 이름이고, 나머지는 시각으로 시작합니다.
    [Fact]
    public void ThermalArraysHaveFixedNames()
    {
        var layout = Layout(MakeShot(), "mkv", AnalysisRoles);

        Assert.Equal("thermal_left.npy", layout.NameOf(FileRole.ThermalLeftArray));
        Assert.Equal("thermal_right.npy", layout.NameOf(FileRole.ThermalRightArray));
        Assert.Equal("20260915_142233_left.mkv", layout.NameOf(FileRole.LeftVideo));
        Assert.False(UsesStemPrefix(FileRole.ThermalLeftArray));
        Assert.False(UsesStemPrefix(FileRole.ThermalRightArray));
        Assert.True(UsesStemPrefix(FileRole.ThermalRaw));
        Assert.True(UsesStemPrefix(FileRole.Manifest));
    }

    // 번호 폴더에서 시각으로 시작하는 이름만 번호를 넣고 .npy 이름은 그대로 둡니다.
    [Fact]
    public void NumberedLayoutKeepsArrayNames()
    {
        var layout = Layout(MakeShot(), "mkv", AnalysisRoles);

        var third = NumberLayout(layout, 3);

        Assert.Equal("홍길동_19551103/M/20260915_142233_3", third.Folder);
        Assert.Equal("thermal_left.npy", third.NameOf(FileRole.ThermalLeftArray));
        Assert.Equal("thermal_right.npy", third.NameOf(FileRole.ThermalRightArray));
        Assert.Equal("20260915_142233_3_right.mkv", third.NameOf(FileRole.RightVideo));
        Assert.Equal("20260915_142233_3_thermal.raw16", third.NameOf(FileRole.ThermalRaw));
        Assert.Equal("20260915_142233_3_shot.json", third.NameOf(FileRole.Manifest));
    }

    // 분석 입력은 좌우 영상과 좌우 열화상 .npy 네 역할입니다.
    [Fact]
    public void AnalysisInputRoles()
    {
        Assert.True(IsAnalysisInput(FileRole.LeftVideo));
        Assert.True(IsAnalysisInput(FileRole.RightVideo));
        Assert.True(IsAnalysisInput(FileRole.ThermalLeftArray));
        Assert.True(IsAnalysisInput(FileRole.ThermalRightArray));
        Assert.False(IsAnalysisInput(FileRole.FullVideo));
        Assert.False(IsAnalysisInput(FileRole.LeftSidecar));
        Assert.False(IsAnalysisInput(FileRole.ThermalRaw));
    }

    // 매니페스트는 화소 왼쪽 발과 분석 입력 역할 목록(파일 목록 순서)을 남깁니다.
    [Fact]
    public void ManifestListsAnalysisInputsAndFoot()
    {
        ShotInfo shot = MakeShot() with { PixelLeftFoot = FootSide.Left };
        var layout = Layout(shot, "mkv", AnalysisRoles);

        string json = BuildManifestJson(shot, layout, new[] { new ShotDevice(StreamKind.Rgb, "See3CAM_24CUG") });

        Assert.Contains("\"schema_version\": 7,", json);
        Assert.Contains("\"pixel_left_foot\": \"left\",", json);
        Assert.Contains("\"thermal_left_array\": \"thermal_left.npy\"", json);
        Assert.Contains(
            "\"analysis_inputs\": [\"left_video\", \"right_video\", \"thermal_left_array\", " +
            "\"thermal_right_array\"]\n}\n",
            json);
    }

    // 앱 밖에서 켠 조명은 출처와 제품을 남기고 밝기는 null입니다. 열화상 역할이 있으면 녹화 이유를 쓰지 않습니다.
    [Fact]
    public void ManifestWritesExternalLightingAndThermalRecorded()
    {
        ShotInfo shot = MakeShot() with
        {
            Lighting = new LightingInfo { Source = LightSource.External, Product = "4000K COB LED, 12V", BrightnessPercent = null },
            ThermalAbsence = ThermalAbsence.NotReceiving,
        };
        var layout = Layout(shot, "mkv", AnalysisRoles);

        string json = BuildManifestJson(shot, layout, new[] { new ShotDevice(StreamKind.Rgb, "See3CAM_24CUG") });

        Assert.Contains(
            "\"lighting\": {\"source\": \"external\", \"product\": \"4000K COB LED, 12V\", " +
            "\"brightness_percent\": null},\n  \"thermal_recorded\": true,\n  \"devices\"",
            json);
        Assert.DoesNotContain("thermal_absent_reason", json);
    }

    // 열화상 역할이 없는 촬영은 thermal_recorded가 false이고 이유와 사용자 확인 여부를 남깁니다. 앱이 보낸 밝기는 숫자입니다.
    [Fact]
    public void ManifestWritesThermalAbsentReason()
    {
        ShotInfo shot = MakeShot() with
        {
            Lighting = new LightingInfo { Source = LightSource.App, Product = "", BrightnessPercent = 80 },
            ThermalAbsence = ThermalAbsence.Preparing,
            ThermalSkipConfirmed = true,
        };
        var layout = Layout(shot, "mkv", FullFrame);

        string json = BuildManifestJson(shot, layout, new[] { new ShotDevice(StreamKind.Rgb, "See3CAM_24CUG") });

        Assert.Contains("\"source\": \"app\", \"product\": \"\", \"brightness_percent\": 80}", json);
        Assert.Contains(
            "\"thermal_recorded\": false,\n  \"thermal_absent_reason\": \"preparing\",\n" +
            "  \"thermal_skip_confirmed\": true,\n",
            json);
    }

    // 앱 버전, 커밋, 촬영 선언 파일 이름과 SHA-256을 남깁니다. 개발 빌드처럼 버전이 비어 있으면 null입니다.
    [Fact]
    public void ManifestWritesAppAndSpec()
    {
        ShotInfo shot = MakeShot() with { AppCommit = "2c80764", SpecFile = "see3cam_auto_30fps.json", SpecSha256 = "ab12" };
        var layout = Layout(shot, "mkv", FullFrame);
        var devices = new[] { new ShotDevice(StreamKind.Rgb, "See3CAM_24CUG") };

        string json = BuildManifestJson(shot, layout, devices);
        Assert.Contains(
            "\"app\": {\"version\": null, \"commit\": \"2c80764\"},\n" +
            "  \"spec\": {\"file\": \"see3cam_auto_30fps.json\", \"sha256\": \"ab12\"},\n",
            json);

        shot = shot with { AppVersion = "v0.1.2" };
        json = BuildManifestJson(shot, layout, devices);
        Assert.Contains("\"app\": {\"version\": \"v0.1.2\", \"commit\": \"2c80764\"}", json);
    }

    // 분석결과 파일 이름 앞부분은 폴더 경로 단계 전체라, 같은 시각이어도 날짜, 환자, 기준, 재촬영 번호가 다르면 다릅니다.
    [Fact]
    public void CollectedResultPrefixIsUniquePerShotFolder()
    {
        var layout = Layout(MakeShot(), "mkv", AllRoles);
        Assert.Equal("홍길동_19551103_M_20260915_142233", CollectedResultPrefix(layout));
        Assert.Equal("홍길동_19551103_M_20260915_142233_2", CollectedResultPrefix(NumberLayout(layout, 2)));
        ShotInfo otherDay = MakeShot() with { Day = 16 };
        var dayLayout = Layout(otherDay, "mkv", AllRoles);
        Assert.NotEqual(CollectedResultPrefix(dayLayout), CollectedResultPrefix(layout));
        ShotInfo otherPatient = MakeShot() with { PatientName = "김철수" };
        var patientLayout = Layout(otherPatient, "mkv", AllRoles);
        Assert.NotEqual(CollectedResultPrefix(patientLayout), CollectedResultPrefix(layout));
        Assert.Equal("", CollectedResultPrefix(new ShotLayout()));
    }

    // 촬영 폴더 경로의 마지막 세 단계가 폴더 배치이고 그 앞이 저장 위치입니다. 두 구분자를 모두 받습니다.
    [Fact]
    public void SplitShotFolderPathTakesLastThreeLevels()
    {
        var layout = Layout(MakeShot(), "mkv", AllRoles);
        var split = SplitShotFolderPath("C:\\data\\촬영/" + layout.Folder + "/");
        Assert.NotNull(split);
        Assert.Equal("C:\\data\\촬영", split.Root);
        Assert.Equal(layout.Folder, split.Layout.Folder);
        Assert.Equal(layout.Stem, split.Layout.Stem);
        Assert.Equal(CollectedResultPrefix(layout), CollectedResultPrefix(split.Layout));

        var bare = SplitShotFolderPath("a_19551103/0/20260915_142233_2");
        Assert.NotNull(bare);
        Assert.Equal("", bare.Root);
        Assert.Equal("a_19551103/0/20260915_142233_2", bare.Layout.Folder);
        Assert.Equal("20260915_142233_2", bare.Layout.Stem);
        var unc = SplitShotFolderPath("\\\\nas\\share\\a_19551103\\0\\20260915_142233_12");
        Assert.NotNull(unc);
        Assert.Equal("\\\\nas\\share", unc.Root);
        Assert.Equal("a_19551103/0/20260915_142233_12", unc.Layout.Folder);
        Assert.Equal("20260915_142233_12", unc.Layout.Stem);
        Assert.Null(SplitShotFolderPath("0/20260915_142233"));
        Assert.Null(SplitShotFolderPath("a/b/c"));
        Assert.Null(SplitShotFolderPath("x/a//c/d"));
        Assert.Null(SplitShotFolderPath(""));
    }

    // 이전 배치(날짜/이름_생년월일/기준/시각)로 저장한 촬영은 마지막 네 단계를 배치로 보고, 결과 이름 앞부분도 전과 같습니다.
    [Fact]
    public void SplitShotFolderPathReadsPreviousLayout()
    {
        var split = SplitShotFolderPath("C:\\data\\촬영\\20260915\\홍길동_19551103\\40\\142233\\");
        Assert.NotNull(split);
        Assert.Equal("C:\\data\\촬영", split.Root);
        Assert.Equal("20260915/홍길동_19551103/40/142233", split.Layout.Folder);
        Assert.Equal("142233", split.Layout.Stem);
        Assert.Equal("20260915_홍길동_19551103_40_142233", CollectedResultPrefix(split.Layout));

        var bare = SplitShotFolderPath("20260915/a_19551103/0/142233_2");
        Assert.NotNull(bare);
        Assert.Equal("", bare.Root);
        Assert.Equal("142233_2", bare.Layout.Stem);
        Assert.Null(SplitShotFolderPath("a_19551103/0/142233"));
    }

    // 다시 분석한 결과의 앞부분은 끝에 _reanalysis<번호>를 더하고, 번호 1 이하는 처음 모음과 같습니다.
    [Fact]
    public void CollectedResultPrefixNumbersReanalysis()
    {
        var layout = Layout(MakeShot(), "mkv", AllRoles);
        Assert.Equal(CollectedResultPrefix(layout), CollectedResultPrefix(layout, 1));
        Assert.Equal(CollectedResultPrefix(layout), CollectedResultPrefix(layout, 0));
        Assert.Equal("홍길동_19551103_M_20260915_142233_reanalysis2", CollectedResultPrefix(layout, 2));
        Assert.Equal("홍길동_19551103_M_20260915_142233_2_reanalysis3", CollectedResultPrefix(NumberLayout(layout, 2), 3));
        Assert.Equal("", CollectedResultPrefix(new ShotLayout(), 2));
    }
}
