using System.Text.Json;
using System.Text.Json.Nodes;

namespace DiabeticFoot.FakeAnalyzer.Tests;

public sealed class ProtocolTests
{
    static FakeAnalyzerProcess Preloaded(params string[] extra)
    {
        FakeAnalyzerProcess analyzer = FakeAnalyzerProcess.StartLikeApp(extra);
        Assert.True(AnalyzerLines.IsDfr(analyzer.NextRequired(), "ready"));
        analyzer.Send("{\"preload\":true}");
        Assert.Equal("[dfr] preloaded", analyzer.NextRequired());
        return analyzer;
    }

    static JsonElement DoneOf(FakeAnalyzerProcess analyzer, string request)
    {
        analyzer.Send(request);
        return AnalyzerLines.Json(analyzer.ReadThrough(AnalyzerLines.IsDone)[^1], "done");
    }

    static JsonElement SummaryOf(Shot shot) => JsonDocument.Parse(File.ReadAllText(shot.ResultFile("_summary.json"))).RootElement;

    [Fact]
    public void ReadyLineNamesVariantAndResearchCommit()
    {
        using FakeAnalyzerProcess analyzer = FakeAnalyzerProcess.StartLikeApp("--presegment-threads", "2", "--prestream");
        JsonElement ready = AnalyzerLines.Json(analyzer.NextRequired(), "ready");
        Assert.Equal("fake", ready.GetProperty("variant").GetString());
        Assert.Equal("fake", ready.GetProperty("upstream_commit").GetString());
    }

    [Fact]
    public void StartsWithoutTheScriptPathArgumentToo()
    {
        using FakeAnalyzerProcess analyzer = FakeAnalyzerProcess.Start("--variant", "fake", "--idle-exit", "0");
        Assert.True(AnalyzerLines.IsDfr(analyzer.NextRequired(), "ready"));
    }

    [Fact]
    public void UnknownVariantEndsBeforeReady()
    {
        using FakeAnalyzerProcess analyzer = FakeAnalyzerProcess.Start(FakeAnalyzerProcess.Dll, "--idle-exit", "0", "--variant", "v10");
        Assert.Equal(1, analyzer.WaitForExit());
        Assert.Null(analyzer.Next(1000));
        Assert.Contains("fake", analyzer.Errors);
    }

    [Fact]
    public void MissingVariantEndsBeforeReady()
    {
        using FakeAnalyzerProcess analyzer = FakeAnalyzerProcess.Start(FakeAnalyzerProcess.Dll, "--idle-exit", "0");
        Assert.Equal(2, analyzer.WaitForExit());
        Assert.Null(analyzer.Next(1000));
    }

    [Fact]
    public void ClosingInputEndsWithExitCodeZero()
    {
        using FakeAnalyzerProcess analyzer = Preloaded();
        analyzer.CloseInput();
        Assert.Equal(0, analyzer.WaitForExit());
        Assert.Null(analyzer.Next(1000));
    }

    [Fact]
    public void IdleExitPrintsLineThenEnds()
    {
        using FakeAnalyzerProcess analyzer = FakeAnalyzerProcess.Start(FakeAnalyzerProcess.Dll, "--idle-exit", "0.5", "--variant", "fake");
        Assert.True(AnalyzerLines.IsDfr(analyzer.NextRequired(), "ready"));
        Assert.Equal("[dfr] idle-exit", analyzer.NextRequired(5000));
        Assert.Equal(0, analyzer.WaitForExit());
    }

    [Fact]
    public void BomBeforeTheFirstLineMakesItUnreadable()
    {
        using FakeAnalyzerProcess analyzer = FakeAnalyzerProcess.StartLikeApp();
        analyzer.NextRequired();
        analyzer.SendBytes([0xEF, 0xBB, 0xBF, .. "{\"preload\":true}\n"u8]);
        JsonElement done = AnalyzerLines.Json(analyzer.NextRequired(), "done");
        Assert.False(done.GetProperty("ok").GetBoolean());
        Assert.StartsWith("요청을 읽지 못했습니다", done.GetProperty("error").GetString());
    }

    [Fact]
    public void RejectsWhatTheAnalyzerRejectsAndKeepsRunning()
    {
        using var shot = new Shot();
        using FakeAnalyzerProcess analyzer = Preloaded();
        (string Request, string Error)[] cases =
        [
            ("not json", "요청을 읽지 못했습니다"),
            (shot.Request(r => r.Remove("output")), "rec_dir 과 output 이 있어야 합니다."),
            (shot.Request(r => r.Remove("right_video")), "left_video 와 right_video 는 둘 다 주거나 둘 다 빼야 합니다."),
            (shot.Request(r => r["left_video"] = shot.LeftVideo + ".missing"), "left 영상이 없습니다: "),
            (shot.Request(r => { r.Remove("left_video"); r.Remove("right_video"); }), "left 영상이 없습니다: "),
            (shot.Request(r => r["pad_classifier"] = true), "이 패키지에는 이진 PAD 판정 모델이 없습니다."),
        ];
        foreach ((string request, string error) in cases)
        {
            JsonElement done = DoneOf(analyzer, request);
            Assert.False(done.GetProperty("ok").GetBoolean(), request);
            Assert.StartsWith(error, done.GetProperty("error").GetString());
        }
        Assert.True(DoneOf(analyzer, shot.Request()).GetProperty("ok").GetBoolean());
    }

