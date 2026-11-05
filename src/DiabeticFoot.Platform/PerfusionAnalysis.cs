using System.Text;

namespace DiabeticFoot.Platform;

/// <summary>
/// 관류 분석 프로그램에 보내는 관류 분석 요청 하나입니다.
/// 영상 경로는 둘 다 주거나 둘 다 비웁니다. 둘 다 비우면 관류 분석 프로그램은 촬영 폴더의 left.avi, right.avi를 찾습니다.
/// 장 수를 주면 관류 분석 프로그램은 영상 머리글의 장 수와 다를 때 분석을 거부합니다.
/// 열화상은 연구 코드가 촬영 폴더에서 thermal_{left,right}.npy 이름으로 찾으므로 경로 키가 없습니다.
/// </summary>
public sealed record PerfusionRequest
{
    /// <summary>촬영 폴더. 요청 키 rec_dir입니다.</summary>
    public string RecordingDir { get; init; } = "";

    /// <summary>왼발 영상 경로(피험자 기준). 비어 있으면 요청 키 left_video를 넣지 않습니다.</summary>
    public string LeftVideo { get; init; } = "";

    /// <summary>오른발 영상 경로(피험자 기준). 비어 있으면 요청 키 right_video를 넣지 않습니다.</summary>
    public string RightVideo { get; init; } = "";

    /// <summary>왼발 영상에 쓴 장 수(사이드카 기록). 없으면 요청 키 left_frames를 넣지 않습니다.</summary>
    public long? LeftFrames { get; init; }

    /// <summary>오른발 영상에 쓴 장 수(사이드카 기록). 없으면 요청 키 right_frames를 넣지 않습니다.</summary>
    public long? RightFrames { get; init; }

    /// <summary>결과 폴더. 요청 키 output입니다.</summary>
    public string OutputDir { get; init; } = "";

    /// <summary>결과 파일 이름의 앞부분. 비우면 관류 분석 프로그램이 폴더 이름으로 짓습니다. 요청 키 name입니다.</summary>
    public string Name { get; init; } = "";

    /// <summary>커프를 감은 발입니다. 요청 키 cuff_side입니다.</summary>
    public string CuffSide { get; init; } = "none";

    /// <summary>맥동 템플릿입니다. 요청 키 template입니다.</summary>
    public string PulseTemplate { get; init; } = "v106w";

    /// <summary>판정 단계를 돌릴지. 끄면 커프 단계도 내지 않습니다. 요청 키 classify입니다.</summary>
    public bool Classify { get; init; } = true;

    /// <summary>
    /// 이진 PAD 판정(P(PAD 의심))도 낼지. 끄면 커프 단계만 내고 판정용 템플릿과 열화상 준비를 건너뛰어 분석이 빨라집니다.
    /// 연구 쪽 요청으로 2026-10-26부터 기본값이 false입니다. 요청 키 pad_classifier입니다.
    /// </summary>
    public bool PadClassifier { get; init; }

    /// <summary>관류 지도 애니메이션(GIF)을 만들지. 요청 키 gif입니다.</summary>
    public bool Gif { get; init; } = true;

    /// <summary>
    /// 그림(PNG)과 GIF를 분석 완료 뒤에 그릴지. 켜면 요약과 npz를 쓴 뒤 done을 찍고, 그림을 다 그리면 figures를 찍습니다.
    /// 노트북에서 결과가 약 2초 먼저 보입니다. 요청 키 defer_figures입니다.
    /// </summary>
    public bool DeferFigures { get; init; } = true;
}

/// <summary>관류 분석 프로그램 출력 한 줄의 종류입니다.</summary>
public enum AnalyzerLineKind
{
    /// <summary>진행 단계가 아닌 연구 스크립트의 글. 기록에만 남깁니다.</summary>
    Other,
    /// <summary>`[dfr] ready` 또는 `[dfr] ready {JSON}`: 요청을 받을 준비가 됨.</summary>
    Ready,
    /// <summary>`[dfr] preloaded`.</summary>
    Preloaded,
    /// <summary>`[dfr] preload-failed &lt;이유&gt;`.</summary>
    PreloadFailed,
    /// <summary>`[dfr] presegmented {JSON}`: 녹화 중 첫 장 미리 분할 한 발의 결과.</summary>
    Presegmented,
    /// <summary>줄 맨 앞의 `[n/m] 문구`: 진행 단계.</summary>
    Stage,
    /// <summary>들여 쓴 `  [k/n] 문구`: 진행 단계 안의 하위 단계(열화상 판정의 7단계).</summary>
    SubStage,
    /// <summary>`[dfr] done {"ok":...,"error":"..."}`: 요청 한 건의 끝.</summary>
    Done,
    /// <summary>`[dfr] figures {JSON}`: done 뒤로 미룬 그림과 GIF를 다 그림.</summary>
    Figures,
    /// <summary>`[dfr] idle-exit`: 관류 분석 프로그램이 쉬다가 스스로 끝남.</summary>
    IdleExit,
}

