using System.Globalization;
using System.Text.Json;

namespace DiabeticFoot.FakeAnalyzer;

/// <summary>관류 분석 프로그램과 같은 명령줄 인자입니다. `--stage-ms`(진행 단계 사이 대기)만 가짜 분석기에 더한 인자입니다.</summary>
internal sealed record WorkerOptions(Variant Variant, double IdleExitSeconds, bool Presegment, bool Prestream, int StageDelayMs)
{
    public const int DefaultStageDelayMs = 200;

    /// <summary>
    /// 명령줄을 읽습니다. 앱은 실행 파일 뒤에 스크립트 경로를 먼저 붙이므로, `--`로 시작하지 않는 첫 인자는 무시합니다.
    /// </summary>
    /// <returns>
    /// 읽은 값. 틀리면 null이고 <paramref name="error"/>에 이유를 씁니다. <paramref name="exitCode"/>는 파이썬과 같이
    /// 인자 오류(argparse)이면 2, 모르는 판(ValueError)이면 1입니다.
    /// </returns>
    public static WorkerOptions? Parse(IReadOnlyList<string> args, string variantsDir, out string error, out int exitCode)
    {
        error = "";
        exitCode = 2;
        string? variant = null;
        double idleExit = 600;
        bool presegment = false, prestream = false;
        int stageDelay = DefaultStageDelayMs;
        for (int i = args.Count > 0 && !args[0].StartsWith("--", StringComparison.Ordinal) ? 1 : 0; i < args.Count; i++)
        {
            string? Value() => i + 1 < args.Count ? args[++i] : null;
            switch (args[i])
            {
                case "--variant":
                    variant = Value();
                    break;
                case "--idle-exit" when double.TryParse(Value(), NumberStyles.Float, CultureInfo.InvariantCulture, out double s):
                    idleExit = s;
                    break;
                case "--presegment-threads" when int.TryParse(Value(), CultureInfo.InvariantCulture, out _):
                    presegment = true;
                    break;
                case "--prestream":
                    // 파이썬 argparse의 nargs="?"와 같이 뒤의 숫자 하나는 값으로 먹습니다.
                    if (i + 1 < args.Count && int.TryParse(args[i + 1], CultureInfo.InvariantCulture, out _)) i++;
                    presegment = prestream = true;
                    break;
                case "--stage-ms" when int.TryParse(Value(), CultureInfo.InvariantCulture, out int ms) && ms >= 0:
                    stageDelay = ms;
                    break;
                default:
                    error = $"usage: dfr_worker --variant VARIANT [--idle-exit S] [--presegment-threads N] [--prestream [N]]\n" +
                            $"error: 인자를 읽지 못했습니다: {args[i]}";
                    return null;
            }
        }
        if (variant is null)
        {
            error = "usage: dfr_worker --variant VARIANT\nerror: the following arguments are required: --variant";
            return null;
        }
        Variant? found = Variant.Load(variantsDir, variant, out error);
        exitCode = 1;
        return found is null ? null : new WorkerOptions(found, idleExit, presegment, prestream, stageDelay);
    }
}

internal sealed record Variant(string Id, string UpstreamCommit)
{
    public static Variant? Load(string variantsDir, string id, out string error)
    {
        error = "";
        string[] known = Directory.Exists(variantsDir)
            ? Directory.GetDirectories(variantsDir)
                .Where(d => File.Exists(Path.Combine(d, "VARIANT.json")))
                .Select(d => Path.GetFileName(d))
                .Order(StringComparer.Ordinal)
                .ToArray()
            : [];
        if (!known.Contains(id))
        {
            error = $"ValueError: --variant 는 {(known.Length > 0 ? string.Join(", ", known) : "(판 없음)")} 중 하나여야 합니다: '{id}'";
            return null;
        }
        using JsonDocument json = JsonDocument.Parse(File.ReadAllText(Path.Combine(variantsDir, id, "VARIANT.json")));
        string commit = json.RootElement.TryGetProperty("upstream", out JsonElement upstream) &&
                        upstream.TryGetProperty("commit", out JsonElement c) && c.ValueKind == JsonValueKind.String
            ? c.GetString()!
            : "";
        return new Variant(id, commit);
    }
}
