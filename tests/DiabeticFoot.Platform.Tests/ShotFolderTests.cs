using System.Text;
using DiabeticFoot.Core;
using DiabeticFoot.Core.Flow;
using DiabeticFoot.Core.Pixel;
using DiabeticFoot.Core.Record;
using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

// 원본 test_shot_folder.cc의 시험입니다. 저장 위치 이름에도 한글을 넣어 경로 처리를 함께 봅니다.
internal static class ShotFolderFixtures
{
    public static ShotInfo MakeShot() => new()
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

    public static ShotLayout MakeLayout(ShotInfo shot) => RecordFunctions.LayoutOf(shot, "mkv", [FileRole.FullVideo, FileRole.FullSidecar])!;

    // 테스트마다 비어 있는 저장 위치를 씁니다.
    public static string FreshRoot(string name)
    {
        var parent = Path.Combine(Path.GetTempPath(), "diabeticfoot_test_shot_folder", name);
        if (Directory.Exists(parent)) Directory.Delete(parent, recursive: true);
        return Path.Combine(parent, "저장");
    }

    public static string ReadText(string path) => new UTF8Encoding(false).GetString(File.ReadAllBytes(path));

    public static void WriteBytes(string path, string text) => File.WriteAllBytes(path, new UTF8Encoding(false).GetBytes(text));

    public static readonly ShotDevice[] RgbOnly = [new(StreamKind.Rgb, "FakeCamera")];

    public static readonly ShotDevice[] RgbAndThermal = [new(StreamKind.Rgb, "FakeCamera"), new(StreamKind.Thermal, "FakeThermal")];
}

public class ShotFolderTests
{
    private static ShotInfo MakeShot() => ShotFolderFixtures.MakeShot();

    private static ShotLayout MakeLayout(ShotInfo shot) => ShotFolderFixtures.MakeLayout(shot);

    private static string FreshRoot(string name) => ShotFolderFixtures.FreshRoot(name);

    private static string ReadBytes(string path) => ShotFolderFixtures.ReadText(path);

    private static void WriteBytes(string path, string text) => ShotFolderFixtures.WriteBytes(path, text);

    private static readonly ShotDevice[] RgbOnly = ShotFolderFixtures.RgbOnly;

    private static readonly ShotDevice[] RgbAndThermal = ShotFolderFixtures.RgbAndThermal;

    // 한글 이름이 든 촬영 폴더를 단계마다 만들고, 그 안에 파일을 열 수 있습니다.
    [Fact]
    public void CreatesKoreanFolder()
    {
        var root = FreshRoot("korean");
        var layout = MakeLayout(MakeShot());

        Assert.True(ShotFolder.CreateShotFolder(root, layout, out var error), error);

        var folder = Path.Combine(root, "홍길동_19551103", "M", "20260915_142233");
        Assert.True(Directory.Exists(folder));
        var video = ShotFolder.PathOfFile(root, layout, FileRole.FullVideo);
        File.WriteAllBytes(video, []);
        Assert.True(File.Exists(Path.Combine(folder, "20260915_142233_full.mkv")));
    }

    // 같은 환자와 기준으로 다시 찍어도 환자 폴더의 수정 시각이 새 촬영 시각으로 바뀝니다(최근 환자 목록이 이 값을 씁니다).
    [Fact]
    public void TouchesPatientFolder()
    {
        var root = FreshRoot("touch_patient");
        Assert.True(ShotFolder.CreateShotFolder(root, MakeLayout(MakeShot()), out var error), error);
        var patient = Path.Combine(root, "홍길동_19551103");
        var dayAgo = DateTime.UtcNow - TimeSpan.FromHours(24);
        Directory.SetLastWriteTimeUtc(patient, dayAgo);

        var later = MakeShot() with { Second = 34 };
        Assert.True(ShotFolder.CreateShotFolder(root, MakeLayout(later), out error), error);
        Assert.True(Directory.GetLastWriteTimeUtc(patient) > dayAgo + TimeSpan.FromHours(23));
    }