/// <summary>관류 분석 프로그램 출력 한 줄을 해석한 값입니다.</summary>
public sealed record AnalyzerLine
{
    public AnalyzerLineKind Kind { get; init; } = AnalyzerLineKind.Other;

    /// <summary>Stage일 때 단계 번호(0부터), SubStage일 때 하위 단계 번호(1부터)입니다.</summary>
    public int Step { get; init; }

    /// <summary>Stage일 때 전체 단계 수, SubStage일 때 하위 단계 수입니다.</summary>
    public int Total { get; init; }

    /// <summary>
    /// Stage와 SubStage의 문구(끝의 경과 시간 `[12 s]`, `(0 s)`를 뗀 것), PreloadFailed의 이유, Other의 원문,
    /// Presegmented와 Figures의 JSON입니다.
    /// </summary>
    public string Message { get; init; } = "";

    /// <summary>Ready 뒤에 JSON이 있을 때 worker_version 값입니다. 버전을 찍지 않는 관류 분석 프로그램이면 빈 문자열입니다.</summary>
    public string WorkerVersion { get; init; } = "";

    /// <summary>Ready 뒤에 JSON이 있을 때 upstream_commit 값입니다.</summary>
    public string UpstreamCommit { get; init; } = "";

    /// <summary>Done일 때 성공 여부입니다. 완료 JSON을 읽지 못하면 false입니다.</summary>
    public bool Ok { get; init; }

    /// <summary>Done일 때 관류 분석 프로그램이 적은 오류입니다. 완료 JSON을 읽지 못하면 그 이유입니다.</summary>
    public string Error { get; init; } = "";
}

/// <summary>요약 JSON에 관류 분석 프로그램이 넣은 버전 정보(dfr 항목)입니다.</summary>
public sealed record WorkerStamp(string WorkerVersion, string UpstreamCommit);

/// <summary>관류 분석 프로그램과 주고받는 요청 줄, 출력 줄, 결과 경로입니다.</summary>
public static class PerfusionAnalysis
{
    /// <summary>분석 결과를 쓰는 촬영 폴더 안 하위 폴더 이름입니다.</summary>
    public const string PerfusionFolderName = "perfusion";

    private const string Mark = "[dfr] ";

    /// <summary>
    /// 분석 요청 한 줄(JSON)을 만듭니다. 키 순서가 고정이고 끝에 줄바꿈 문자가 없으며, 한글은 그대로 두고
    /// 역슬래시, 따옴표, 제어 문자만 이스케이프합니다.
    /// 영상 경로가 한쪽만 있거나 장 수가 음수이면 null입니다. 관류 분석 프로그램도 거부하지만, 보내기 전에 막아 원인을 앱에서 알 수 있게 합니다.
    /// </summary>
    public static string? BuildPerfusionRequestLine(PerfusionRequest request, out string error)
    {
        error = "";
        if ((request.LeftVideo.Length == 0) != (request.RightVideo.Length == 0))
        {
            error = "좌우 영상 경로는 둘 다 주거나 둘 다 비워야 합니다";
            return null;
        }
        if (request.LeftFrames < 0 || request.RightFrames < 0)
        {
            error = "영상 장 수가 음수입니다";
            return null;
        }
        var line = new StringBuilder("{\"rec_dir\":");
        AppendQuoted(line, request.RecordingDir);
        if (request.LeftVideo.Length > 0)
        {
            line.Append(",\"left_video\":");
            AppendQuoted(line, request.LeftVideo);
        }
        if (request.RightVideo.Length > 0)
        {
            line.Append(",\"right_video\":");
            AppendQuoted(line, request.RightVideo);
        }
        if (request.LeftFrames is { } left) line.Append(",\"left_frames\":").Append(left);
        if (request.RightFrames is { } right) line.Append(",\"right_frames\":").Append(right);
        line.Append(",\"output\":");
        AppendQuoted(line, request.OutputDir);
        line.Append(",\"name\":");
        AppendQuoted(line, request.Name);
        line.Append(",\"cuff_side\":");
        AppendQuoted(line, request.CuffSide);
        line.Append(",\"template\":");
        AppendQuoted(line, request.PulseTemplate);
        line.Append(",\"classify\":").Append(Bool(request.Classify));
        line.Append(",\"pad_classifier\":").Append(Bool(request.PadClassifier));
        line.Append(",\"gif\":").Append(Bool(request.Gif));
        line.Append(",\"defer_figures\":").Append(Bool(request.DeferFigures));
        line.Append('}');
        return line.ToString();
    }

