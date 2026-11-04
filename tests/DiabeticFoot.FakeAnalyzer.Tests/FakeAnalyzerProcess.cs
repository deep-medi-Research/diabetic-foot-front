using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace DiabeticFoot.FakeAnalyzer.Tests;

internal sealed class FakeAnalyzerProcess : IDisposable
{
    public static readonly string Dll = Path.Combine(AppContext.BaseDirectory, "DiabeticFoot.FakeAnalyzer.dll");

    readonly Process process;
    readonly BlockingCollection<string> lines = new();
    readonly StringBuilder errors = new();

    FakeAnalyzerProcess(IEnumerable<string> args)
    {
        // dotnet test는 자식 프로세스에 DOTNET_HOST_PATH를 줍니다. 없으면 같이 복사된 실행 파일로 띄웁니다.
        string? host = Environment.GetEnvironmentVariable("DOTNET_HOST_PATH");
        var start = new ProcessStartInfo(host ?? Path.ChangeExtension(Dll, OperatingSystem.IsWindows() ? ".exe" : null))
        {
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            StandardInputEncoding = new UTF8Encoding(false),
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
        };
        if (host is not null) start.ArgumentList.Add(Dll);
        foreach (string arg in args) start.ArgumentList.Add(arg);
        process = new Process { StartInfo = start };
        process.OutputDataReceived += (_, e) =>
        {
            if (e.Data is null) lines.CompleteAdding();
            else lines.Add(e.Data);
        };
        process.ErrorDataReceived += (_, e) =>
        {
            if (e.Data is null) return;
            lock (errors) errors.AppendLine(e.Data);
        };
        process.Start();
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
    }

    /// <summary>앱과 같은 명령줄(실행 파일, 스크립트 경로, --idle-exit 0, ..., --variant fake)로 띄웁니다. 단계 대기는 0입니다.</summary>
    public static FakeAnalyzerProcess StartLikeApp(params string[] extra) =>
        new([Dll, "--idle-exit", "0", .. extra, "--variant", "fake", "--stage-ms", "0"]);

    public static FakeAnalyzerProcess Start(params string[] args) => new(args);

    public string Errors
    {
        get
        {
            lock (errors) return errors.ToString();
        }
    }

    public void Send(string line)
    {
        process.StandardInput.Write(line + "\n");
        process.StandardInput.Flush();
    }

    public void SendBytes(byte[] bytes)
    {
        process.StandardInput.BaseStream.Write(bytes);
        process.StandardInput.BaseStream.Flush();
    }

    public void CloseInput() => process.StandardInput.Close();

    /// <summary>다음 줄입니다. 시간 안에 줄이 없거나 출력이 닫히면 null입니다.</summary>
    public string? Next(int timeoutMs = 10_000) =>
        lines.TryTake(out string? line, timeoutMs) ? line : null;

    public string NextRequired(int timeoutMs = 10_000) =>
        Next(timeoutMs) ?? throw new TimeoutException($"{timeoutMs}ms 안에 줄이 없습니다. 표준 오류: {Errors}");

    /// <summary>조건에 맞는 줄까지 읽습니다. 돌려주는 목록의 마지막 줄이 그 줄입니다.</summary>
    public List<string> ReadThrough(Func<string, bool> stop, int timeoutMs = 10_000)
    {
        var seen = new List<string>();
        var clock = Stopwatch.StartNew();
        while (true)
        {
            int left = (int)Math.Max(1, timeoutMs - clock.ElapsedMilliseconds);
            string line = NextRequired(left);
            seen.Add(line);
            if (stop(line)) return seen;
        }
    }

    public int? WaitForExit(int timeoutMs = 10_000)
    {
        if (!process.WaitForExit(timeoutMs)) return null;
        process.WaitForExit();
        return process.ExitCode;
    }

    public void Dispose()
    {
        if (!process.HasExited) process.Kill(entireProcessTree: true);
        process.Dispose();
    }
}

/// <summary>관류 분석 프로그램 출력 줄의 규칙입니다. 앱의 줄 해석(perfusion_analysis.cc ParseAnalyzerLine)과 같습니다.</summary>
internal static partial class AnalyzerLines
{
    [GeneratedRegex(@"^\[(\d{1,6})/(\d{1,6})\]")]
    private static partial Regex StagePattern();

    public static bool IsDfr(string line, string kind) =>
        line == "[dfr] " + kind || line.StartsWith("[dfr] " + kind + " ", StringComparison.Ordinal);

    /// <summary>`[dfr] &lt;kind&gt; {JSON}` 줄의 JSON입니다.</summary>
    public static JsonElement Json(string line, string kind)
    {
        Assert.True(IsDfr(line, kind), $"[dfr] {kind} 줄이 아닙니다: {line}");
        return JsonDocument.Parse(line[("[dfr] " + kind + " ").Length..]).RootElement.Clone();
    }

    /// <summary>줄 맨 앞의 `[n/m]`이면 단계 번호와 전체 단계 수입니다.</summary>
    public static (int Step, int Total)? Stage(string line)
    {
        Match match = StagePattern().Match(line);
        return match.Success ? (int.Parse(match.Groups[1].Value), int.Parse(match.Groups[2].Value)) : null;
    }

    public static bool IsDone(string line) => IsDfr(line, "done");
}

/// <summary>테스트마다 따로 쓰는 촬영 폴더입니다. 경로에 한글이 들어 있고, 좌우 영상 파일(내용 없음)이 있습니다.</summary>
internal sealed class Shot : IDisposable
{
    readonly string root;

    public Shot(string stem = "102528")
    {
        root = Path.Combine(Path.GetTempPath(), "fake-analyzer-tests", Guid.NewGuid().ToString("N"));
        Folder = Path.Combine(root, "홍길동_19551103", stem);
        Directory.CreateDirectory(Folder);
        Stem = stem;
        LeftVideo = Path.Combine(Folder, $"{stem}_left.mkv");
        RightVideo = Path.Combine(Folder, $"{stem}_right.mkv");
        File.WriteAllBytes(LeftVideo, [1]);
        File.WriteAllBytes(RightVideo, [1]);
    }

    public string Folder { get; }
    public string Stem { get; }
    public string LeftVideo { get; }
    public string RightVideo { get; }
    public string Output => Path.Combine(Folder, "perfusion", "fake");

    public string ResultFile(string suffix) => Path.Combine(Output, Stem + suffix);

    /// <summary>
    /// 앱이 보내는 요청 한 줄입니다. 키와 순서, 값은 C++ 앱의 BuildPerfusionRequestLine과 같고, 한글은 UTF-8 그대로입니다.
    /// </summary>
    public string Request(Action<JsonObject>? change = null, string? name = null)
    {
        var request = new JsonObject
        {
            ["rec_dir"] = Folder,
            ["left_video"] = LeftVideo,
            ["right_video"] = RightVideo,
            ["left_frames"] = 900,
            ["right_frames"] = 900,
            ["output"] = Output,
            ["name"] = name ?? Stem,
            ["cuff_side"] = "none",
            ["template"] = "v106w",
            ["classify"] = true,
            ["pad_classifier"] = false,
            ["gif"] = true,
            ["defer_figures"] = true,
        };
        change?.Invoke(request);
        return request.ToJsonString(new JsonSerializerOptions { Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping });
    }

    public void Dispose()
    {
        try
        {
            Directory.Delete(root, recursive: true);
        }
        catch (IOException)
        {
        }
    }
}
