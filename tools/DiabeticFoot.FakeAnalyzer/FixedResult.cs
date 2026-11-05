using System.Text.Json.Nodes;

namespace DiabeticFoot.FakeAnalyzer;

/// <summary>
/// 가짜 분석기가 모든 요청에 내는 고정 결과입니다. 값은 실제 결과와 같은 범위가 되도록 식으로 만든 것이고, 실제 분석과 관계가 없습니다.
/// </summary>
/// <remarks>
/// 발 번호는 0이 왼발, 1이 오른발(피험자 기준)입니다. 요약의 평균값은 시간 창별 배열에서 계산해 두 파일이 서로 맞습니다.
/// </remarks>
internal static class FixedResult
{
    public const double Fps = 30.0;
    public const int Frames = 900;
    public const int WindowSeconds = 10;
    public const int StrideSeconds = 2;
    public const int Windows = (Frames / 30 - WindowSeconds) / StrideSeconds + 1;
    public const int MapRows = 40;
    public const int MapCols = 15;
    public const int OutlinePoints = 41;

    public static readonly string[] Sides = ["left", "right"];
    public static readonly string[] Regions = ["toes", "fore", "mid", "heel"];

    static readonly double[] FootPi = [0.12, 0.15];
    static readonly double[] RegionFactor = [1.15, 1.05, 1.0, 0.8];
    static readonly double[] WavePi = [0.118, 0.147];
    static readonly double[] PulseAmplitude = [0.13, 0.15];
    static readonly double[] PulsePhase = [0.0, 0.35];
    const double PulseHz = 1.1;

    static double Cycle(int k) => 2 * Math.PI * k / (Windows - 1);

    public static double[] TimeS { get; } = Enumerable.Range(0, Windows).Select(k => k * StrideSeconds + WindowSeconds / 2.0).ToArray();

    public static double[] Pi(int foot, int? region = null) =>
        Enumerable.Range(0, Windows)
            .Select(k => FootPi[foot] * (region is int r ? RegionFactor[r] : 1.0) * (1 + 0.08 * Math.Sin(Cycle(k))))
            .ToArray();

    public static double[] Template(int foot) =>
        Enumerable.Range(0, Frames).Select(i => Math.Sqrt(2) * Math.Sin(2 * Math.PI * PulseHz * i / Fps + PulsePhase[foot])).ToArray();

    public static double[] TemplateCorr { get; } = Enumerable.Range(0, Windows).Select(k => 0.92 + 0.04 * Math.Cos(Cycle(k))).ToArray();

    public static bool MapValid(int row, int col) =>
        !((row < 3 || row >= MapRows - 2) && (col == 0 || col == MapCols - 1));

    public static double Map(int foot, int window, int row, int col) =>
        MapValid(row, col) ? Pi(foot)[window] * (1.25 - 0.5 * row / (MapRows - 1)) : double.NaN;

    public static double MapTop { get; } = Enumerable.Range(0, 2).Max(f => Pi(f).Max()) * 1.25;

    // u는 발끝 0에서 뒤꿈치 1까지의 길이 위치이고, 폭은 발 길이를 1로 둔 값입니다.
    public static double HalfWidth(double u) => 0.07 + 0.14 * Math.Sin(Math.PI * Math.Sqrt(u));

    public static (double U, double Left, double Right) Outline(int i)
    {
        double u = (double)i / (OutlinePoints - 1);
        return (u, -HalfWidth(u), HalfWidth(u));
    }

    public static double[] Pulse(int foot) =>
        Enumerable.Range(0, Frames).Select(i => PulseAmplitude[foot] * Math.Sin(2 * Math.PI * PulseHz * i / Fps + PulsePhase[foot])).ToArray();

    public static double[] Ac(int foot)
    {
        double[] pulse = Pulse(foot);
        return pulse.Select((p, i) => p + 0.03 * Math.Sin(2 * Math.PI * 0.2 * i / Fps) + 0.015 * Math.Sin(2 * Math.PI * 4.7 * i / Fps)).ToArray();
    }

    public static IEnumerable<(string, NpyArray)> PerfusionArrays(string cuffSide)
    {
        var maps = new double[2 * Windows * MapRows * MapCols];
        var valid = new int[MapRows * MapCols];
        int at = 0;
        for (int foot = 0; foot < 2; foot++)
        {
            for (int k = 0; k < Windows; k++)
            {
                for (int r = 0; r < MapRows; r++)
                {
                    for (int c = 0; c < MapCols; c++) maps[at++] = Map(foot, k, r, c);
                }
            }
        }
        for (int r = 0; r < MapRows; r++)
        {
            for (int c = 0; c < MapCols; c++) valid[r * MapCols + c] = MapValid(r, c) ? 1 : 0;
        }
        var outline = new double[3 * OutlinePoints];
        for (int i = 0; i < OutlinePoints; i++)
        {
            (outline[i], outline[OutlinePoints + i], outline[2 * OutlinePoints + i]) = Outline(i);
        }

        yield return ("time_s", NpyArray.Float64(TimeS, Windows));
        yield return ("maps", NpyArray.Float64(maps, 2, Windows, MapRows, MapCols));
        yield return ("map_valid", NpyArray.Int32(valid, MapRows, MapCols));
        yield return ("outline", NpyArray.Float64(outline, 3, OutlinePoints));
        yield return ("template_left", NpyArray.Float64(Template(0), Frames));
        yield return ("template_right", NpyArray.Float64(Template(1), Frames));
        yield return ("template_corr", NpyArray.Float64(TemplateCorr, Windows));
        yield return ("n_light_ref", NpyArray.Int64(Enumerable.Repeat(6L, Windows).ToArray(), Windows));
        yield return ("fps", NpyArray.Float64Scalar(Fps));
        // 실제 파일에도 있는 문자열 배열입니다. 앱의 npz 읽기는 지원하지 않는 dtype을 건너뛰어야 합니다.
        yield return ("sides", NpyArray.Unicode(Sides, 2));
        yield return ("cuff_side", NpyArray.Unicode([cuffSide]));
        for (int foot = 0; foot < 2; foot++) yield return ($"pi_{Sides[foot]}", NpyArray.Float64(Pi(foot), Windows));
        for (int r = 0; r < Regions.Length; r++)
        {
            for (int foot = 0; foot < 2; foot++)
            {
                yield return ($"pi_{Regions[r]}_{Sides[foot]}", NpyArray.Float64(Pi(foot, r), Windows));
            }
        }
    }