    // 같은 촬영 폴더가 이미 있으면 만들지 않고 이유를 남깁니다. 안에 있던 파일은 그대로입니다.
    [Fact]
    public void RejectsExistingFolder()
    {
        var root = FreshRoot("existing");
        var layout = MakeLayout(MakeShot());
        Assert.True(ShotFolder.CreateShotFolder(root, layout, out var error), error);
        var video = ShotFolder.PathOfFile(root, layout, FileRole.FullVideo);
        WriteBytes(video, "first");

        Assert.False(ShotFolder.CreateShotFolder(root, layout, out error));
        Assert.Contains("이미 있음", error);
        Assert.Equal("first", ReadBytes(video));
    }

    // 같은 초에 다시 찍으면 번호 2, 3을 붙인 폴더를 새로 만듭니다.
    [Fact]
    public void NumbersFolderWhenTaken()
    {
        var root = FreshRoot("numbered");
        var layout = MakeLayout(MakeShot());

        var first = ShotFolder.CreateFreshShotFolder(root, layout, 99, out var error);
        var second = ShotFolder.CreateFreshShotFolder(root, layout, 99, out error);
        var third = ShotFolder.CreateFreshShotFolder(root, layout, 99, out error);

        Assert.True(first is not null && second is not null && third is not null, error);
        Assert.Equal("20260915_142233", first!.Stem);
        Assert.Equal("20260915_142233_2", second!.Stem);
        Assert.Equal("20260915_142233_3", third!.Stem);
        Assert.Equal("20260915_142233_3_full.mkv", third.NameOf(FileRole.FullVideo));
        Assert.Null(ShotFolder.CreateFreshShotFolder(root, layout, 3, out error));
    }

    // 매니페스트는 BuildManifestJson의 바이트를 그대로 쓰고, 이미 있으면 덮어쓰지 않습니다.
    [Fact]
    public void WritesManifestBytes()
    {
        var root = FreshRoot("manifest");
        var shot = MakeShot();
        var layout = MakeLayout(shot);
        var json = RecordFunctions.BuildManifestJson(shot, layout, [new ShotDevice(StreamKind.Rgb, "FakeCamera")]);
        Assert.True(ShotFolder.CreateShotFolder(root, layout, out var error), error);

        Assert.True(ShotFolder.WriteManifest(root, layout, json, out error), error);

        var path = ShotFolder.PathOfFile(root, layout, FileRole.Manifest);
        Assert.Equal(json, ReadBytes(path));
        Assert.Equal(Encoding.UTF8.GetBytes(json), File.ReadAllBytes(path));
        Assert.False(ShotFolder.WriteManifest(root, layout, "{}\n", out error));
        Assert.Equal(json, ReadBytes(path));
    }

    // RGB만 쓰면 영상, 사이드카, 매니페스트만 정하고 열화상 경로는 비웁니다. 매니페스트는 BuildManifestJson의 바이트입니다.
    [Fact]
    public void PreparesRgbOnlyShot()
    {
        var root = FreshRoot("prepare_rgb");
        var shot = MakeShot();

        var prepared = ShotFolder.PrepareShot(root, shot, VideoCodec.Ffv1, SavedVideos.Full, RgbOnly, 99, out var error);

        Assert.NotNull(prepared);
        Assert.True(Directory.Exists(prepared!.Folder));
        Assert.Equal(ShotFolder.PathOfFile(root, prepared.Layout, FileRole.FullVideo), prepared.Video);
        Assert.Equal("20260915_142233_full.mkv", Path.GetFileName(prepared.Video));
        Assert.Equal("20260915_142233_full.json", Path.GetFileName(prepared.Sidecar));
        Assert.Empty(prepared.ThermalRaw);
        Assert.Empty(prepared.ThermalSidecar);
        Assert.Empty(prepared.ThermalPreview);
        Assert.Equal(RecordFunctions.BuildManifestJson(shot, prepared.Layout, RgbOnly), ReadBytes(prepared.Manifest));
        Assert.Equal("", error);
    }

