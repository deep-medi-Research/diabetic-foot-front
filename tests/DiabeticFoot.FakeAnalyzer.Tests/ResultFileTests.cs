using System.Text.Json;

namespace DiabeticFoot.FakeAnalyzer.Tests;

/// <summary>앱과 같은 요청 한 건(그림 미룸, GIF 켬)을 분석하고, 출력 줄과 결과 파일을 남깁니다.</summary>
public sealed class AnalyzedShot : IDisposable
{
    internal Shot Shot { get; } = new();
    internal List<string> Lines { get; }

    public AnalyzedShot()
    {
        using FakeAnalyzerProcess analyzer = FakeAnalyzerProcess.StartLikeApp();
        analyzer.NextRequired();
        analyzer.Send("{\"preload\":true}");
        analyzer.NextRequired();
        analyzer.Send(Shot.Request());
        Lines = analyzer.ReadThrough(line => AnalyzerLines.IsDfr(line, "figures"));
    }

    internal JsonElement Summary => JsonDocument.Parse(File.ReadAllText(Shot.ResultFile("_summary.json"))).RootElement;

    public void Dispose() => Shot.Dispose();
}

public sealed class ResultFileTests(AnalyzedShot analyzed) : IClassFixture<AnalyzedShot>
{
    readonly JsonElement summary = analyzed.Summary;
    int Windows => summary.GetProperty("n_windows").GetInt32();
    int Frames => summary.GetProperty("n_frames").GetInt32();

    [Fact]
    public void StagesRunFromZeroToFiveThenDoneThenFigures()
    {
        List<string> lines = analyzed.Lines;
        (int Step, int Total)[] stages = lines.Select(AnalyzerLines.Stage).Where(s => s is not null).Select(s => s!.Value).ToArray();
        Assert.Equal([0, 1, 2, 3, 4, 5], stages.Select(s => s.Step));
        Assert.All(stages, s => Assert.Equal(5, s.Total));

        int done = lines.FindIndex(AnalyzerLines.IsDone);
        int figures = lines.FindIndex(line => AnalyzerLines.IsDfr(line, "figures"));
        Assert.True(done > lines.FindLastIndex(line => AnalyzerLines.Stage(line) is not null), "완료 줄은 마지막 진행 줄 뒤에 옵니다");
        Assert.Equal(lines.Count - 1, figures);
        Assert.True(figures > done, "그림 줄은 완료 줄 뒤에 옵니다");

        JsonElement doneJson = AnalyzerLines.Json(lines[done], "done");
        Assert.True(doneJson.GetProperty("ok").GetBoolean());
        Assert.Equal("", doneJson.GetProperty("error").GetString());
        Assert.All(doneJson.EnumerateObject(), member => Assert.DoesNotContain(member.Value.ValueKind, new[] { JsonValueKind.Object, JsonValueKind.Array }));
        Assert.True(AnalyzerLines.Json(lines[figures], "figures").GetProperty("ok").GetBoolean());
    }

    [Fact]
    public void SummaryHasTheValuesTheResultScreenReads()
    {
        Assert.Equal(JsonValueKind.Number, summary.GetProperty("pi_left").ValueKind);
        Assert.Equal(JsonValueKind.Number, summary.GetProperty("pi_right").ValueKind);
        Assert.Equal(30.0, summary.GetProperty("fps").GetDouble());
        Assert.Equal(900, Frames);
        Assert.Equal(10, summary.GetProperty("window_s").GetDouble());
        Assert.Equal(2, summary.GetProperty("window_stride_s").GetDouble());
        Assert.Equal(11, Windows);
        Assert.Equal(JsonValueKind.Null, summary.GetProperty("classification").ValueKind);

        JsonElement cuff = summary.GetProperty("cuff_level");
        Assert.Contains(cuff.GetProperty("level").GetString(), new[] { "0", "V", "D", "O" });
        Assert.Contains(cuff.GetProperty("level_if_quality_ignored").GetString(), new[] { "0", "V", "D", "O" });
        Assert.Equal(1.0, new[] { "0", "V", "D", "O" }.Sum(s => cuff.GetProperty("state_probabilities").GetProperty(s).GetDouble()), 9);
        Assert.NotEqual("", cuff.GetProperty("rule").GetString());
        Assert.True(cuff.GetProperty("applicable").GetBoolean());

        JsonElement qc = summary.GetProperty("qc");
        Assert.Contains(qc.GetProperty("status").GetString(), new[] { "ok", "check", "retake" });
        Assert.All(qc.GetProperty("items").EnumerateArray(), item =>
        {
            Assert.Equal(JsonValueKind.String, item.GetProperty("code").ValueKind);
            Assert.Contains(item.GetProperty("severity").GetString(), new[] { "ok", "warn", "fail", "na" });
        });

        JsonElement dfr = summary.GetProperty("dfr");
        Assert.Equal("fake", dfr.GetProperty("variant").GetString());
        Assert.Equal("fake", dfr.GetProperty("upstream_commit").GetString());
        Assert.True(dfr.GetProperty("timing").TryGetProperty("figures_s", out _), "그림을 다 그린 뒤 요약을 바꿔 씁니다");
    }

