namespace DiabeticFoot.Platform;

/// <summary>
/// 관류 분석 프로그램이 쓴 `&lt;name&gt;_perfusion.npz`에서 결과 창이 그래프로 그리는 값입니다.
/// </summary>
/// <remarks>
/// 맥파 템플릿은 영상 장마다 값이 하나이고, 나머지 열은 시간 창마다 값이 하나입니다. 좌우는 피험자 기준입니다.
/// 관류지수(PI) 계열 값은 퍼센트입니다.
/// 배열 이름과 모양은 관류 분석 프로그램(analysis/variants/&lt;판&gt;/plantar_perfusion.py)의 np.savez 호출이 정합니다.
/// </remarks>
public sealed record PerfusionDetail
{
    /// <summary>`time_s`: 시간 창 가운데 시각(초, 영상 첫 장 기준). 길이가 시간 창 수입니다.</summary>
    public IReadOnlyList<double> WindowCentersS { get; init; } = Array.Empty<double>();

    /// <summary>`template_left`: 왼발 맥파 템플릿. 영상 장마다 값 하나입니다.</summary>
    public IReadOnlyList<double> TemplateLeft { get; init; } = Array.Empty<double>();

    /// <summary>`template_right`: 오른발 맥파 템플릿. 길이는 TemplateLeft와 같습니다.</summary>
    public IReadOnlyList<double> TemplateRight { get; init; } = Array.Empty<double>();

    /// <summary>`fps`: 관류 분석 프로그램이 쓴 영상 fps. 없으면 NoNumber입니다. 템플릿의 가로축(초)은 장 번호 / fps입니다.</summary>
    public double Fps { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`template_corr`: 시간 창별 좌우 템플릿 상관계수. 없으면 비었습니다.</summary>
    public IReadOnlyList<double> TemplateCorr { get; init; } = Array.Empty<double>();

    /// <summary>`n_light_ref`: 시간 창별 조명 기준 영역 수. 0이면 그 창은 POS 맥파로 PI를 구했습니다. 없으면 비었습니다.</summary>
    public IReadOnlyList<int> LightRefCount { get; init; } = Array.Empty<int>();

    /// <summary>
    /// 왼발의 시간 창별 지표. 키는 지표 이름이고 배열 이름 `&lt;키&gt;_left`에서 왔습니다(키 순서).
    /// 관류 분석 프로그램이 쓰는 키: `pi`, `pi_toes`, `pi_fore`, `pi_mid`, `pi_heel`, `pi_green_legacy`, `pi_pos_lockin`,
    /// `pi_pos_std`, `pi_ref_std`. 관류 분석 프로그램 판에 따라 키가 더 있거나 빠질 수 있습니다. 값을 못 구한 창은 NaN입니다.
    /// </summary>
    public IReadOnlyDictionary<string, IReadOnlyList<double>> LeftSeries { get; init; } = new SortedDictionary<string, IReadOnlyList<double>>(StringComparer.Ordinal);

    /// <summary>오른발의 시간 창별 지표. 키는 LeftSeries와 같은 규칙으로 배열 이름 `&lt;키&gt;_right`에서 왔습니다.</summary>
    public IReadOnlyDictionary<string, IReadOnlyList<double>> RightSeries { get; init; } = new SortedDictionary<string, IReadOnlyList<double>>(StringComparer.Ordinal);

    /// <summary>
    /// `maps`: 시간 창별 관류 지도(관류지수 %, 발 영역 격자). 순서는 [발(0 왼발, 1 오른발)][시간 창][행][열]이고
    /// 발 영역 밖 칸은 NaN입니다. 없으면 비었습니다.
    /// </summary>
    /// <remarks>
    /// v6, v7은 영상의 발 영역(좌우반전 전)을 32 x 16으로 나눈 격자이고, v8은 표준 발 틀을 40 x 15로 나눈 격자입니다.
    /// v8 격자의 행은 발끝(0)에서 뒤꿈치까지 발 길이를 같은 간격으로 나누고, 열은 그 행의 발 폭을 같은 간격으로 나눕니다.
    /// </remarks>
    public IReadOnlyList<double> Maps { get; init; } = Array.Empty<double>();

    public int MapRows { get; init; }

    public int MapCols { get; init; }

    /// <summary>`map_valid`: v8 표준 발 틀 격자에서 PI를 계산하는 칸이면 1입니다. 순서는 [행][열]입니다. 없으면 비었습니다.</summary>
    public IReadOnlyList<byte> MapValid { get; init; } = Array.Empty<byte>();

    /// <summary>
    /// `outline`: v8 표준 발 틀의 윤곽입니다. OutlineU는 발끝 0에서 뒤꿈치 1까지의 길이 위치이고, OutlineLeft와
    /// OutlineRight는 그 위치의 왼쪽과 오른쪽 끝(발 길이를 1로 둔 가로 위치)입니다. 셋의 길이는 같습니다. 없으면 비었습니다.
    /// </summary>
    public IReadOnlyList<double> OutlineU { get; init; } = Array.Empty<double>();

    public IReadOnlyList<double> OutlineLeft { get; init; } = Array.Empty<double>();

    public IReadOnlyList<double> OutlineRight { get; init; } = Array.Empty<double>();

    /// <summary>v8 표준 발 틀 지도를 그릴 값(Maps, MapValid, Outline)이 모두 있으면 true입니다.</summary>
    public bool HasTemplateMap =>
        Maps.Count != 0 && MapValid.Count != 0 && OutlineU.Count >= 2 && OutlineLeft.Count == OutlineU.Count &&
        OutlineRight.Count == OutlineU.Count;

    /// <summary>관류 지도 한 칸의 값입니다. 범위를 벗어나면 NaN입니다.</summary>
    public double MapAt(int foot, int window, int row, int col)
    {
        long windows = WindowCentersS.Count;
        if (foot < 0 || foot > 1 || window < 0 || window >= windows || row < 0 || row >= MapRows || col < 0 || col >= MapCols)
        {
            return double.NaN;
        }
        var at = ((foot * windows + window) * MapRows + row) * MapCols + col;
        return at < Maps.Count ? Maps[(int)at] : double.NaN;
    }
}

public static partial class PerfusionResults
{
    /// <summary>
    /// 분석 결과 배열 파일의 경로입니다.
    /// </summary>
    /// <param name="outputDir">분석 요청의 결과 폴더</param>
    /// <param name="name">분석 요청의 결과 이름</param>
    /// <returns>`&lt;outputDir&gt;/&lt;name&gt;_perfusion.npz`. 구분자는 '/'입니다</returns>
    public static string PerfusionDetailPath(string outputDir, string name) => ResultPath(outputDir, name + "_perfusion.npz");

