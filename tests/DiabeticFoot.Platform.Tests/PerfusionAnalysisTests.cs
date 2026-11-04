using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

public class PerfusionAnalysisTests
{
    // 요청 줄은 기존 앱과 같은 키를 고정된 순서로 쓰고, 역슬래시와 따옴표만 이스케이프하며 한글은 그대로 둡니다.
    [Fact]
    public void BuildsRequestLineWithEscapes()
    {
        var request = new PerfusionRequest
        {
            RecordingDir = "C:\\촬영\\홍길동 \"A\"",
            OutputDir = "C:\\촬영\\perfusion",
            Name = "20261019_홍길동",
        };
        Assert.Equal(
            "{\"rec_dir\":\"C:\\\\촬영\\\\홍길동 \\\"A\\\"\",\"output\":\"C:\\\\촬영\\\\perfusion\"," +
            "\"name\":\"20261019_홍길동\",\"cuff_side\":\"none\",\"template\":\"v106w\"," +
            "\"classify\":true,\"pad_classifier\":false,\"gif\":true,\"defer_figures\":true}",
            PerfusionAnalysis.BuildPerfusionRequestLine(request));

        request = request with { Classify = false, PadClassifier = true, Gif = false, Name = "줄\n바꿈" };
        var line = PerfusionAnalysis.BuildPerfusionRequestLine(request)!;
        Assert.Contains("\"name\":\"줄\\n바꿈\"", line);
        Assert.Contains("\"classify\":false,\"pad_classifier\":true,\"gif\":false,\"defer_figures\":true}", line);
        Assert.DoesNotContain('\n', line);
    }

    // 영상 경로가 있으면 rec_dir 뒤에 left_video, right_video 키를 넣습니다.
    [Fact]
    public void BuildsRequestLineWithVideoPaths()
    {
        var request = new PerfusionRequest
        {
            RecordingDir = "C:\\촬영\\101500",
            LeftVideo = "C:\\촬영\\101500\\101500_left.mkv",
            RightVideo = "C:\\촬영\\101500\\101500_right.mkv",
            OutputDir = "C:\\촬영\\101500\\perfusion",
            Name = "n",
        };
        Assert.Equal(
            "{\"rec_dir\":\"C:\\\\촬영\\\\101500\"," +
            "\"left_video\":\"C:\\\\촬영\\\\101500\\\\101500_left.mkv\"," +
            "\"right_video\":\"C:\\\\촬영\\\\101500\\\\101500_right.mkv\"," +
            "\"output\":\"C:\\\\촬영\\\\101500\\\\perfusion\",\"name\":\"n\",\"cuff_side\":\"none\"," +
            "\"template\":\"v106w\",\"classify\":true,\"pad_classifier\":false,\"gif\":true,\"defer_figures\":true}",
            PerfusionAnalysis.BuildPerfusionRequestLine(request));

        // 장 수는 영상 경로 뒤에 정수로 넣습니다.
        request = request with { LeftFrames = 900, RightFrames = 899 };
        var withFrames = PerfusionAnalysis.BuildPerfusionRequestLine(request)!;
        Assert.Contains("101500_right.mkv\",\"left_frames\":900,\"right_frames\":899,\"output\":", withFrames);
    }

    // 영상 경로를 한쪽만 채우거나 장 수가 음수인 요청은 만들지 않고 이유를 돌려줍니다.
    [Fact]
    public void RejectsOneSidedVideoPathOrNegativeFrames()
    {
        var request = new PerfusionRequest { RecordingDir = "C:\\촬영", RightVideo = "C:\\촬영\\right.mkv" };
        Assert.Null(PerfusionAnalysis.BuildPerfusionRequestLine(request, out var error));
        Assert.NotEmpty(error);

        request = request with { RightVideo = "", LeftVideo = "C:\\촬영\\left.mkv" };
        Assert.Null(PerfusionAnalysis.BuildPerfusionRequestLine(request));

        request = request with { RightVideo = "C:\\촬영\\right.mkv", LeftFrames = -1 };
        Assert.Null(PerfusionAnalysis.BuildPerfusionRequestLine(request, out error));
        Assert.NotEmpty(error);

        // 경로 없이 장 수만 주면 관류 분석 프로그램이 촬영 폴더의 기본 이름 영상과 비교하므로 받아들입니다.
        var framesOnly = new PerfusionRequest { LeftFrames = 900, RightFrames = 900 };
        var line = PerfusionAnalysis.BuildPerfusionRequestLine(framesOnly)!;
        Assert.DoesNotContain("_video", line);
        Assert.Contains("\"left_frames\":900,\"right_frames\":900", line);
    }