    public static string? BuildPerfusionRequestLine(PerfusionRequest request) => BuildPerfusionRequestLine(request, out _);

    /// <summary>분할 모델만 미리 올려 두는 요청 한 줄입니다. 관류 분석 프로그램은 `[dfr] preloaded`로 답하고 done을 찍지 않습니다.</summary>
    public static string BuildPreloadLine() => "{\"preload\":true}";

    /// <summary>
    /// 녹화 중인 발 영상의 첫 장을 미리 분할하게 하는 요청 한 줄입니다. 관류 분석 프로그램은 발마다
    /// `[dfr] presegmented {JSON}`으로 답하고 done을 찍지 않습니다. `--presegment-threads`와 함께 띄웠을 때만 분할하고,
    /// 아니면 실패 줄 하나로 답합니다.
    /// </summary>
    public static string BuildPresegmentLine(string leftVideo, string rightVideo)
    {
        var line = new StringBuilder("{\"presegment\":true,\"left_video\":");
        AppendQuoted(line, leftVideo);
        line.Append(",\"right_video\":");
        AppendQuoted(line, rightVideo);
        line.Append('}');
        return line.ToString();
    }

    /// <summary>
    /// 관류 분석 프로그램 출력 한 줄을 해석합니다. 알아보지 못한 줄은 Other입니다.
    /// 줄 맨 앞의 `[n/m]`은 진행 단계(Stage), 들여 쓴 `  [k/n]`은 하위 단계(SubStage)로 나누어 하위 단계가 진행 단계 번호를 되돌리지 않게 합니다.
    /// </summary>
    public static AnalyzerLine ParseAnalyzerLine(string line)
    {
        if (line.StartsWith(Mark, StringComparison.Ordinal))
        {
            var rest = line[Mark.Length..];
            if (rest == "ready" || rest.StartsWith("ready ", StringComparison.Ordinal))
            {
                // 버전 JSON은 덧붙인 정보라 읽지 못해도 준비 완료로 봅니다.
                var members = rest.Length > 6 ? new JsonReader(rest[6..].Trim(Whitespace)).Read() : null;
                return new AnalyzerLine
                {
                    Kind = AnalyzerLineKind.Ready,
                    WorkerVersion = members?.GetValueOrDefault("worker_version") ?? "",
                    UpstreamCommit = members?.GetValueOrDefault("upstream_commit") ?? "",
                };
            }
            if (rest == "preloaded") return new AnalyzerLine { Kind = AnalyzerLineKind.Preloaded };
            if (rest.StartsWith("preload-failed", StringComparison.Ordinal))
            {
                return new AnalyzerLine { Kind = AnalyzerLineKind.PreloadFailed, Message = rest["preload-failed".Length..].Trim(Whitespace) };
            }
            if (rest.StartsWith("presegmented ", StringComparison.Ordinal))
            {
                return new AnalyzerLine { Kind = AnalyzerLineKind.Presegmented, Message = rest["presegmented ".Length..].Trim(Whitespace) };
            }
            if (rest.StartsWith("figures ", StringComparison.Ordinal))
            {
                return new AnalyzerLine { Kind = AnalyzerLineKind.Figures, Message = rest["figures ".Length..].Trim(Whitespace) };
            }
            if (rest == "idle-exit") return new AnalyzerLine { Kind = AnalyzerLineKind.IdleExit };
            if (rest.StartsWith("done ", StringComparison.Ordinal)) return ParseDone(rest["done ".Length..]);
            return new AnalyzerLine { Message = line };
        }
        return TryParseStage(line) ?? new AnalyzerLine { Message = line };
    }

    /// <summary>요약 JSON 내용에서 관류 분석 프로그램 버전 정보를 읽습니다. dfr 항목이 없거나 JSON을 읽지 못하면 빈 값입니다.</summary>
    public static WorkerStamp ReadWorkerStamp(string summaryJson) => new(
        new JsonReader(summaryJson).FindString("dfr", "worker_version") ?? "",
        new JsonReader(summaryJson).FindString("dfr", "upstream_commit") ?? "");

    /// <summary>분석이 끝나면 생기는 요약 JSON의 경로 `&lt;outputDir&gt;/&lt;name&gt;_summary.json`입니다. 구분자는 '/'입니다.</summary>
    public static string PerfusionSummaryPath(string outputDir, string name)
    {
        var path = outputDir;
        if (path.Length > 0 && path[^1] != '/' && path[^1] != '\\') path += '/';
        return path + name + "_summary.json";
    }