    /// <summary>
    /// 읽은 `.npz` 내용에서 결과 창 값을 꺼냅니다.
    /// </summary>
    /// <param name="contents"><see cref="NpyArchive.ReadNpz(string, out string)"/> 또는 <see cref="NpyArchive.ReadNpzBytes(ReadOnlySpan{byte}, out string)"/>의 결과</param>
    /// <param name="error">꺼내지 못하면 이유를 씁니다. 꺼냈으면 빈 문자열입니다</param>
    /// <returns>
    /// 꺼낸 값. `time_s`, `template_left`, `template_right`가 없거나 1차원이 아니거나 비었으면, 두 템플릿 길이가
    /// 다르면, `template_corr`, `n_light_ref`, 지표 배열 중 길이가 시간 창 수와 다른 것이 있으면 null
    /// </returns>
    /// <remarks>이름이 `_left`, `_right`로 끝나는 1차원 배열 중 템플릿이 아닌 것을 모두 지표로 읽습니다. 없는 지표는 맵에 없습니다.</remarks>
    public static PerfusionDetail? PerfusionDetailOf(NpzContents contents, out string error)
    {
        var (detail, why) = BuildDetail(contents);
        error = detail == null ? why : "";
        return detail;
    }

    public static PerfusionDetail? PerfusionDetailOf(NpzContents contents) => PerfusionDetailOf(contents, out _);

    /// <summary>
    /// 분석 결과 배열 파일을 읽습니다. ReadNpz 뒤 <see cref="PerfusionDetailOf(NpzContents, out string)"/>와 같습니다.
    /// </summary>
    /// <param name="path">`&lt;name&gt;_perfusion.npz` 경로</param>
    /// <param name="error">읽지 못하면 이유를 씁니다. 읽었으면 빈 문자열입니다</param>
    /// <returns>읽은 값. 파일을 열지 못하거나 형식이 틀리거나 PerfusionDetailOf가 실패하면 null</returns>
    /// <remarks>파일 입출력이 있으므로 화면 스레드에서 부르지 않습니다.</remarks>
    public static PerfusionDetail? ReadPerfusionDetail(string path, out string error)
    {
        var contents = NpyArchive.ReadNpz(path, out error);
        return contents == null ? null : PerfusionDetailOf(contents, out error);
    }

    public static PerfusionDetail? ReadPerfusionDetail(string path) => ReadPerfusionDetail(path, out _);

