using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

// 원본 test_perfusion_detail.cc의 시험입니다(결과 창이 그래프로 그리는 .npz 값).
public class PerfusionDetailTests
{
    private static string Data(string name) => Path.Combine(AppContext.BaseDirectory, "Data", name);

    private static NpyArray Vector(params double[] values) => new([values.Length], values);

    // 관류 분석 프로그램 출력과 같은 이름의 배열을 시간 창 3개, 영상 6장 크기로 만듭니다.
    private static NpzContents Minimal()
    {
        var c = new NpzContents();
        c.Arrays["time_s"] = Vector(5.0, 7.0, 9.0);
        c.Arrays["template_left"] = Vector(0, 1, 0, -1, 0, 1);
        c.Arrays["template_right"] = Vector(0, 1, 0, -1, 0, 1);
        return c;
    }

    private static void Near(double expected, double actual, double tolerance) =>
        Assert.True(Math.Abs(expected - actual) <= tolerance, $"expected {expected}, actual {actual}");

    [Fact]
    public void PathMirrorsGifPath()
    {
        Assert.Equal("out/154007_perfusion.npz", PerfusionResults.PerfusionDetailPath("out", "154007"));
        Assert.Equal("out/a_perfusion.npz", PerfusionResults.PerfusionDetailPath("out/", "a"));
        Assert.Equal("C:\\out\\a_perfusion.npz", PerfusionResults.PerfusionDetailPath("C:\\out\\", "a"));
    }

    [Fact]
    public void ReadsRequiredArraysOnly()
    {
        var d = PerfusionResults.PerfusionDetailOf(Minimal(), out var error);
        Assert.True(d is not null, error);
        Assert.Equal(new[] { 5.0, 7.0, 9.0 }, d!.WindowCentersS);
        Assert.Equal(6, d.TemplateLeft.Count);
        Assert.True(double.IsNaN(d.Fps));
        Assert.Empty(d.TemplateCorr);
        Assert.Empty(d.LightRefCount);
        Assert.Empty(d.LeftSeries);
        Assert.Empty(d.RightSeries);
    }

    [Fact]
    public void SplitsMetricsByFoot()
    {
        var c = Minimal();
        c.Arrays["fps"] = new NpyArray([], [30.0]);
        c.Arrays["template_corr"] = Vector(0.9, 0.8, 0.7);
        c.Arrays["n_light_ref"] = Vector(6, 0, 6);
        c.Arrays["pi_left"] = Vector(0.1, 0.2, 0.3);
        c.Arrays["pi_right"] = Vector(0.4, 0.5, 0.6);
        c.Arrays["pi_toes_left"] = Vector(1, double.NaN, 3);
        c.Arrays["maps"] = new NpyArray([2, 3, 1, 1], new double[6]);
        var d = PerfusionResults.PerfusionDetailOf(c, out var error);
        Assert.True(d is not null, error);
        Assert.Equal(30.0, d!.Fps);
        Assert.Equal(new[] { 0.9, 0.8, 0.7 }, d.TemplateCorr);
        Assert.Equal(new[] { 6, 0, 6 }, d.LightRefCount);
        Assert.Equal(2, d.LeftSeries.Count);
        Assert.Equal(new[] { 0.1, 0.2, 0.3 }, d.LeftSeries["pi"]);
        Assert.True(double.IsNaN(d.LeftSeries["pi_toes"][1]));
        Assert.Single(d.RightSeries);
        Assert.Equal(new[] { 0.4, 0.5, 0.6 }, d.RightSeries["pi"]);
    }

    [Fact]
    public void RejectsMissingOrInconsistentArrays()
    {
        var c = Minimal();
        c.Arrays.Remove("time_s");
        Assert.Null(PerfusionResults.PerfusionDetailOf(c, out var error));
        Assert.Contains("time_s", error);

        c = Minimal();
        c.Arrays.Remove("template_right");
        Assert.Null(PerfusionResults.PerfusionDetailOf(c, out error));
        Assert.Contains("template", error);

        c = Minimal();
        c.Arrays["template_right"] = Vector(0, 1, 0);
        Assert.Null(PerfusionResults.PerfusionDetailOf(c, out error));
        Assert.Contains("템플릿 길이", error);

        c = Minimal();
        c.Arrays["pi_left"] = Vector(0.1, 0.2);
        Assert.Null(PerfusionResults.PerfusionDetailOf(c, out error));
        Assert.Contains("pi_left", error);

        c = Minimal();
        c.Arrays["template_corr"] = Vector(0.9);
        Assert.Null(PerfusionResults.PerfusionDetailOf(c, out error));
        Assert.Contains("template_corr", error);
    }

