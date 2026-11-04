using System.Text.Json;

namespace DiabeticFoot.FakeAnalyzer;

internal abstract record WorkerMessage
{
    // 줄 앞의 BOM(U+FEFF)은 떼지 않습니다. 관류 분석 프로그램(파이썬 json.loads)도 BOM이 붙은 줄을 읽지 못합니다.
    public static WorkerMessage Parse(string line)
    {
        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(line);
        }
        catch (JsonException error)
        {
            return new Unreadable($"요청을 읽지 못했습니다: {error.Message}");
        }
        using (document)
        {
            JsonElement root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object) return new Unreadable("요청을 읽지 못했습니다: JSON 객체가 아닙니다");
            if (Flag(root, "presegment")) return new Presegment(Text(root, "left_video"), Text(root, "right_video"));
            if (Flag(root, "preload")) return new Preload();
            return new Analysis(
                Line: line,
                RecDir: Text(root, "rec_dir"),
                Output: Text(root, "output"),
                Name: Text(root, "name"),
                LeftVideo: Text(root, "left_video"),
                RightVideo: Text(root, "right_video"),
                CuffSide: Text(root, "cuff_side"),
                Template: Text(root, "template") ?? "v106w",
                Classify: Flag(root, "classify"),
                PadClassifier: Flag(root, "pad_classifier"),
                Gif: Flag(root, "gif"),
                DeferFigures: Flag(root, "defer_figures"));
        }
    }

    static bool Flag(JsonElement root, string key) =>
        root.TryGetProperty(key, out JsonElement value) && value.ValueKind == JsonValueKind.True;

    static string? Text(JsonElement root, string key) =>
        root.TryGetProperty(key, out JsonElement value) && value.ValueKind == JsonValueKind.String && value.GetString() is { Length: > 0 } text
            ? text
            : null;

    public sealed record Preload : WorkerMessage;

    public sealed record Presegment(string? LeftVideo, string? RightVideo) : WorkerMessage;

    // 빈 문자열 값은 파이썬의 `request.get(...) or ...`처럼 없는 키와 같게 null입니다.
    public sealed record Analysis(
        string Line,
        string? RecDir,
        string? Output,
        string? Name,
        string? LeftVideo,
        string? RightVideo,
        string? CuffSide,
        string Template,
        bool Classify,
        bool PadClassifier,
        bool Gif,
        bool DeferFigures) : WorkerMessage;

    public sealed record Unreadable(string Error) : WorkerMessage;
}