    public static IEnumerable<(string, NpyArray)> WaveformArrays()
    {
        yield return ("fps", NpyArray.Float64Scalar(Fps));
        for (int foot = 0; foot < 2; foot++)
        {
            yield return ($"ac_{Sides[foot]}", NpyArray.Float64(Ac(foot), Frames));
            yield return ($"pulse_{Sides[foot]}", NpyArray.Float64(Pulse(foot), Frames));
            yield return ($"pi_{Sides[foot]}", NpyArray.Float64Scalar(WavePi[foot]));
        }
    }

    public static JsonObject Summary(string name, string cuffSide, bool cuffLevel, JsonObject dfr)
    {
        double piLeft = Pi(0).Average(), piRight = Pi(1).Average();
        var perSide = new JsonObject();
        for (int foot = 0; foot < 2; foot++)
        {
            var side = new JsonObject { ["pi"] = Pi(foot).Average() };
            for (int r = 0; r < Regions.Length; r++) side[$"pi_{Regions[r]}"] = Pi(foot, r).Average();
            perSide[Sides[foot]] = side;
        }
        return new JsonObject
        {
            ["name"] = name,
            ["fps"] = Fps,
            ["n_frames"] = Frames,
            ["window_s"] = WindowSeconds,
            ["window_stride_s"] = StrideSeconds,
            ["n_windows"] = Windows,
            ["cuff_side"] = cuffSide,
            ["pi_left"] = piLeft,
            ["pi_right"] = piRight,
            ["template_corr_lr"] = TemplateCorr.Average(),
            ["per_side"] = perSide,
            ["pi_ratio_left_right"] = piLeft / piRight,
            ["classification"] = null,
            ["cuff_level"] = cuffLevel ? CuffLevel() : null,
            ["qc"] = Quality(),
            ["foot_mask"] = new JsonObject
            {
                ["left"] = FootMask(0.981),
                ["right"] = FootMask(0.978),
            },
            ["foot_cells"] = new JsonObject { ["common"] = 92, ["used"] = 90 },
            ["n_light_ref_min"] = 6,
            ["dfr"] = dfr,
        };
    }

    static JsonObject FootMask(double score) => new()
    {
        ["method"] = "fake",
        ["det_score"] = score,
        ["sam_iou"] = score,
        ["cells"] = 100,
    };

    static JsonObject CuffLevel() => new()
    {
        ["level"] = "0",
        ["level_ko"] = "정상",
        ["pad"] = false,
        ["pad_probability"] = 0.02,
        ["level_if_quality_ignored"] = "0",
        ["quality_ok"] = true,
        ["state_probabilities"] = new JsonObject { ["0"] = 0.86, ["V"] = 0.12, ["D"] = 0.015, ["O"] = 0.005 },
        ["rule"] = "가짜 분석기의 고정 결과",
        ["features"] = new JsonObject { ["hi_snr1_30"] = 18.0 },
        ["quality_snr_db"] = 11.5,
        ["setting"] = new JsonObject { ["mode"] = "manual", ["exposure_step"] = -9, ["gain"] = 1 },
        ["applicable"] = true,
    };

    static JsonObject Quality()
    {
        JsonObject Item(string code, string name, string severity, JsonNode value, string unit, string message, string action, string? reason) => new()
        {
            ["code"] = code,
            ["name_ko"] = name,
            ["severity"] = severity,
            ["value"] = value,
            ["unit"] = unit,
            ["threshold"] = "가짜 기준",
            ["message_ko"] = message,
            ["action_ko"] = action,
            ["reason"] = reason,
        };
        const string action = "가짜 분석기의 고정 안내입니다. 실제 분석에서는 여기에 조치 문장이 옵니다.";
        return new JsonObject
        {
            ["version"] = 2,
            ["status"] = "check",
            ["status_ko"] = "측정 완료 (확인 필요)",
            ["action_ko"] = action,
            ["items"] = new JsonArray(
                Item("camera_setting", "카메라 설정", "ok", "manual, 노출 -9, 게인 1", "", "", "", null),
                Item("foot_found", "발 찾기", "ok", "양발", "", "", "", null),
                Item("pulse", "맥박 신호", "warn", 9.8, "dB", "가짜 분석기의 고정 경고입니다.", action, "other")),
            ["thermal_contrast"] = new JsonObject { ["left"] = 140.0, ["right"] = 145.0 },
        };
    }
}