    private static (PerfusionDetail?, string) BuildDetail(NpzContents contents)
    {
        NpyArray? Find(string name) => contents.Arrays.TryGetValue(name, out var array) ? array : null;
        double[]? Vector(string name) => Find(name) is { Shape.Length: 1, Values.Length: > 0 } array ? array.Values : null;

        if (Vector("time_s") is not { } windowCenters) return (null, "결과 배열 파일에 time_s가 없거나 1차원 배열이 아닙니다");
        if (Vector("template_left") is not { } templateLeft || Vector("template_right") is not { } templateRight)
        {
            return (null, "결과 배열 파일에 template_left 또는 template_right가 없거나 1차원 배열이 아닙니다");
        }
        if (templateLeft.Length != templateRight.Length)
        {
            return (null, $"좌우 맥파 템플릿 길이가 다릅니다: 왼발 {templateLeft.Length}, 오른발 {templateRight.Length}");
        }
        var windows = windowCenters.Length;
        string? failure = null;
        // 배열이 없으면 비워 두고 성공, 길이가 시간 창 수와 다르면 실패입니다.
        double[] WindowSeries(string name)
        {
            if (Find(name) is not { } array) return [];
            if (array.Shape.Length != 1 || array.Values.Length != windows)
            {
                failure = $"{name}의 길이가 시간 창 수({windows})와 다릅니다";
                return [];
            }
            return array.Values;
        }

        var fps = double.NaN;
        if (Find("fps") is { Values.Length: 1 } fpsArray) fps = double.IsFinite(fpsArray.Values[0]) && fpsArray.Values[0] > 0.0 ? fpsArray.Values[0] : double.NaN;
        var corr = WindowSeries("template_corr");
        if (failure != null) return (null, failure);
        var lightRef = WindowSeries("n_light_ref");
        if (failure != null) return (null, failure);
        var lightRefCount = lightRef.Select(n => (int)n).ToArray();

        // 관류 지도는 있으면 씁니다. 모양이 (2, 창 수, 행, 열)이 아니면 다른 판의 관류 분석 프로그램으로 보고 비워 둡니다.
        double[] maps = [];
        var rows = 0;
        var cols = 0;
        if (Find("maps") is { Shape.Length: 4 } mapsArray && mapsArray.Shape[0] == 2 && mapsArray.Shape[1] == windows)
        {
            maps = mapsArray.Values;
            rows = (int)mapsArray.Shape[2];
            cols = (int)mapsArray.Shape[3];
        }
        // 표준 발 틀(v8)의 칸 표시와 윤곽입니다. 모양이 지도 격자와 맞지 않으면 틀 지도를 그리지 않도록 비워 둡니다.
        byte[] mapValid = [];
        if (Find("map_valid") is { Shape.Length: 2 } validArray && validArray.Shape[0] == rows && validArray.Shape[1] == cols)
        {
            mapValid = validArray.Values.Select(v => (byte)(v != 0.0 ? 1 : 0)).ToArray();
        }
        double[] outlineU = [];
        double[] outlineLeft = [];
        double[] outlineRight = [];
        if (Find("outline") is { Shape.Length: 2 } outline && outline.Shape[0] == 3 && outline.Shape[1] >= 2)
        {
            var n = (int)outline.Shape[1];
            outlineU = outline.Values[..n];
            outlineLeft = outline.Values[n..(2 * n)];
            outlineRight = outline.Values[(2 * n)..(3 * n)];
        }

        var leftSeries = new SortedDictionary<string, IReadOnlyList<double>>(StringComparer.Ordinal);
        var rightSeries = new SortedDictionary<string, IReadOnlyList<double>>(StringComparer.Ordinal);
        foreach (var (name, _) in contents.Arrays.OrderBy(pair => pair.Key, StringComparer.Ordinal))
        {
            if (name == "template_left" || name == "template_right") continue;
            foreach (var (tail, series) in new[] { ("_left", leftSeries), ("_right", rightSeries) })
            {
                if (name.Length <= tail.Length || !name.EndsWith(tail, StringComparison.Ordinal)) continue;
                var values = WindowSeries(name);
                if (failure != null) return (null, failure);
                series[name[..^tail.Length]] = values;
            }
        }
        return (new PerfusionDetail
        {
            WindowCentersS = windowCenters,
            TemplateLeft = templateLeft,
            TemplateRight = templateRight,
            Fps = fps,
            TemplateCorr = corr,
            LightRefCount = lightRefCount,
            LeftSeries = leftSeries,
            RightSeries = rightSeries,
            Maps = maps,
            MapRows = rows,
            MapCols = cols,
            MapValid = mapValid,
            OutlineU = outlineU,
            OutlineLeft = outlineLeft,
            OutlineRight = outlineRight,
        }, "");
    }
}