    [Fact]
    public void ReadsAnalyzerOutputFile()
    {
        var d = PerfusionResults.ReadPerfusionDetail(Data("154007_perfusion.npz"), out var error);
        Assert.True(d is not null, error);
        Assert.Equal(11, d!.WindowCentersS.Count);
        Assert.Equal(5.0, d.WindowCentersS[0]);
        Assert.Equal(25.0, d.WindowCentersS[^1]);
        Assert.Equal(900, d.TemplateLeft.Count);
        Assert.Equal(900, d.TemplateRight.Count);
        Assert.Equal(30.0, d.Fps);
        Assert.Equal(11, d.TemplateCorr.Count);
        Near(0.95329, d.TemplateCorr[0], 1e-5);
        Assert.Equal(Enumerable.Repeat(6, 11), d.LightRefCount);
        string[] keys = ["pi", "pi_fore", "pi_green_legacy", "pi_heel", "pi_mid", "pi_pos_lockin", "pi_pos_std", "pi_ref_std", "pi_toes"];
        foreach (var series in new[] { d.LeftSeries, d.RightSeries })
        {
            Assert.Equal(keys.Length, series.Count);
            foreach (var key in keys)
            {
                Assert.True(series.ContainsKey(key), key);
                Assert.True(series[key].Count == 11, key);
            }
        }
        Near(0.0116510, d.LeftSeries["pi"][0], 1e-6);
        Near(0.0192766, d.RightSeries["pi_toes"][10], 1e-6);
        // 관류 지도: 좌우 2 x 창 11 x 32 x 16 격자. 발 밖은 NaN이고 첫 창 왼발의 7행 11열은 발 안입니다.
        Assert.Equal(32, d.MapRows);
        Assert.Equal(16, d.MapCols);
        Assert.Equal(2 * 11 * 32 * 16, d.Maps.Count);
        Assert.True(double.IsNaN(d.MapAt(0, 0, 0, 0)));
        Assert.True(d.MapAt(0, 0, 7, 11) > 0.02);
        Assert.True(double.IsNaN(d.MapAt(2, 0, 7, 11)));
        Assert.True(double.IsNaN(d.MapAt(0, 11, 7, 11)));
    }

    // v8의 perfusion.npz: 관류 지도가 표준 발 틀 40x15이고, 창별 값은 pi와 부위 네 개뿐입니다. 지도 칸 수(map_valid)와
    // 발 윤곽(outline)은 _left/_right로 끝나지 않아 창별 값으로 읽지 않습니다.
    [Fact]
    public void ReadsV8TemplateMaps()
    {
        var d = PerfusionResults.ReadPerfusionDetail(Data("160307_v8_perfusion.npz"), out var error);
        Assert.True(d is not null, error);
        Assert.Equal(11, d!.WindowCentersS.Count);
        Assert.Equal(900, d.TemplateLeft.Count);
        Near(0.5628579592469367, d.TemplateCorr[0], 1e-12);
        Assert.Equal(Enumerable.Repeat(6, 11), d.LightRefCount);
        string[] keys = ["pi", "pi_fore", "pi_heel", "pi_mid", "pi_toes"];
        foreach (var series in new[] { d.LeftSeries, d.RightSeries })
        {
            Assert.Equal(keys.Length, series.Count);
            foreach (var key in keys)
            {
                Assert.True(series.ContainsKey(key), key);
                Assert.True(series[key].Count == 11, key);
            }
        }
        Near(0.0376581592247184, d.LeftSeries["pi"][0], 1e-12);
        Near(0.12070892124406714, d.RightSeries["pi_toes"][10], 1e-12);
        Assert.Equal(40, d.MapRows);
        Assert.Equal(15, d.MapCols);
        Assert.True(double.IsNaN(d.MapAt(0, 0, 0, 0)));
        Near(0.030002709958432044, d.MapAt(0, 0, 20, 7), 1e-12);
        // 표준 발 틀: 계산 칸 518개와 윤곽 41점(관류 분석 프로그램 v8 foot_cells.OUTLINE_*)
        Assert.True(d.HasTemplateMap);
        Assert.Equal(600, d.MapValid.Count);
        Assert.Equal(518, d.MapValid.Sum(v => v));
        Assert.Equal(41, d.OutlineU.Count);
        Assert.Equal(0.0, d.OutlineU[0], 15);
        Assert.Equal(1.0, d.OutlineU[^1], 15);
        Near(-0.137, d.OutlineLeft[20], 1e-3);
        Near(0.187, d.OutlineRight[20], 1e-3);
    }

    [Fact]
    public void SkipsTemplateOutlineThatDoesNotFitMaps()
    {
        var c = Minimal();
        c.Arrays["maps"] = new NpyArray([2, 3, 2, 2], Enumerable.Repeat(0.1, 24).ToArray());
        c.Arrays["map_valid"] = new NpyArray([3, 2], Enumerable.Repeat(1.0, 6).ToArray());
        c.Arrays["outline"] = new NpyArray([3, 2], [0, 1, -0.1, -0.1, 0.1, 0.1]);
        var d = PerfusionResults.PerfusionDetailOf(c, out var error);
        Assert.True(d is not null, error);
        Assert.Empty(d!.MapValid);
        Assert.Equal(2, d.OutlineU.Count);
        Assert.False(d.HasTemplateMap);
    }

    [Fact]
    public void ReportsMissingFile()
    {
        Assert.Null(PerfusionResults.ReadPerfusionDetail(Data("없는_perfusion.npz"), out var error));
        Assert.NotEmpty(error);
    }
}