    // 미리 올리기 요청은 관류 분석 프로그램이 preload 키로 알아보는 한 줄입니다.
    [Fact]
    public void BuildsPreloadLine() => Assert.Equal("{\"preload\":true}", PerfusionAnalysis.BuildPreloadLine());

    // 미리 분할 요청은 좌우 경로를 이스케이프해 담습니다.
    [Fact]
    public void BuildsPresegmentLine() =>
        Assert.Equal(
            "{\"presegment\":true,\"left_video\":\"C:\\\\촬영\\\\a_left.mkv\",\"right_video\":\"b\\\"r.mkv\"}",
            PerfusionAnalysis.BuildPresegmentLine("C:\\촬영\\a_left.mkv", "b\"r.mkv"));

    // 제어 문자는 \u 이스케이프로 쓰고, 이스케이프한 줄은 읽어서 원래 값으로 돌아옵니다.
    [Fact]
    public void EscapesControlCharactersAsUnicode()
    {
        var line = PerfusionAnalysis.BuildPresegmentLine("a\u0001b\tc", "x");
        Assert.Equal("{\"presegment\":true,\"left_video\":\"a\\u0001b\\tc\",\"right_video\":\"x\"}", line);
        var members = new JsonReader(line).Read()!;
        Assert.Equal("a\u0001b\tc", members["left_video"]);
    }

    // 미리 분할 결과 줄은 JSON을 문구로 둡니다.
    [Fact]
    public void ParsesPresegmentedLine()
    {
        var line = PerfusionAnalysis.ParseAnalyzerLine("[dfr] presegmented {\"ok\": false, \"error\": \"x\"}");
        Assert.Equal(AnalyzerLineKind.Presegmented, line.Kind);
        Assert.Equal("{\"ok\": false, \"error\": \"x\"}", line.Message);
    }

    // 미룬 그림을 다 그렸다는 줄은 JSON을 문구로 둡니다.
    [Fact]
    public void ParsesFiguresLine()
    {
        var line = PerfusionAnalysis.ParseAnalyzerLine("[dfr] figures {\"ok\": true, \"seconds\": 2.1}");
        Assert.Equal(AnalyzerLineKind.Figures, line.Kind);
        Assert.Equal("{\"ok\": true, \"seconds\": 2.1}", line.Message);
    }

    // 진행 단계 줄에서 번호와 문구를 읽고 끝의 경과 시간을 제거합니다.
    [Fact]
    public void ParsesStageAndStripsElapsed()
    {
        var line = PerfusionAnalysis.ParseAnalyzerLine("[3/5] Computing perfusion maps... [12 s]");
        Assert.Equal(AnalyzerLineKind.Stage, line.Kind);
        Assert.Equal(3, line.Step);
        Assert.Equal(5, line.Total);
        Assert.Equal("Computing perfusion maps...", line.Message);

        var zero = PerfusionAnalysis.ParseAnalyzerLine("[0/5] Loading");
        Assert.Equal(AnalyzerLineKind.Stage, zero.Kind);
        Assert.Equal(0, zero.Step);
        Assert.Equal("Loading", zero.Message);
    }

    // 들여 쓴 `[k/n]`은 하위 단계입니다. 끝의 `(0 s)`는 떼고, 문구 안의 괄호는 둡니다.
    [Fact]
    public void ParsesIndentedSubStage()
    {
        var sub = PerfusionAnalysis.ParseAnalyzerLine("  [3/7] thermal foot masks: RGB mask -> thermal registration  (0 s)");
        Assert.Equal(AnalyzerLineKind.SubStage, sub.Kind);
        Assert.Equal(3, sub.Step);
        Assert.Equal(7, sub.Total);
        Assert.Equal("thermal foot masks: RGB mask -> thermal registration", sub.Message);

        var kept = PerfusionAnalysis.ParseAnalyzerLine("  [1/7] thermal: reading (started in the background)  (0 s)");
        Assert.Equal(AnalyzerLineKind.SubStage, kept.Kind);
        Assert.Equal("thermal: reading (started in the background)", kept.Message);

        var tab = PerfusionAnalysis.ParseAnalyzerLine("\t[7/7] inference (12 s)");
        Assert.Equal(AnalyzerLineKind.SubStage, tab.Kind);
        Assert.Equal(7, tab.Step);
        Assert.Equal("inference", tab.Message);
        // 들여 쓴 줄의 깊이 표시(`[2/7] ... (8 of 12 members)`)처럼 숫자 밖의 글자가 있으면 떼지 않습니다.
        Assert.Equal("model (8 of 12 members)", PerfusionAnalysis.ParseAnalyzerLine("  [2/7] model (8 of 12 members)").Message);
    }

