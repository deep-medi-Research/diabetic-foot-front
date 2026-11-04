using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

public class AnalyzerSetupTests
{
    private static string TempDir(string name)
    {
        var dir = Path.Combine(Path.GetTempPath(), "diabeticfoot_test_analyzer_setup", name);
        if (Directory.Exists(dir)) Directory.Delete(dir, recursive: true);
        Directory.CreateDirectory(dir);
        return dir;
    }

    // 배포 폴더의 파이썬과 스크립트를 낮은 우선순위, 유휴 종료 없음, UTF-8 출력으로 띄웁니다.
    [Fact]
    public void UsesBundledPythonAndScript()
    {
        var launch = AnalyzerSetup.Launch("C:/앱/", "", "");
        Assert.Equal(OperatingSystem.IsWindows() ? "C:/앱/python/python.exe" : "C:/앱/python/bin/python3", launch.Program);
        Assert.Equal(new[] { "C:/앱/analysis/dfr_worker.py", "--idle-exit", "0" }, launch.Arguments);
        Assert.Equal("C:/앱/analysis", launch.WorkingDir);
        Assert.True(launch.LowPriority);
        Assert.Equal(
            new[] { ("PYTHONUNBUFFERED", "1"), ("PYTHONUTF8", "1"), ("PYTHONIOENCODING", "utf-8") },
            launch.ExtraEnv);
    }

    // 개발용으로 준 파이썬과 스크립트를 그대로 씁니다. PATH에서 찾지 않습니다.
    [Fact]
    public void UsesOverrides()
    {
        var launch = AnalyzerSetup.Launch("/app", "/venv/bin/python", "/src/analysis/dfr_worker.py");
        Assert.Equal("/venv/bin/python", launch.Program);
        Assert.Equal("/src/analysis/dfr_worker.py", launch.Arguments[0]);
        Assert.Equal("/src/analysis", launch.WorkingDir);
    }

    // 파이썬이나 스크립트가 없거나 비어 있으면 그 경로를 이유로 돌려줍니다.
    [Fact]
    public void ReportsMissingFiles()
    {
        var dir = TempDir("files");
        var python = Path.Combine(dir, "python.exe");
        var script = Path.Combine(dir, "dfr_worker.py");
        var problem = AnalyzerSetup.FilesProblem(AnalyzerSetup.Launch(dir, python, script));
        Assert.NotNull(problem);
        Assert.Contains("파이썬 없음", problem);
        File.WriteAllText(python, "x");
        File.WriteAllText(script, "");
        problem = AnalyzerSetup.FilesProblem(AnalyzerSetup.Launch(dir, python, script));
        Assert.NotNull(problem);
        Assert.Contains("분석 스크립트 크기 0", problem);
        File.WriteAllText(script, "print()");
        Assert.Null(AnalyzerSetup.FilesProblem(AnalyzerSetup.Launch(dir, python, script)));
    }

    // variants/<판>/VARIANT.json을 판 이름 순으로 읽고, id가 폴더 이름과 다르거나 없는 판은 빼고 이유를 남깁니다.
    [Fact]
    public void ReadsVariantFolders()
    {
        var dir = TempDir("variants");
        foreach (var name in new[] { "v7", "v6", "bad", "empty" }) Directory.CreateDirectory(Path.Combine(dir, "variants", name));
        File.WriteAllText(Path.Combine(dir, "variants", "v7", "VARIANT.json"), """
            {"id": "v7", "label": "v7", "summary": "규칙 판정, 템플릿 v7",
            "upstream": {"repo": "r", "commit": "1f47dea", "date": "2026-10-28"},
            "release": {"tag": "models-v2",
              "classifier": {"name": "pad_cuff_level_bundle.json", "file": "f", "uploaded": "2026-10-28", "sha256": "0a51"},
              "template": {"name": "v106w_foot.onnx", "file": "g", "uploaded": "2026-10-28", "sha256": "aee1"}}}
            """);
        File.WriteAllText(Path.Combine(dir, "variants", "v6", "VARIANT.json"), """{"id": "v6", "upstream": {"commit": "590da2d"}}""");
        File.WriteAllText(Path.Combine(dir, "variants", "bad", "VARIANT.json"), """{"id": "other"}""");

        var variants = AnalyzerSetup.FindVariants(dir, out var error);
        Assert.Equal(2, variants.Count);
        Assert.Equal("v6", variants[0].Id);
        Assert.Equal("v6", variants[0].Label);
        Assert.Equal("590da2d", variants[0].UpstreamCommit);
        Assert.Equal("", variants[0].Classifier.Name);
        var v7 = variants[1];
        Assert.Equal("규칙 판정, 템플릿 v7", v7.Summary);
        Assert.Equal("2026-10-28", v7.UpstreamDate);
        Assert.Equal("models-v2", v7.ReleaseTag);
        Assert.Equal("pad_cuff_level_bundle.json", v7.Classifier.Name);
        Assert.Equal("0a51", v7.Classifier.Sha256);
        Assert.Equal("2026-10-28", v7.PulseTemplate.Uploaded);
        Assert.Contains("variants/bad/VARIANT.json", error);

        var none = AnalyzerSetup.FindVariants(Path.Combine(dir, "nothing"), out error);
        Assert.Empty(none);
        Assert.Contains("연구 코드 판이 없습니다", error);
    }

    // 판 하나를 올리는 실행 정보는 기본 인자 끝에 --variant를 더합니다.
    [Fact]
    public void AddsVariantArgument()
    {
        var launch = AnalyzerSetup.VariantLaunch(AnalyzerSetup.Launch("/app", "", ""), "v7");
        Assert.Equal(5, launch.Arguments.Count);
        Assert.Equal("--variant", launch.Arguments[3]);
        Assert.Equal("v7", launch.Arguments[4]);
        Assert.Equal("/app/analysis", launch.WorkingDir);
    }
}
