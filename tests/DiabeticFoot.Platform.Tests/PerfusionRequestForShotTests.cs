using System.Text;
using DiabeticFoot.Core;
using DiabeticFoot.Core.Pixel;
using DiabeticFoot.Core.Record;
using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

// 원본 test_analyzer.cc의 PerfusionRequestForShot 시험 3건입니다.
public class PerfusionRequestForShotTests
{
    private static string TempDir(string name)
    {
        var dir = Path.Combine(Path.GetTempPath(), "diabeticfoot_test_request_for_shot", name);
        if (Directory.Exists(dir)) Directory.Delete(dir, recursive: true);
        Directory.CreateDirectory(dir);
        return dir;
    }

    private static void WriteFile(string path, string text) => File.WriteAllBytes(path, new UTF8Encoding(false).GetBytes(text));

    private static string Sidecar(string foot, long written) =>
        "{\"schema_version\": 3, \"stream\": \"rgb\", \"foot\": \"" + foot +
        "\", \"format\": {\"width\": 640, \"height\": 1080}, \"crop\": {\"x\": 0, \"y\": 0, \"width\": 640, " +
        "\"height\": 1080}, \"frames\": {\"written\": " + written + ", \"written_in_rgb_window\": " + written +
        ", \"missing\": 0, \"dropped\": 0}}";

    // 촬영 폴더의 매니페스트와 사이드카로 요청(영상 경로, 장 수, 결과 폴더, 촬영 폴더 이름)을 만듭니다.
    [Fact]
    public void ReadsManifestAndSidecars()
    {
        var dir = Path.Combine(TempDir("홍길동_19551103"), "102528_2");
        Directory.CreateDirectory(dir);
        WriteFile(Path.Combine(dir, "102528_2_shot.json"),
            "{\"schema_version\": 3, \"files\": {\"left_video\": \"102528_2_left.mkv\", \"left_sidecar\": " +
            "\"102528_2_left.json\", \"right_video\": \"102528_2_right.mkv\", \"right_sidecar\": " +
            "\"102528_2_right.json\", \"manifest\": \"102528_2_shot.json\"}}");
        WriteFile(Path.Combine(dir, "102528_2_left.json"), Sidecar("left", 900));
        WriteFile(Path.Combine(dir, "102528_2_right.json"), Sidecar("right", 899));
        var request = PerfusionAnalysis.PerfusionRequestForShot(dir + "/", out var error);
        Assert.True(request is not null, error);
        Assert.Equal(dir, request!.RecordingDir);
        Assert.Equal(dir + "/102528_2_left.mkv", request.LeftVideo);
        Assert.Equal(dir + "/102528_2_right.mkv", request.RightVideo);
        Assert.Equal(900, request.LeftFrames);
        Assert.Equal(899, request.RightFrames);
        Assert.Equal(dir + "/perfusion", request.OutputDir);
        Assert.Equal("102528_2", request.Name);
    }

    // 지금 앱이 쓰는 사이드카(조명 기록 포함)로도 요청의 장 수를 읽습니다.
    [Fact]
    public void ReadsCurrentSidecarSchemaWithLight()
    {
        var dir = Path.Combine(TempDir("schema4"), "102528");
        Directory.CreateDirectory(dir);
        WriteFile(Path.Combine(dir, "102528_shot.json"),
            "{\"files\": {\"left_video\": \"102528_left.mkv\", \"left_sidecar\": \"102528_left.json\", " +
            "\"right_video\": \"102528_right.mkv\", \"right_sidecar\": \"102528_right.json\"}}");
        var sidecar = new Sidecar
        {
            Format = new VideoFormat(PixelFormat.Uyvy, 640, 1080, 30),
            Crop = new Rect(0, 0, 640, 1080),
            Light = new LightRecord { Source = LightSource.App, Percent = 50, Flicker = new Flicker { FrameCount = 900 } },
            Foot = FootSide.Left,
            WrittenFrames = 900,
        };
        WriteFile(Path.Combine(dir, "102528_left.json"), RecordFunctions.BuildSidecarJson(sidecar));
        sidecar = sidecar with
        {
            Foot = FootSide.Right,
            WrittenFrames = 898,
            Light = sidecar.Light with { Source = LightSource.External, Percent = null },
        };
        WriteFile(Path.Combine(dir, "102528_right.json"), RecordFunctions.BuildSidecarJson(sidecar));
        var request = PerfusionAnalysis.PerfusionRequestForShot(dir, out var error);
        Assert.True(request is not null, error);
        Assert.Equal(900, request!.LeftFrames);
        Assert.Equal(898, request.RightFrames);
    }

    // 매니페스트나 사이드카를 읽지 못하면 요청을 만들지 않고 이유를 남깁니다.
    [Fact]
    public void FailsWithoutManifestOrSidecar()
    {
        var dir = Path.Combine(TempDir("broken"), "093000");
        Directory.CreateDirectory(dir);
        Assert.Null(PerfusionAnalysis.PerfusionRequestForShot(dir, out var error));
        Assert.Contains("매니페스트를 열지 못했습니다", error);
        WriteFile(Path.Combine(dir, "093000_shot.json"),
            "{\"files\": {\"left_video\": \"a.mkv\", \"left_sidecar\": \"a.json\", \"right_video\": \"b.mkv\", " +
            "\"right_sidecar\": \"b.json\"}}");
        WriteFile(Path.Combine(dir, "a.json"), Sidecar("left", 900));
        Assert.Null(PerfusionAnalysis.PerfusionRequestForShot(dir, out error));
        Assert.Contains("사이드카를 읽지 못했습니다", error);
        Assert.Contains("b.json", error);
    }
}