    // 열화상 장치가 있으면 열화상 역할 3개가 생기고, 매니페스트의 장치 목록과 파일 목록에 열화상이 들어갑니다.
    [Fact]
    public void AddsThermalRolesWithThermalDevice()
    {
        var root = FreshRoot("prepare_thermal");

        var prepared = ShotFolder.PrepareShot(root, MakeShot(), VideoCodec.Uncompressed, SavedVideos.Full, RgbAndThermal, 99, out var error);

        Assert.True(prepared is not null, error);
        Assert.Equal("20260915_142233_full.avi", Path.GetFileName(prepared!.Video));
        Assert.Equal("20260915_142233_thermal.raw16", Path.GetFileName(prepared.ThermalRaw));
        Assert.Equal("20260915_142233_thermal.json", Path.GetFileName(prepared.ThermalSidecar));
        Assert.Equal("20260915_142233_thermal.avi", Path.GetFileName(prepared.ThermalPreview));
        Assert.Equal(prepared.Folder, Path.GetDirectoryName(prepared.ThermalRaw));
        var manifest = ReadBytes(prepared.Manifest);
        Assert.Contains("{\"stream\": \"thermal\", \"name\": \"FakeThermal\"}", manifest);
        Assert.Contains("\"thermal_raw\": \"20260915_142233_thermal.raw16\"", manifest);
        Assert.Contains("\"thermal_sidecar\": \"20260915_142233_thermal.json\"", manifest);
        Assert.Contains("\"thermal_preview\": \"20260915_142233_thermal.avi\"", manifest);
    }

    // 같은 초의 촬영 폴더가 이미 있으면 번호를 넣은 폴더에 준비하고, 앞 촬영의 파일은 그대로 둡니다.
    [Fact]
    public void PrepareNumbersFolderWhenTaken()
    {
        var root = FreshRoot("prepare_numbered");
        var first = ShotFolder.PrepareShot(root, MakeShot(), VideoCodec.Ffv1, SavedVideos.Full, RgbAndThermal, 99, out var error);
        Assert.True(first is not null, error);
        var firstManifest = ReadBytes(first!.Manifest);

        var second = ShotFolder.PrepareShot(root, MakeShot(), VideoCodec.Ffv1, SavedVideos.Full, RgbAndThermal, 99, out error);

        Assert.True(second is not null, error);
        Assert.Equal("20260915_142233_2", second!.Layout.Stem);
        Assert.Equal("20260915_142233_2", Path.GetFileName(second.Folder));
        Assert.Equal("20260915_142233_2_full.mkv", Path.GetFileName(second.Video));
        Assert.Equal("20260915_142233_2_thermal.raw16", Path.GetFileName(second.ThermalRaw));
        Assert.Contains("\"folder\": \"홍길동_19551103/M/20260915_142233_2\"", ReadBytes(second.Manifest));
        Assert.Equal(firstManifest, ReadBytes(first.Manifest));
        Assert.Null(ShotFolder.PrepareShot(root, MakeShot(), VideoCodec.Ffv1, SavedVideos.Full, RgbOnly, 2, out error));
        Assert.NotEmpty(error);
    }

    // 촬영 정보가 검증을 통과하지 않으면 폴더를 만들지 않고 이유를 남깁니다.
    [Fact]
    public void RejectsInvalidShotWithoutFolder()
    {
        var root = FreshRoot("prepare_invalid");
        var shot = MakeShot() with { AnkleLevel = "x" };

        Assert.Null(ShotFolder.PrepareShot(root, shot, VideoCodec.Ffv1, SavedVideos.Full, RgbOnly, 99, out var error));
        Assert.NotEmpty(error);
        Assert.False(Directory.Exists(root));
    }

    // 한글 저장 위치와 한글 이름 폴더에 준비한 경로로 파일을 열 수 있습니다.
    [Fact]
    public void PreparesKoreanFolder()
    {
        var root = FreshRoot("prepare_korean");
        var shot = MakeShot() with { PatientName = "김철수 환자" };

        var prepared = ShotFolder.PrepareShot(root, shot, VideoCodec.Ffv1, SavedVideos.Full, RgbAndThermal, 99, out var error);

        Assert.True(prepared is not null, error);
        var folder = Path.Combine(root, "김철수 환자_19551103", "M", "20260915_142233");
        Assert.Equal(Path.GetFullPath(folder), Path.GetFullPath(prepared!.Folder));
        WriteBytes(prepared.ThermalRaw, "raw");
        Assert.True(File.Exists(Path.Combine(folder, "20260915_142233_thermal.raw16")));
        Assert.Contains("\"patient_name\": \"김철수 환자\"", ReadBytes(prepared.Manifest));
    }