    [Fact]
    public void UsesLeftAviAndRightAviWhenVideoKeysAreAbsent()
    {
        using var shot = new Shot();
        File.WriteAllBytes(Path.Combine(shot.Folder, "left.avi"), [1]);
        File.WriteAllBytes(Path.Combine(shot.Folder, "right.avi"), [1]);
        using FakeAnalyzerProcess analyzer = Preloaded();
        Assert.True(DoneOf(analyzer, shot.Request(r => { r.Remove("left_video"); r.Remove("right_video"); })).GetProperty("ok").GetBoolean());
    }

    [Fact]
    public void FailNameEndsWithFailedDoneCarryingMultilineError()
    {
        using var shot = new Shot();
        using FakeAnalyzerProcess analyzer = Preloaded();
        analyzer.Send(shot.Request(name: "fail"));
        List<string> lines = analyzer.ReadThrough(AnalyzerLines.IsDone);
        Assert.Contains(lines, line => AnalyzerLines.Stage(line) == (5, 5));
        JsonElement done = AnalyzerLines.Json(lines[^1], "done");
        Assert.False(done.GetProperty("ok").GetBoolean());
        Assert.Equal("left.avi 가 없습니다: C:\\촬영\\left.avi\n두 번째 줄", done.GetProperty("error").GetString());
    }

    [Fact]
    public void NoSummaryNameReportsSuccessWithoutWritingFiles()
    {
        using var shot = new Shot();
        using FakeAnalyzerProcess analyzer = Preloaded();
        Assert.True(DoneOf(analyzer, shot.Request(name: "nosummary")).GetProperty("ok").GetBoolean());
        Assert.False(File.Exists(Path.Combine(shot.Output, "nosummary_summary.json")));
    }

    [Fact]
    public void CrashNameEndsWithExitCodeThreeAfterAProgressLine()
    {
        using var shot = new Shot();
        using FakeAnalyzerProcess analyzer = Preloaded();
        analyzer.Send(shot.Request(name: "crash"));
        Assert.Contains(analyzer.ReadThrough(line => AnalyzerLines.Stage(line) == (1, 5)), line => line.StartsWith("[0/5]"));
        Assert.Equal(3, analyzer.WaitForExit());
        Assert.Contains("Traceback", analyzer.Errors);
    }

    [Fact]
    public void HangNameStopsPrintingWithoutDone()
    {
        using var shot = new Shot();
        using FakeAnalyzerProcess analyzer = Preloaded();
        analyzer.Send(shot.Request(name: "hang"));
        analyzer.ReadThrough(line => AnalyzerLines.Stage(line) == (1, 5));
        Assert.Null(analyzer.Next(1500));
    }

    [Fact]
    public void PresegmentIsRefusedWithoutTheFlag()
    {
        using var shot = new Shot();
        using FakeAnalyzerProcess analyzer = Preloaded();
        analyzer.Send(PresegmentLine(shot));
        Assert.False(AnalyzerLines.Json(analyzer.NextRequired(), "presegmented").GetProperty("ok").GetBoolean());
        Assert.True(DoneOf(analyzer, shot.Request()).GetProperty("ok").GetBoolean());
        Assert.False(SummaryOf(shot).GetProperty("dfr").GetProperty("presegment").GetProperty("enabled").GetBoolean());
    }

    [Fact]
    public void PresegmentBeforePreloadIsRefusedAndRecordedInTheSummary()
    {
        using var shot = new Shot();
        using FakeAnalyzerProcess analyzer = FakeAnalyzerProcess.StartLikeApp("--presegment-threads", "2", "--prestream");
        analyzer.NextRequired();
        analyzer.Send(PresegmentLine(shot));
        Assert.False(AnalyzerLines.Json(analyzer.NextRequired(), "presegmented").GetProperty("ok").GetBoolean());
        analyzer.Send("{\"preload\":true}");
        analyzer.NextRequired();
        Assert.True(DoneOf(analyzer, shot.Request()).GetProperty("ok").GetBoolean());
        JsonElement dfr = SummaryOf(shot).GetProperty("dfr");
        Assert.Equal("model_not_loaded", dfr.GetProperty("presegment").GetProperty("reason").GetString());
        Assert.Equal("not_started", dfr.GetProperty("prestream").GetProperty("reason").GetString());
    }

