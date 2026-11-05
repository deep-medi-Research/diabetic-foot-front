using System.Diagnostics;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace DiabeticFoot.FakeAnalyzer;

/// <summary>
/// 관류 분석 프로그램(dfr_worker)과 같은 줄을 주고받는 가짜 분석기입니다. 표준 입력이 닫힐 때까지 요청을 한 건씩 처리합니다.
/// </summary>
/// <remarks>
/// 분석 요청의 이름이 다음 중 하나이면 그 경우를 흉내 냅니다. 입력 확인은 먼저 합니다.
/// fail: 진행 줄 뒤 실패 완료 줄. hang: [1/5] 뒤 60초 동안 출력 없음, 완료 줄 없음.
/// crash: [1/5] 뒤 표준 오류에 예외 기록을 쓰고 종료 코드 3. nosummary: 결과 파일 없이 성공 완료 줄.
/// </remarks>
internal sealed class Worker(WorkerOptions options, TextReader input, OutputLines output, TextWriter errors)
{
    readonly IdleExit idle = new(options.IdleExitSeconds, output);
    readonly HashSet<string> presegmented = new(PathComparer);
    readonly HashSet<string> prestreamJobs = new(PathComparer);
    bool preloaded;
    bool anyPresegment;
    string? presegmentFailure;

    static StringComparer PathComparer => OperatingSystem.IsWindows() ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal;

    public int Run()
    {
        idle.Start();
        output.Dfr("ready " + Json.Line(new JsonObject { ["variant"] = options.Variant.Id, ["upstream_commit"] = options.Variant.UpstreamCommit }));
        while (input.ReadLine() is string raw)
        {
            string line = raw.Trim();
            if (line.Length == 0) continue;
            idle.Touch();
            switch (WorkerMessage.Parse(line))
            {
                case WorkerMessage.Unreadable unreadable:
                    Done(unreadable.Error);
                    break;
                case WorkerMessage.Presegment request:
                    Presegment(request);
                    break;
                case WorkerMessage.Preload:
                    preloaded = true;
                    output.Dfr("preloaded");
                    break;
                case WorkerMessage.Analysis request:
                    idle.Busy(() => Analyze(request));
                    break;
            }
        }
        return 0;
    }

    void Done(string? problem) => output.Dfr("done " + Json.Line(new JsonObject { ["ok"] = problem is null, ["error"] = problem ?? "" }));

    void Presegment(WorkerMessage.Presegment request)
    {
        string[] videos = new[] { request.LeftVideo, request.RightVideo }.OfType<string>().ToArray();
        string? refused = !options.Presegment ? "미리 분할이 꺼져 있습니다" : !preloaded ? "분할 모델이 아직 올라 있지 않습니다" : null;
        if (refused is not null)
        {
            if (options.Presegment) presegmentFailure = "model_not_loaded";
            output.Dfr("presegmented " + Json.Line(new JsonObject { ["ok"] = false, ["error"] = refused }));
            return;
        }
        anyPresegment = true;
        presegmentFailure = null;
        prestreamJobs.Clear();
        if (options.Prestream && request.LeftVideo is not null && request.RightVideo is not null)
        {
            prestreamJobs.UnionWith(videos.Select(Path.GetFullPath));
        }
        foreach (string video in videos)
        {
            if (File.Exists(video))
            {
                presegmented.Add(Path.GetFullPath(video));
                output.Dfr("presegmented " + Json.Line(new JsonObject
                {
                    ["video"] = video, ["ok"] = true, ["waited_s"] = 0.0, ["seconds"] = 0.0, ["found"] = true,
                }));
            }
            else
            {
                presegmentFailure ??= "first_frame_unreadable";
                output.Dfr("presegmented " + Json.Line(new JsonObject
                {
                    ["video"] = video, ["ok"] = false, ["error"] = "첫 장을 읽지 못했습니다", ["reason"] = "first_frame_unreadable",
                }));
            }
        }
    }