    [Fact]
    public void PerfusionNpzHasArraysTheAppReads()
    {
        Dictionary<string, NpyEntry> npz = NpzFile.Read(analyzed.Shot.ResultFile("_perfusion.npz"));

        double[] time = npz["time_s"].Values();
        Assert.Equal([Windows], npz["time_s"].Shape);
        double window = summary.GetProperty("window_s").GetDouble(), stride = summary.GetProperty("window_stride_s").GetDouble();
        for (int k = 0; k < Windows; k++) Assert.Equal(k * stride + window / 2, time[k], 9);

        Assert.Equal([Frames], npz["template_left"].Shape);
        Assert.Equal([Frames], npz["template_right"].Shape);
        Assert.Equal([], npz["fps"].Shape);
        Assert.Equal(summary.GetProperty("fps").GetDouble(), npz["fps"].Values()[0]);

        string[] series = npz.Keys.Where(k => (k.EndsWith("_left") || k.EndsWith("_right")) && !k.StartsWith("template_")).ToArray();
        Assert.Contains("pi_left", series);
        Assert.Contains("pi_heel_right", series);
        Assert.All(series, k => Assert.Equal([Windows], npz[k].Shape));
        Assert.Equal([Windows], npz["template_corr"].Shape);
        Assert.Equal([Windows], npz["n_light_ref"].Shape);
        Assert.StartsWith("<i", npz["n_light_ref"].Descr);

        Assert.All(npz.Where(p => !p.Value.IsNumeric), p => Assert.StartsWith("<U", p.Value.Descr));
    }

    [Fact]
    public void PerfusionMapIsNaNExactlyOutsideValidCells()
    {
        Dictionary<string, NpyEntry> npz = NpzFile.Read(analyzed.Shot.ResultFile("_perfusion.npz"));
        long[] shape = npz["maps"].Shape;
        Assert.Equal(4, shape.Length);
        Assert.Equal(2, shape[0]);
        Assert.Equal(Windows, shape[1]);
        int rows = (int)shape[2], cols = (int)shape[3];
        Assert.Equal([rows, cols], npz["map_valid"].Shape);
        Assert.StartsWith("<i", npz["map_valid"].Descr);

        double[] valid = npz["map_valid"].Values();
        Assert.All(valid, v => Assert.True(v is 0 or 1));
        Assert.Contains(0.0, valid);
        double[] maps = npz["maps"].Values();
        for (int i = 0; i < maps.Length; i++)
        {
            bool cell = valid[i % (rows * cols)] == 1;
            Assert.True(cell == double.IsFinite(maps[i]), $"칸 {i}: map_valid {cell}, 값 {maps[i]}");
        }
    }

    [Fact]
    public void OutlineRunsFromToesToHeelWithLeftBeforeRight()
    {
        Dictionary<string, NpyEntry> npz = NpzFile.Read(analyzed.Shot.ResultFile("_perfusion.npz"));
        long[] shape = npz["outline"].Shape;
        Assert.Equal(2, shape.Length);
        Assert.Equal(3, shape[0]);
        int n = (int)shape[1];
        Assert.True(n >= 2);
        double[] outline = npz["outline"].Values();
        Assert.Equal(0.0, outline[0]);
        Assert.Equal(1.0, outline[n - 1]);
        for (int i = 1; i < n; i++) Assert.True(outline[i] > outline[i - 1], "outline_u가 늘어납니다");
        for (int i = 0; i < n; i++) Assert.True(outline[n + i] < outline[2 * n + i], "왼쪽 끝이 오른쪽 끝보다 작습니다");
    }

    [Fact]
    public void SummaryAveragesMatchWindowSeries()
    {
        Dictionary<string, NpyEntry> npz = NpzFile.Read(analyzed.Shot.ResultFile("_perfusion.npz"));
        Assert.Equal(summary.GetProperty("pi_left").GetDouble(), npz["pi_left"].Values().Average(), 12);
        Assert.Equal(summary.GetProperty("pi_right").GetDouble(), npz["pi_right"].Values().Average(), 12);
        foreach (string side in new[] { "left", "right" })
        {
            foreach (string region in new[] { "toes", "fore", "mid", "heel" })
            {
                Assert.Equal(summary.GetProperty("per_side").GetProperty(side).GetProperty($"pi_{region}").GetDouble(),
                             npz[$"pi_{region}_{side}"].Values().Average(), 12);
            }
        }
        Assert.Equal(summary.GetProperty("template_corr_lr").GetDouble(), npz["template_corr"].Values().Average(), 12);
    }

    [Fact]
    public void WaveformsNpzHasPulseAcAndThirtySecondPi()
    {
        Dictionary<string, NpyEntry> npz = NpzFile.Read(analyzed.Shot.ResultFile("_waveforms.npz"));
        foreach (string key in new[] { "pulse_left", "pulse_right", "ac_left", "ac_right" })
        {
            Assert.Equal([Frames], npz[key].Shape);
            Assert.Equal("<f8", npz[key].Descr);
        }
        Assert.Equal([], npz["pi_left"].Shape);
        Assert.Equal([], npz["pi_right"].Shape);
        Assert.InRange(npz["pi_left"].Values()[0], 0.0, 1.0);
    }

    [Fact]
    public void GifHasOneLoopingFramePerWindow()
    {
        GifFile gif = GifFile.Read(analyzed.Shot.ResultFile("_perfusion.gif"));
        Assert.Equal(Windows, gif.Frames.Count);
        Assert.True(gif.Loops);
        Assert.All(gif.Frames, frame => Assert.Equal(gif.Width * gif.Height, frame.Length));
        for (int k = 1; k < gif.Frames.Count; k++) Assert.False(gif.Frames[k].SequenceEqual(gif.Frames[k - 1]), $"{k - 1}번과 {k}번 장이 같습니다");
    }

    [Theory]
    [InlineData("_segmentation.png")]
    [InlineData("_template.png")]
    [InlineData("_pi_map.png")]
    public void SummaryFiguresAreValidPng(string suffix)
    {
        (int width, int height) = PngFile.Read(analyzed.Shot.ResultFile(suffix));
        Assert.True(width > 0 && height > 0);
    }
}