    // 번호가 아닌 대괄호, 전체보다 큰 번호, 전체가 0인 줄은 진행 단계가 아닙니다.
    [Fact]
    public void RejectsNonStageBrackets()
    {
        Assert.Equal(AnalyzerLineKind.Other, PerfusionAnalysis.ParseAnalyzerLine("  [a/7] thermal").Kind);
        Assert.Equal(AnalyzerLineKind.Other, PerfusionAnalysis.ParseAnalyzerLine("[WARN] something").Kind);
        Assert.Equal(AnalyzerLineKind.Other, PerfusionAnalysis.ParseAnalyzerLine("[a/5] x").Kind);
        Assert.Equal(AnalyzerLineKind.Other, PerfusionAnalysis.ParseAnalyzerLine("[6/5] x").Kind);
        Assert.Equal(AnalyzerLineKind.Other, PerfusionAnalysis.ParseAnalyzerLine("[1/0] x").Kind);
        var other = PerfusionAnalysis.ParseAnalyzerLine("  Window 3/11: t=5.5s");
        Assert.Equal(AnalyzerLineKind.Other, other.Kind);
        Assert.Equal("  Window 3/11: t=5.5s", other.Message);
    }

    // 파이썬 json.dumps가 찍는 완료 줄(쉼표와 콜론 뒤 공백)을 읽습니다.
    [Fact]
    public void ParsesDoneSuccess()
    {
        var line = PerfusionAnalysis.ParseAnalyzerLine("[dfr] done {\"ok\": true, \"error\": \"\"}");
        Assert.Equal(AnalyzerLineKind.Done, line.Kind);
        Assert.True(line.Ok);
        Assert.Equal("", line.Error);
    }

    // 실패한 완료 줄의 오류 문장에서 줄바꿈, 역슬래시, 따옴표, 유니코드 이스케이프를 풉니다.
    [Fact]
    public void ParsesDoneFailureWithEscapes()
    {
        var line = PerfusionAnalysis.ParseAnalyzerLine(
            "[dfr] done {\"ok\": false, \"error\": \"Traceback\\n  File \\\"C:\\\\\\ud55c\\uae00.py\\\"\\n\\ud83d\\ude00\"}");
        Assert.Equal(AnalyzerLineKind.Done, line.Kind);
        Assert.False(line.Ok);
        Assert.Equal("Traceback\n  File \"C:\\한글.py\"\n\U0001F600", line.Error);
    }

    // 완료 줄의 JSON이 깨졌으면 실패로 보고 원문을 이유에 남깁니다.
    [Fact]
    public void MalformedDoneIsFailure()
    {
        var line = PerfusionAnalysis.ParseAnalyzerLine("[dfr] done {\"ok\": tru");
        Assert.Equal(AnalyzerLineKind.Done, line.Kind);
        Assert.False(line.Ok);
        Assert.Contains("{\"ok\": tru", line.Error);

        var missing = PerfusionAnalysis.ParseAnalyzerLine("[dfr] done {}");
        Assert.False(missing.Ok);
        Assert.NotEmpty(missing.Error);
    }