    // 영상 없이 끝난 촬영은 매니페스트와 빈 촬영 폴더를 지우고, 상위 폴더는 남깁니다.
    [Fact]
    public void DiscardRemovesManifestAndEmptyFolder()
    {
        var root = FreshRoot("discard_empty");
        var prepared = ShotFolder.PrepareShot(root, MakeShot(), VideoCodec.Ffv1, SavedVideos.Full, RgbAndThermal, 99, out var error);
        Assert.True(prepared is not null, error);

        Assert.True(ShotFolder.DiscardUnrecordedShot(prepared!));

        Assert.False(File.Exists(prepared.Manifest));
        Assert.False(Directory.Exists(prepared.Folder));
        Assert.True(Directory.Exists(Path.GetDirectoryName(prepared.Folder)));
    }

    // 영상 파일이 있는 촬영은 지우지 않습니다.
    [Fact]
    public void DiscardKeepsRecordedShot()
    {
        var root = FreshRoot("discard_recorded");
        var prepared = ShotFolder.PrepareShot(root, MakeShot(), VideoCodec.Ffv1, SavedVideos.Full, RgbOnly, 99, out var error);
        Assert.True(prepared is not null, error);
        WriteBytes(prepared!.Video, "video");

        Assert.False(ShotFolder.DiscardUnrecordedShot(prepared));

        Assert.True(File.Exists(prepared.Manifest));
        Assert.Equal("video", ReadBytes(prepared.Video));
    }

    // 영상이 없어도 매니페스트 말고 다른 파일(열화상 원시값)이 있으면 매니페스트와 폴더를 모두 남깁니다.
    [Fact]
    public void DiscardKeepsFolderWithOtherFiles()
    {
        var root = FreshRoot("discard_other");
        var prepared = ShotFolder.PrepareShot(root, MakeShot(), VideoCodec.Ffv1, SavedVideos.Full, RgbAndThermal, 99, out var error);
        Assert.True(prepared is not null, error);
        var manifest = ReadBytes(prepared!.Manifest);
        WriteBytes(prepared.ThermalRaw, "raw");

        Assert.False(ShotFolder.DiscardUnrecordedShot(prepared));

        Assert.Equal(manifest, ReadBytes(prepared.Manifest));
        Assert.Equal("raw", ReadBytes(prepared.ThermalRaw));
    }

    // 좌우 분할과 열화상 장치가 있으면 좌우 영상, 좌우 .npy 경로가 생기고 전체 화면 영상은 없습니다.
    [Fact]
    public void AddsFeetAndArrayRoles()
    {
        var root = FreshRoot("prepare_feet");
        var shot = MakeShot() with { PixelLeftFoot = FootSide.Left };

        var prepared = ShotFolder.PrepareShot(root, shot, VideoCodec.Ffv1, SavedVideos.Feet, RgbAndThermal, 99, out var error);

        Assert.True(prepared is not null, error);
        Assert.Empty(prepared!.Video);
        Assert.Empty(prepared.Sidecar);
        Assert.Equal("20260915_142233_left.mkv", Path.GetFileName(prepared.LeftVideo));
        Assert.Equal("20260915_142233_right.json", Path.GetFileName(prepared.RightSidecar));
        Assert.Equal("thermal_left.npy", Path.GetFileName(prepared.ThermalLeftArray));
        Assert.Equal("thermal_right.npy", Path.GetFileName(prepared.ThermalRightArray));
        Assert.Equal(FootSide.Left, prepared.PixelLeftFoot);
        var manifest = ReadBytes(prepared.Manifest);
        Assert.Contains("\"pixel_left_foot\": \"left\"", manifest);
        Assert.Contains("\"analysis_inputs\": [\"left_video\", \"right_video\", \"thermal_left_array\", \"thermal_right_array\"]", manifest);

        var both = ShotFolder.PrepareShot(root, shot, VideoCodec.Ffv1, SavedVideos.FullAndFeet, RgbOnly, 99, out error);
        Assert.True(both is not null, error);
        Assert.NotEmpty(both!.Video);
        Assert.NotEmpty(both.LeftVideo);
        Assert.Empty(both.ThermalLeftArray);
    }