    void Analyze(WorkerMessage.Analysis request)
    {
        var clock = Stopwatch.StartNew();
        output.Line("[fake] request " + request.Line);
        string? problem = Check(request, out string[] videos, out string name);
        if (problem is not null)
        {
            Done(problem);
            return;
        }
        string outputDir = request.Output!;
        string cuffSide = request.CuffSide ?? "left";
        var dfr = new JsonObject
        {
            ["variant"] = options.Variant.Id,
            ["upstream_commit"] = options.Variant.UpstreamCommit,
            ["presegment"] = PresegmentStamp(videos),
            ["prestream"] = PrestreamStamp(videos),
        };

        Stage(0, "가짜 분석기: 준비", clock);
        Stage(1, $"가짜 분석기: 영상 읽기 ({name})", clock);
        if (name == "hang")
        {
            Thread.Sleep(60_000);
            return;
        }
        if (name == "crash")
        {
            errors.Write("Traceback (most recent call last)\nMemoryError\n");
            errors.Flush();
            Environment.Exit(3);
        }
        Stage(2, "가짜 분석기: 맥박 신호", clock);
        Stage(3, "가짜 분석기: 관류 계산", clock);
        Stage(4, "가짜 분석기: 커프 단계", clock);
        Stage(5, $"가짜 분석기: 결과 파일 -> {outputDir}/", clock, wait: false);
        if (name == "fail")
        {
            Done("left.avi 가 없습니다: C:\\촬영\\left.avi\n두 번째 줄");
            return;
        }
        if (name == "nosummary")
        {
            Done(null);
            return;
        }

        bool cuffLevel = request.Classify && request.Template == "v106w";
        dfr["timing"] = new JsonObject { ["done_s"] = Seconds(clock) };
        JsonObject summary = FixedResult.Summary(name, cuffSide, cuffLevel, dfr);
        ResultFiles.WriteValues(outputDir, name, summary, cuffSide);
        if (!request.DeferFigures) ResultFiles.WriteFigures(outputDir, name, request.Gif);
        output.Line($"Done  [{Math.Round(clock.Elapsed.TotalSeconds):0} s]");
        Done(null);
        if (!request.DeferFigures) return;

        Thread.Sleep(options.StageDelayMs);
        ResultFiles.WriteFigures(outputDir, name, request.Gif);
        double figures = Seconds(clock);
        try
        {
            ((JsonObject)dfr["timing"]!)["figures_s"] = figures;
            ResultFiles.ReplaceJson(ResultFiles.SummaryPath(outputDir, name), summary);
        }
        catch (IOException error)
        {
            output.Dfr($"stamp-failed {error.Message}");
        }
        output.Dfr("figures " + Json.Line(new JsonObject { ["ok"] = true, ["error"] = "", ["seconds"] = figures }));
    }

    // 관류 분석 프로그램이 분석 전에 거절하는 요청을 같은 문장으로 거절합니다. 영상 머리글의 장 수 확인은 영상을 풀어야 해서 하지 않습니다.
    static string? Check(WorkerMessage.Analysis request, out string[] videos, out string name)
    {
        videos = [];
        name = "";
        if (request.RecDir is null || request.Output is null) return "rec_dir 과 output 이 있어야 합니다.";
        if ((request.LeftVideo is null) != (request.RightVideo is null)) return "left_video 와 right_video 는 둘 다 주거나 둘 다 빼야 합니다.";
        videos = request.LeftVideo is not null
            ? [request.LeftVideo, request.RightVideo!]
            : [Path.Combine(request.RecDir, "left.avi"), Path.Combine(request.RecDir, "right.avi")];
        for (int i = 0; i < 2; i++)
        {
            if (!File.Exists(videos[i])) return $"{FixedResult.Sides[i]} 영상이 없습니다: {videos[i]}";
        }
        string dir = Path.GetFullPath(request.RecDir).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        name = request.Name ?? $"{Path.GetFileName(Path.GetDirectoryName(dir))}_{Path.GetFileName(dir)}";
        if (request.PadClassifier) return "이 패키지에는 이진 PAD 판정 모델이 없습니다. pad_classifier 를 빼고 요청해 주세요.";
        return null;
    }

    JsonObject PresegmentStamp(string[] videos)
    {
        if (!options.Presegment) return new JsonObject { ["enabled"] = false };
        string? reason = videos.All(v => presegmented.Contains(Path.GetFullPath(v)))
            ? null
            : presegmentFailure ?? (anyPresegment ? "first_frame_mismatch" : "not_requested");
        return new JsonObject { ["enabled"] = true, ["reason"] = reason };
    }

    JsonObject PrestreamStamp(string[] videos)
    {
        if (!options.Prestream) return new JsonObject { ["enabled"] = false };
        string[] keys = videos.Select(Path.GetFullPath).ToArray();
        if (!keys.All(prestreamJobs.Contains)) return new JsonObject { ["used"] = false, ["reason"] = "not_started" };
        prestreamJobs.ExceptWith(keys);
        bool used = keys.All(presegmented.Contains);
        foreach (string video in videos)
        {
            bool ok = presegmented.Contains(Path.GetFullPath(video));
            output.Dfr("prestreamed " + Json.Line(new JsonObject
            {
                ["video"] = video,
                ["ok"] = ok,
                ["frames"] = ok ? FixedResult.Frames : 0,
                ["error"] = ok ? null : "미리 분할한 첫 장이 없습니다",
                ["reason"] = ok ? null : "no_presegment",
            }));
        }
        return new JsonObject { ["used"] = used, ["reason"] = used ? null : "no_presegment" };
    }