    // 관류 분석 프로그램 표식 줄(ready, preloaded, preload-failed, idle-exit)을 구분하고 모르는 표식은 기록용으로 둡니다.
    [Fact]
    public void ParsesWorkerMarks()
    {
        var plain = PerfusionAnalysis.ParseAnalyzerLine("[dfr] ready");
        Assert.Equal(AnalyzerLineKind.Ready, plain.Kind);
        Assert.Equal("", plain.WorkerVersion);
        Assert.Equal(AnalyzerLineKind.Preloaded, PerfusionAnalysis.ParseAnalyzerLine("[dfr] preloaded").Kind);
        var failed = PerfusionAnalysis.ParseAnalyzerLine("[dfr] preload-failed 모델 파일이 없습니다");
        Assert.Equal(AnalyzerLineKind.PreloadFailed, failed.Kind);
        Assert.Equal("모델 파일이 없습니다", failed.Message);
        Assert.Equal(AnalyzerLineKind.IdleExit, PerfusionAnalysis.ParseAnalyzerLine("[dfr] idle-exit").Kind);
        Assert.Equal(AnalyzerLineKind.Other, PerfusionAnalysis.ParseAnalyzerLine("[dfr] stamp-failed x").Kind);
    }

    // ready 뒤의 버전 JSON을 읽고, JSON이 깨졌거나 키가 없어도 준비 완료로 봅니다.
    [Fact]
    public void ParsesReadyWithVersion()
    {
        var line = PerfusionAnalysis.ParseAnalyzerLine(
            "[dfr] ready {\"worker_version\": \"2026.10.19\", \"upstream_commit\": \"5e5178d\"}");
        Assert.Equal(AnalyzerLineKind.Ready, line.Kind);
        Assert.Equal("2026.10.19", line.WorkerVersion);
        Assert.Equal("5e5178d", line.UpstreamCommit);

        var broken = PerfusionAnalysis.ParseAnalyzerLine("[dfr] ready {\"worker_version\": ");
        Assert.Equal(AnalyzerLineKind.Ready, broken.Kind);
        Assert.Equal("", broken.WorkerVersion);
        Assert.Equal(AnalyzerLineKind.Ready, PerfusionAnalysis.ParseAnalyzerLine("[dfr] ready {\"other\": 1}").Kind);
        Assert.Equal(AnalyzerLineKind.Other, PerfusionAnalysis.ParseAnalyzerLine("[dfr] readyx").Kind);
    }

    // 요약 JSON(json.dump indent=1 꼴, NaN과 배열 포함)의 dfr 항목에서 버전 값만 읽고, 없으면 빈 값입니다.
    [Fact]
    public void ReadsWorkerStampFromSummary()
    {
        const string summary =
            "{\n \"n_frames\": 900,\n \"fps\": 30.0,\n \"pi\": NaN,\n \"windows\": [1.5, -Infinity, {\"upstream_commit\": \"x\"}],\n" +
            " \"classification\": {\"decision\": \"normal\", \"upstream_commit\": \"wrong\"},\n" +
            " \"dfr\": {\n  \"upstream_commit\": \"5e5178d3836c\",\n  \"detector_edge\": 400,\n" +
            "  \"models\": null,\n  \"worker_version\": \"2026.10.19\"\n }\n}\n";
        var stamp = PerfusionAnalysis.ReadWorkerStamp(summary);
        Assert.Equal("5e5178d3836c", stamp.UpstreamCommit);
        Assert.Equal("2026.10.19", stamp.WorkerVersion);

        var old = PerfusionAnalysis.ReadWorkerStamp("{\"dfr\": {\"upstream_commit\": \"abc\"}}");
        Assert.Equal("abc", old.UpstreamCommit);
        Assert.Equal("", old.WorkerVersion);
        Assert.Equal("", PerfusionAnalysis.ReadWorkerStamp("{\"pi\": 1}").UpstreamCommit);
        Assert.Equal("", PerfusionAnalysis.ReadWorkerStamp("{\"dfr\": {\"upstream_commit\": ").UpstreamCommit);
    }

    // 요약 JSON 경로는 결과 폴더와 이름으로 정해지고, 판별 결과 폴더는 perfusion 아래 판 이름입니다.
    [Fact]
    public void SummaryPath()
    {
        Assert.Equal("C:/촬영/perfusion/이름_summary.json", PerfusionAnalysis.PerfusionSummaryPath("C:/촬영/perfusion", "이름"));
        Assert.Equal("out/n_summary.json", PerfusionAnalysis.PerfusionSummaryPath("out/", "n"));
        Assert.Equal("C:/촬영/101500/perfusion", PerfusionAnalysis.PerfusionOutputDir("C:/촬영/101500", ""));
        Assert.Equal("C:/촬영/101500/perfusion/v7", PerfusionAnalysis.PerfusionOutputDir("C:/촬영/101500/", "v7"));
    }
}