    // 번호 폴더에서도 .npy는 접두 없는 이름 그대로 번호 폴더 안에 둡니다.
    [Fact]
    public void NumberedFolderKeepsArrayNames()
    {
        var root = FreshRoot("prepare_numbered_npy");
        Assert.NotNull(ShotFolder.PrepareShot(root, MakeShot(), VideoCodec.Ffv1, SavedVideos.Feet, RgbAndThermal, 99, out _));

        var second = ShotFolder.PrepareShot(root, MakeShot(), VideoCodec.Ffv1, SavedVideos.Feet, RgbAndThermal, 99, out var error);

        Assert.True(second is not null, error);
        Assert.Equal("thermal_left.npy", Path.GetFileName(second!.ThermalLeftArray));
        Assert.Equal("20260915_142233_2", Path.GetFileName(Path.GetDirectoryName(second.ThermalLeftArray)));
        Assert.Equal("20260915_142233_2_left.mkv", Path.GetFileName(second.LeftVideo));
        Assert.Contains("\"thermal_right_array\": \"thermal_right.npy\"", ReadBytes(second.Manifest));
    }

    // 좌우 분할 영상만 있는 촬영(전체 화면 영상 없음)도 영상이 있는 촬영으로 보고 지우지 않습니다.
    [Fact]
    public void DiscardKeepsShotWithOnlyFootVideo()
    {
        var root = FreshRoot("discard_feet");
        var prepared = ShotFolder.PrepareShot(root, MakeShot(), VideoCodec.Ffv1, SavedVideos.Feet, RgbOnly, 99, out var error);
        Assert.True(prepared is not null, error);
        WriteBytes(prepared!.RightVideo, "video");

        Assert.False(ShotFolder.DiscardUnrecordedShot(prepared));

        Assert.True(File.Exists(prepared.Manifest));
        Assert.Equal("video", ReadBytes(prepared.RightVideo));
    }

    // 폴더를 만들기 전에 구한 배치는 PrepareShot이 만든 배치와 같고, 폴더를 만들지 않습니다.
    [Fact]
    public void PlannedLayoutMatchesPreparedShot()
    {
        var root = FreshRoot("limits_plan");

        var planned = ShotFolder.PlanShotLayout(MakeShot(), VideoCodec.Ffv1, SavedVideos.Feet, RgbAndThermal);
        Assert.NotNull(planned);
        Assert.False(Directory.Exists(root));
        var prepared = ShotFolder.PrepareShot(root, MakeShot(), VideoCodec.Ffv1, SavedVideos.Feet, RgbAndThermal, 99, out var error);
        Assert.True(prepared is not null, error);
        Assert.Equal(planned!.Folder, prepared!.Layout.Folder);
        Assert.Equal(planned.Files.Count, prepared.Layout.Files.Count);
        for (var i = 0; i < planned.Files.Count; ++i) Assert.Equal(planned.Files[i].Name, prepared.Layout.Files[i].Name);
    }

    // 아직 없는 저장 위치는 있는 상위 폴더의 디스크 여유를 읽습니다.
    [Fact]
    public void FreeBytesOfMissingRootUsesParent()
    {
        var root = FreshRoot("limits_free");

        var freeBytes = ShotFolder.FreeBytesAt(Path.Combine(root, "없는", "폴더"));

        Assert.NotNull(freeBytes);
        Assert.True(freeBytes > 0);
    }