    [Fact]
    public void PresegmentAnswersPerFootAndTheAnalysisUsesIt()
    {
        using var shot = new Shot();
        using FakeAnalyzerProcess analyzer = Preloaded("--presegment-threads", "2", "--prestream");
        analyzer.Send(PresegmentLine(shot));
        JsonElement left = AnalyzerLines.Json(analyzer.NextRequired(), "presegmented");
        JsonElement right = AnalyzerLines.Json(analyzer.NextRequired(), "presegmented");
        Assert.Equal(shot.LeftVideo, left.GetProperty("video").GetString());
        Assert.Equal(shot.RightVideo, right.GetProperty("video").GetString());
        Assert.True(left.GetProperty("ok").GetBoolean() && right.GetProperty("ok").GetBoolean());

        analyzer.Send(shot.Request());
        List<string> lines = analyzer.ReadThrough(AnalyzerLines.IsDone);
        Assert.Equal(2, lines.Count(line => AnalyzerLines.IsDfr(line, "prestreamed")));
        JsonElement dfr = SummaryOf(shot).GetProperty("dfr");
        Assert.Equal(JsonValueKind.Null, dfr.GetProperty("presegment").GetProperty("reason").ValueKind);
        Assert.True(dfr.GetProperty("prestream").GetProperty("used").GetBoolean());
    }

    [Fact]
    public void AnalysisWithoutPresegmentRecordsNotRequested()
    {
        using var shot = new Shot();
        using FakeAnalyzerProcess analyzer = Preloaded("--presegment-threads", "2", "--prestream");
        Assert.True(DoneOf(analyzer, shot.Request()).GetProperty("ok").GetBoolean());
        JsonElement dfr = SummaryOf(shot).GetProperty("dfr");
        Assert.Equal("not_requested", dfr.GetProperty("presegment").GetProperty("reason").GetString());
        Assert.Equal("not_started", dfr.GetProperty("prestream").GetProperty("reason").GetString());
    }

    [Fact]
    public void FiguresAreWrittenBeforeDoneWhenNotDeferred()
    {
        using var shot = new Shot();
        using FakeAnalyzerProcess analyzer = Preloaded();
        Assert.True(DoneOf(analyzer, shot.Request(r => r["defer_figures"] = false)).GetProperty("ok").GetBoolean());
        Assert.True(File.Exists(shot.ResultFile("_perfusion.gif")));
        Assert.True(File.Exists(shot.ResultFile("_pi_map.png")));
        analyzer.CloseInput();
        Assert.Equal(0, analyzer.WaitForExit());
        Assert.Null(analyzer.Next(500));
    }

    [Fact]
    public void GifFalseWritesTheOtherFilesOnly()
    {
        using var shot = new Shot();
        using FakeAnalyzerProcess analyzer = Preloaded();
        analyzer.Send(shot.Request(r => r["gif"] = false));
        analyzer.ReadThrough(line => AnalyzerLines.IsDfr(line, "figures"));
        Assert.False(File.Exists(shot.ResultFile("_perfusion.gif")));
        Assert.True(File.Exists(shot.ResultFile("_perfusion.npz")));
        Assert.True(File.Exists(shot.ResultFile("_segmentation.png")));
    }

    [Fact]
    public void ClassifyFalseLeavesCuffLevelNull()
    {
        using var shot = new Shot();
        using FakeAnalyzerProcess analyzer = Preloaded();
        Assert.True(DoneOf(analyzer, shot.Request(r => r["classify"] = false)).GetProperty("ok").GetBoolean());
        Assert.Equal(JsonValueKind.Null, SummaryOf(shot).GetProperty("cuff_level").ValueKind);
    }

    [Fact]
    public void SameRequestWritesTheSameArraysAndPictures()
    {
        using var first = new Shot();
        using var second = new Shot();
        using FakeAnalyzerProcess analyzer = Preloaded();
        foreach (Shot shot in new[] { first, second })
        {
            analyzer.Send(shot.Request());
            analyzer.ReadThrough(line => AnalyzerLines.IsDfr(line, "figures"));
        }
        foreach (string suffix in new[] { "_perfusion.npz", "_waveforms.npz", "_perfusion.gif", "_segmentation.png", "_template.png", "_pi_map.png" })
        {
            Assert.True(File.ReadAllBytes(first.ResultFile(suffix)).SequenceEqual(File.ReadAllBytes(second.ResultFile(suffix))), suffix);
        }
    }

    static string PresegmentLine(Shot shot) =>
        new JsonObject { ["presegment"] = true, ["left_video"] = shot.LeftVideo, ["right_video"] = shot.RightVideo }.ToJsonString();
}