    void Stage(int step, string message, Stopwatch clock, bool wait = true)
    {
        output.Line($"[{step}/5] {message}  [{Math.Round(clock.Elapsed.TotalSeconds):0} s]");
        if (wait) Thread.Sleep(options.StageDelayMs);
    }

    static double Seconds(Stopwatch clock) => Math.Round(clock.Elapsed.TotalSeconds, 2);
}

// 유휴 종료 스레드와 분석이 같은 출력에 쓰므로 줄마다 잠급니다.
internal sealed class OutputLines(TextWriter writer)
{
    readonly object gate = new();

    public void Line(string text)
    {
        lock (gate)
        {
            writer.Write(text);
            writer.Write('\n');
            writer.Flush();
        }
    }

    public void Dfr(string rest) => Line("[dfr] " + rest);
}

// 분석 중에는 세지 않습니다. 한 건이 유휴 시간보다 오래 걸려도 도중에 끝나지 않습니다.
internal sealed class IdleExit(double seconds, OutputLines output)
{
    long touched = Environment.TickCount64;
    volatile bool busy;

    public void Start()
    {
        if (seconds <= 0) return;
        new Thread(Watch) { IsBackground = true }.Start();
    }

    public void Touch() => Interlocked.Exchange(ref touched, Environment.TickCount64);

    public void Busy(Action work)
    {
        busy = true;
        try
        {
            work();
        }
        finally
        {
            busy = false;
            Touch();
        }
    }

    void Watch()
    {
        while (true)
        {
            Thread.Sleep(100);
            if (busy || Environment.TickCount64 - Interlocked.Read(ref touched) < seconds * 1000) continue;
            output.Dfr("idle-exit");
            Environment.Exit(0);
        }
    }
}

internal static class Json
{
    // 요약 파일은 파이썬 json.dump(ensure_ascii=True)처럼 ASCII 밖 글자를 \uXXXX로 씁니다.
    static readonly JsonSerializerOptions Indented = new() { WriteIndented = true, Encoder = JavaScriptEncoder.Default };

    // 출력 줄은 관류 분석 프로그램(json.dumps(ensure_ascii=False))처럼 한글을 UTF-8 그대로 씁니다.
    static readonly JsonSerializerOptions Compact = new() { Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping };

    public static string Line(JsonNode node) => node.ToJsonString(Compact);

    public static string Document(JsonNode node) => node.ToJsonString(Indented);
}

internal static class ResultFiles
{
    public static string SummaryPath(string output, string name) => Path.Combine(output, $"{name}_summary.json");

    public static void WriteValues(string output, string name, JsonObject summary, string cuffSide)
    {
        Directory.CreateDirectory(output);
        ReplaceJson(SummaryPath(output, name), summary);
        Npz.Write(Path.Combine(output, $"{name}_perfusion.npz"), FixedResult.PerfusionArrays(cuffSide));
        Npz.Write(Path.Combine(output, $"{name}_waveforms.npz"), FixedResult.WaveformArrays());
    }

    public static void WriteFigures(string output, string name, bool gif)
    {
        Png.Write(Path.Combine(output, $"{name}_segmentation.png"), Figures.Segmentation());
        Png.Write(Path.Combine(output, $"{name}_template.png"), Figures.TemplateCells());
        Png.Write(Path.Combine(output, $"{name}_pi_map.png"), Figures.MeanMap());
        if (gif)
        {
            IndexedImage[] frames = Enumerable.Range(0, FixedResult.Windows).Select(Figures.MapFrame).ToArray();
            Gif.Write(Path.Combine(output, $"{name}_perfusion.gif"), frames, delayCentiseconds: 80);
        }
    }

    // 관류 분석 프로그램과 같이 임시 파일에 쓴 뒤 이름을 바꿔 요약을 바꿉니다. 앱이 요약 파일을 열어 두고 있으면 Windows에서 실패합니다.
    public static void ReplaceJson(string path, JsonNode node)
    {
        string temp = path + ".tmp";
        File.WriteAllText(temp, Json.Document(node));
        File.Move(temp, path, overwrite: true);
    }
}