    // 경로가 상한을 넘거나 디스크 여유가 모자라면 이유를 돌려주고, 상한과 여유를 모르면 검사하지 않습니다.
    [Fact]
    public void ChecksPathLengthAndFreeSpace()
    {
        var layout = MakeLayout(MakeShot());
        const string root = "C:/data";
        var longest = RecordFunctions.LongestShotPathLength(root, layout, layout.Stem);

        Assert.Equal("", ShotFolder.CheckShotLimits(root, layout, 1000, null, longest));
        Assert.Contains("저장 경로가 너무 김", ShotFolder.CheckShotLimits(root, layout, 1000, null, longest - 1));
        Assert.Equal("", ShotFolder.CheckShotLimits(root, layout, 1000, 1000 + ShotFolder.FreeSpaceReserveBytes, null));
        var shortSpace = ShotFolder.CheckShotLimits(root, layout, 1000, 999 + ShotFolder.FreeSpaceReserveBytes, null);
        Assert.Contains("디스크 여유 부족", shortSpace);
    }

    // 10 GB 미만이면 낮음 문장을, 필요한 양 미만이면 부족 문장을 만들고, 낮음은 촬영을 막지 않습니다.
    [Fact]
    public void DescribesLowAndShortFreeSpace()
    {
        var enough = ShotFolder.DescribeFreeSpace(10_000_000_000, 1_000_000_000);
        Assert.Equal(FreeSpace.Enough, enough.Verdict);
        Assert.Equal("", enough.Text);

        var low = ShotFolder.DescribeFreeSpace(9_200_000_000, 1_000_000_000);
        Assert.Equal(FreeSpace.Low, low.Verdict);
        Assert.Equal("저장 공간이 9.2 GB 남았습니다. 10 GB 미만이면 촬영 중 공간이 부족할 수 있습니다. 다른 위치로 옮기거나 공간을 비워 주세요.", low.Text);

        var scarce = ShotFolder.DescribeFreeSpace(1_500_000_000, 1_000_000_000);
        Assert.Equal(FreeSpace.Short, scarce.Verdict);
        Assert.Contains("남은 1.5 GB, 필요 2 GB", scarce.Text);

        var layout = MakeLayout(MakeShot());
        Assert.Equal("", ShotFolder.CheckShotLimits("C:/data", layout, 1000, 9_000_000_000, null));
    }

    // 저장 실패 문장은 남은 공간이 예비량 미만이면 공간 부족을 원인으로 적고, 저장된 폴더를 알립니다.
    [Fact]
    public void DescribesSaveFailureWithCause()
    {
        Assert.Equal(
            "저장 실패: 저장 공간이 부족해 이 촬영을 끝까지 저장하지 못했습니다(남은 120 MB). 저장된 부분은 D:/촬영/폴더에 그대로 있습니다.",
            ShotFolder.DescribeSaveFailure(120_000_000, "D:/촬영/폴더", "x"));

        var other = ShotFolder.DescribeSaveFailure(5_000_000_000, "D:/촬영/폴더", "영상 파일을 끝까지 쓰지 못함: a.mkv");
        Assert.DoesNotContain("저장 공간이 부족", other);
        Assert.Contains("영상 파일을 끝까지 쓰지 못함: a.mkv", other);
        Assert.Contains("저장 실패", ShotFolder.DescribeSaveFailure(null, "D:/f", ""));
    }
}

// 열화상 변환 시험용: 화소 값이 장 번호 * 1000 + 행 * 10 + 열인 원시값 파일과 열화상 사이드카를 씁니다.
public class ThermalArraysTests
{
    private const int Width = 6;
    private const int Height = 4;
    private const int Frames = 3;

    private PreparedShot _shot = new();

    private void Prepare(string name, int width, int height, long sidecarFrames)
    {
        var root = ShotFolderFixtures.FreshRoot(name);
        var prepared = ShotFolder.PrepareShot(root, ShotFolderFixtures.MakeShot(), VideoCodec.Ffv1, SavedVideos.Feet,
                                              ShotFolderFixtures.RgbAndThermal, 99, out var error);
        Assert.True(prepared is not null, error);
        _shot = prepared!;
        var raw = new List<byte>();
        for (var f = 0; f < Frames; ++f)
        {
            for (var y = 0; y < height; ++y)
            {
                for (var x = 0; x < width; ++x)
                {
                    var v = (ushort)(f * 1000 + y * 10 + x);
                    raw.Add((byte)(v & 0xFF));
                    raw.Add((byte)(v >> 8));
                }
            }
        }
        File.WriteAllBytes(_shot.ThermalRaw, raw.ToArray());
        var sidecar = new Sidecar
        {
            Stream = StreamKind.Thermal,
            Format = new VideoFormat(PixelFormat.Y16, width, height, 30),
            Crop = new Rect(0, 0, width, height),
            WrittenFrames = sidecarFrames,
            WrittenInRgbWindow = 270,
        };
        ShotFolderFixtures.WriteBytes(_shot.ThermalSidecar, RecordFunctions.BuildSidecarJson(sidecar));
    }