    /// <summary>연구 코드 판 하나의 분석 결과 폴더 `&lt;shotFolder&gt;/perfusion/&lt;variant&gt;`입니다. variant가 비면 `&lt;shotFolder&gt;/perfusion`입니다.</summary>
    public static string PerfusionOutputDir(string shotFolder, string variant)
    {
        var dir = shotFolder;
        while (dir.Length > 1 && (dir[^1] == '/' || dir[^1] == '\\')) dir = dir[..^1];
        var output = JoinPath(dir, PerfusionFolderName);
        return variant.Length == 0 ? output : JoinPath(output, variant);
    }

    internal static string JoinPath(string dir, string name) =>
        dir.Length == 0 || dir[^1] == '/' || dir[^1] == '\\' ? dir + name : dir + "/" + name;

    private static readonly char[] Whitespace = [' ', '\t', '\r', '\n'];

    private static string Bool(bool value) => value ? "true" : "false";

    private static void AppendQuoted(StringBuilder line, string text)
    {
        line.Append('"');
        foreach (var c in text)
        {
            switch (c)
            {
                case '"': line.Append("\\\""); break;
                case '\\': line.Append("\\\\"); break;
                case '\n': line.Append("\\n"); break;
                case '\r': line.Append("\\r"); break;
                case '\t': line.Append("\\t"); break;
                case < ' ': line.Append("\\u").Append(((int)c).ToString("x4")); break;
                default: line.Append(c); break;
            }
        }
        line.Append('"');
    }

    private static AnalyzerLine ParseDone(string json)
    {
        var members = new JsonReader(json.Trim(Whitespace)).Read();
        if (members is null)
        {
            return new AnalyzerLine
            {
                Kind = AnalyzerLineKind.Done,
                Ok = false,
                Error = "관류 분석 프로그램의 완료 줄을 읽지 못했습니다: " + json,
            };
        }
        var hasOk = members.TryGetValue("ok", out var okText);
        var ok = okText == "true";
        var error = members.GetValueOrDefault("error") ?? "";
        if (!ok && error.Length == 0 && !hasOk) error = "완료 줄에 ok가 없습니다: " + json;
        return new AnalyzerLine { Kind = AnalyzerLineKind.Done, Ok = ok, Error = error };
    }

    // 줄 맨 앞의 `[n/m]`은 진행 단계, 들여 쓴 `  [k/n]`은 그 단계 안의 하위 단계입니다.
    private static AnalyzerLine? TryParseStage(string line)
    {
        var open = line.AsSpan().IndexOfAnyExcept(" \t");
        if (open < 0 || line[open] != '[') return null;
        var slash = line.IndexOf('/', open);
        var close = line.IndexOf(']', open);
        if (slash < 0 || close < 0 || slash > close) return null;
        if (!TryParseInt(line.AsSpan(open + 1, slash - open - 1), out var step) ||
            !TryParseInt(line.AsSpan(slash + 1, close - slash - 1), out var total))
        {
            return null;
        }
        if (total <= 0 || step > total) return null;
        return new AnalyzerLine
        {
            Kind = open == 0 ? AnalyzerLineKind.Stage : AnalyzerLineKind.SubStage,
            Step = step,
            Total = total,
            Message = StripElapsed(line[(close + 1)..].Trim(Whitespace)),
        };
    }

    private static bool TryParseInt(ReadOnlySpan<char> text, out int value)
    {
        value = 0;
        if (text.Length == 0 || text.Length > 6) return false;
        foreach (var c in text)
        {
            if (c is < '0' or > '9') return false;
            value = value * 10 + (c - '0');
        }
        return true;
    }

    // 연구 스크립트가 줄 끝에 출력하는 경과 시간(`[12 s]`, 하위 단계는 `(0 s)`)은 앱이 따로 재므로 제거합니다.
    private static string StripElapsed(string message)
    {
        if (message.Length == 0) return message;
        var open = message[^1] == ']' ? '[' : message[^1] == ')' ? '(' : '\0';
        if (open == '\0') return message;
        var start = message.LastIndexOf(" " + open, StringComparison.Ordinal);
        if (start < 0) return message;
        // 괄호 안이 숫자와 단위뿐일 때만 뗍니다. 문구 안의 괄호(예: "(started in the background)")는 둡니다.
        var inside = message.AsSpan(start + 2, message.Length - start - 3);
        if (inside.Length == 0 || inside.IndexOfAnyExcept("0123456789. s") >= 0) return message;
        return message[..start].Trim(Whitespace);
    }
}