    // .npy 머리글 뒤 자료가 원시값 파일에서 영역(x0부터 폭 width)을 자른 것과 같은지 봅니다.
    private static byte[] ExpectedData(int x0, int width, int height)
    {
        var data = new List<byte>();
        for (var f = 0; f < Frames; ++f)
        {
            for (var y = 0; y < height; ++y)
            {
                for (var x = x0; x < x0 + width; ++x)
                {
                    var v = (ushort)(f * 1000 + y * 10 + x);
                    data.Add((byte)(v & 0xFF));
                    data.Add((byte)(v >> 8));
                }
            }
        }
        return data.ToArray();
    }

    private static byte[] DataOf(byte[] npy) => npy[(10 + (npy[8] | (npy[9] << 8)))..];

    private static string Ascii(byte[] bytes) => Encoding.Latin1.GetString(bytes);

    // 기본(가운데 세로선)으로 나눠 왼발과 오른발 .npy를 씁니다. 화소 왼쪽이 오른발이면 화소 오른쪽 절반이 thermal_left.npy입니다.
    [Fact]
    public void WritesFootArraysFromCenterSplit()
    {
        Prepare("arrays_center", Width, Height, Frames);

        Assert.True(ShotFolder.WriteThermalArrays(_shot, null, out var error), error);

        var left = File.ReadAllBytes(_shot.ThermalLeftArray);
        var right = File.ReadAllBytes(_shot.ThermalRightArray);
        Assert.True(left.Length > 10);
        Assert.StartsWith("\u0093NUMPY", Ascii(left));
        Assert.Contains("'shape': (3, 4, 3)", Ascii(left));
        Assert.Contains("'descr': '<u2'", Ascii(left));
        Assert.Equal(0, (left.Length - DataOf(left).Length) % 64);
        Assert.Equal(ExpectedData(3, 3, Height), DataOf(left));
        Assert.Equal(ExpectedData(0, 3, Height), DataOf(right));
        Assert.True(File.Exists(_shot.ThermalRaw));
    }

    // 영역을 직접 주면 그 영역으로 자르고, 화소 왼쪽 발이 왼발이면 화소 왼쪽 영역이 thermal_left.npy입니다.
    [Fact]
    public void UsesGivenRegions()
    {
        Prepare("arrays_given", Width, Height, Frames);
        _shot = _shot with { PixelLeftFoot = FootSide.Left };

        Assert.True(ShotFolder.WriteThermalArrays(_shot, (new Rect(0, 0, 2, 4), new Rect(4, 0, 2, 4)), out var error), error);

        Assert.Contains("'shape': (3, 4, 2)", Ascii(File.ReadAllBytes(_shot.ThermalLeftArray)));
        Assert.Equal(ExpectedData(0, 2, Height), DataOf(File.ReadAllBytes(_shot.ThermalLeftArray)));
        Assert.Equal(ExpectedData(4, 2, Height), DataOf(File.ReadAllBytes(_shot.ThermalRightArray)));
    }

    // 발 영역이 세로로 길지 않으면 .npy를 쓰지 않고, 원시값 파일은 남깁니다.
    [Fact]
    public void RejectsLandscapeRegion()
    {
        Prepare("arrays_landscape", 8, 3, Frames);

        Assert.False(ShotFolder.WriteThermalArrays(_shot, null, out var error));

        Assert.Contains("세로", error);
        Assert.False(File.Exists(_shot.ThermalLeftArray));
        Assert.False(File.Exists(_shot.ThermalRightArray));
        Assert.True(File.Exists(_shot.ThermalRaw));
    }

    // 원시값 장 수가 사이드카와 다르면 이번에 쓴 .npy를 지우고, 원시값 파일은 남깁니다.
    [Fact]
    public void RemovesArraysWhenFrameCountDiffers()
    {
        Prepare("arrays_mismatch", Width, Height, Frames + 2);

        Assert.False(ShotFolder.WriteThermalArrays(_shot, null, out var error));

        Assert.Contains("장 수", error);
        Assert.False(File.Exists(_shot.ThermalLeftArray));
        Assert.False(File.Exists(_shot.ThermalRightArray));
        Assert.Equal(Width * Height * 2 * Frames, new FileInfo(_shot.ThermalRaw).Length);
    }

    // 열화상 장치 없이 준비한 촬영과 사이드카가 없거나 RGB 사이드카인 촬영은 .npy를 쓰지 않습니다.
    [Fact]
    public void RejectsShotWithoutThermalSidecar()
    {
        var root = ShotFolderFixtures.FreshRoot("arrays_no_thermal");
        var rgbOnly = ShotFolder.PrepareShot(root, ShotFolderFixtures.MakeShot(), VideoCodec.Ffv1, SavedVideos.Feet,
                                             ShotFolderFixtures.RgbOnly, 99, out var error);
        Assert.True(rgbOnly is not null, error);
        Assert.False(ShotFolder.WriteThermalArrays(rgbOnly!, null, out error));
        Assert.Equal("열화상 장치 없이 준비한 촬영", error);

        Prepare("arrays_no_sidecar", Width, Height, Frames);
        File.Delete(_shot.ThermalSidecar);
        Assert.False(ShotFolder.WriteThermalArrays(_shot, null, out error));
        Assert.Contains("사이드카를 열지 못함", error);

        var rgbSidecar = new Sidecar { Format = new VideoFormat(PixelFormat.Uyvy, Width, Height, 30), Crop = new Rect(0, 0, Width, Height) };
        ShotFolderFixtures.WriteBytes(_shot.ThermalSidecar, RecordFunctions.BuildSidecarJson(rgbSidecar));
        Assert.False(ShotFolder.WriteThermalArrays(_shot, null, out error));
        Assert.Contains("열화상 사이드카가 아님", error);
    }

    // 한글 폴더의 촬영에서 좌우 영상 사이드카와 .npy를 읽어 분석 전 확인을 통과하고, 영상이 빠지면 그 역할을 알립니다.
    [Fact]
    public void ReadsAnalysisFilesForCheck()
    {
        Prepare("arrays_check", Width, Height, Frames);
        Assert.True(ShotFolder.WriteThermalArrays(_shot, null, out var error), error);
        foreach (var (video, sidecarPath, foot) in new[]
                 {
                     (_shot.LeftVideo, _shot.LeftSidecar, FootSide.Left),
                     (_shot.RightVideo, _shot.RightSidecar, FootSide.Right),
                 })
        {
            ShotFolderFixtures.WriteBytes(video, "video");
            var sidecar = new Sidecar
            {
                Format = new VideoFormat(PixelFormat.Uyvy, 1920, 1080, 30),
                Foot = foot,
                Crop = new Rect(foot == FootSide.Left ? 960 : 480, 0, 480, 1080),
            };
            ShotFolderFixtures.WriteBytes(sidecarPath, RecordFunctions.BuildSidecarJson(sidecar));
        }

        Assert.Empty(RecordFunctions.CheckAnalysisInputs(ShotFolder.ReadAnalysisFiles(_shot), AnalysisFiles.MinThermalFramesInRgbWindow));

        File.Delete(_shot.RightVideo);
        var findings = RecordFunctions.CheckAnalysisInputs(ShotFolder.ReadAnalysisFiles(_shot), AnalysisFiles.MinThermalFramesInRgbWindow);
        var finding = Assert.Single(findings);
        Assert.Equal(InputProblem.MissingFile, finding.Problem);
        Assert.Equal(FileRole.RightVideo, finding.Role);
    }
}
