using DiabeticFoot.Core.Record;
using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

// 원본 test_perfusion_summary.cc의 PerfusionSummary 시험입니다(결과 창이 읽는 요약 JSON과 분석결과 폴더 모으기).
public class PerfusionSummaryTests
{
    private static string Data(string name) => File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Data", name));

    private static string Replace(string text, string from, string to)
    {
        Assert.Contains(from, text);
        return text.Replace(from, to);
    }

    private static void Near(double expected, double actual, double tolerance) =>
        Assert.True(Math.Abs(expected - actual) <= tolerance, $"expected {expected}, actual {actual}");

    // 분석결과 앞부분을 만드는 데 쓰는 폴더만 채운 배치입니다.
    private static ShotLayout FolderLayout(string folder) => new() { Folder = folder };

    private static PerfusionSummary Read(string json)
    {
        var summary = PerfusionResults.ReadPerfusionSummary(json, out var error);
        Assert.True(summary is not null, error);
        return summary!;
    }

    // 실제 촬영 102528(2026-10-20, 열화상 포함)의 요약: 멀티모달 판정 PAD-like 0.60, 왼발 PI가 더 낮습니다.
    [Fact]
    public void ReadsRealMultimodalSample()
    {
        var s = Read(Data("102528_summary.json"));
        Near(0.08121029419302435, s.PiLeft, 1e-12);
        Near(0.09915272544778957, s.PiRight, 1e-12);
        Near(0.8190424804387945, s.PiRatio, 1e-12);
        Assert.Equal(FootSide.Left, s.LowerFoot);
        Assert.True(s.Classified);
        Near(0.6006581783294678, s.PPad, 1e-12);
        Near(0.4475051118604218, s.Threshold, 1e-12);
        Assert.Equal("PAD-like", s.Decision);
        Assert.Equal("multimodal (RGB + thermal)", s.Model);
        Assert.Equal(FootSide.Left, s.AffectedFoot);
        Assert.Equal(5, s.WindowProbs.Count);
        Assert.True(s.ThermalInVerdict);
        Assert.Equal("", s.ThermalSkipped);
        Assert.Equal(11, s.WindowCount);
        Assert.Equal(10_000_000, s.WindowUs);
        Assert.Equal(2_000_000, s.WindowStrideUs);
        Assert.Equal(30.0, s.Fps);
        Assert.Equal(900, s.Frames);
        Assert.Equal("5e5178d3836cdb1f19f0bbf701434ea923bbc5a6", s.UpstreamCommit);
        Assert.Equal(PadVerdict.PadLike, PerfusionResults.PadVerdictOf(s));
        // 부위별 관류지수, 발 인식 품질, 판정 구간 시각입니다.
        Near(0.08801492118000426, s.Left.PiToes, 1e-12);
        Near(0.07309784076001892, s.Right.PiHeel, 1e-12);
        Assert.Equal("segmentation", s.Left.MaskMethod);
        Near(0.8087337613105774, s.Left.DetectionScore, 1e-12);
        Near(0.9942265748977661, s.Right.MaskIou, 1e-12);
        Assert.Equal(152, s.Left.Blocks);
        Near(0.935, s.Left.ThermalMaskDice, 1e-12);
        Near(0.921, s.Right.ThermalMaskDice, 1e-12);
        Near(0.7033094760293708, s.TemplateCorr, 1e-12);
        Assert.Equal(6, s.LightRefMin);
        Assert.Equal(new[] { 5.0, 10.0, 15.0, 20.0, 25.0 }, s.WindowCentresS);
    }

    // 같은 촬영을 RGB 판정 모델로 본 요약: normal 0.06, 판정 측 발 null, 관류 분석 프로그램 기록 필드 dfr(worker_version, upstream_commit, thermal_skipped) 없음.
    [Fact]
    public void ReadsRgbOnlySample()
    {
        var s = Read(Data("102528_rgb_only_summary.json"));
        Assert.True(s.Classified);
        Assert.Equal("normal", s.Decision);
        Assert.Equal("RGB logistic regression", s.Model);
        Assert.Null(s.AffectedFoot);
        Assert.False(s.ThermalInVerdict);
        Assert.Equal("", s.ThermalSkipped);
        Assert.Equal("", s.UpstreamCommit);
        Assert.Equal(PadVerdict.Normal, PerfusionResults.PadVerdictOf(s));
    }

    // 이진 판정을 끄고 커프 단계만 낸 요약(맥 실측 185205, 2026-10-26, 자동 노출이라 참고용): 판정 없음, 커프 단계 0%.
    [Fact]
    public void ReadsCuffLevelOnlySample()
    {
        var s = Read(Data("185205_cuff_level_summary.json"));
        Assert.False(s.Classified);  // "classification": null
        Assert.Equal(PadVerdict.None, PerfusionResults.PadVerdictOf(s));
        Near(0.027625533520584944, s.PiLeft, 1e-12);
        var c = s.CuffLevel!;
        Assert.NotNull(c);
        Assert.Equal(0, c.LevelPct);
        Near(0.8996127605457414, c.E, 1e-12);
        Near(0.631820766754919, c.PGe40, 1e-12);
        Near(0.25753951859565205, c.PGe80, 1e-12);
        Near(0.01025247519517023, c.P120, 1e-12);
        Assert.Equal(3, c.Cuts.Count);
        Near(1.0604571884017018, c.Cuts[0], 1e-12);
        Near(2.4231874752371527, c.Cuts[2], 1e-12);
        Assert.False(c.Applicable);
        Assert.Equal("auto", c.SettingMode);
        Assert.True(double.IsNaN(c.ExposureStep));  // null
        Assert.Equal(4, c.Gain);
        Assert.Empty(c.WindowE);  // 구간별 값을 내기 전의 요약입니다
        Assert.Equal("", s.ThermalSkipped);
    }

    // 고정 카메라 설정이면 applicable이 true이고, 이전 결과(판정만 있는 요약)에는 커프 단계가 없습니다.
    [Fact]
    public void ReadsApplicableCuffLevelAndOldSummaryWithout()
    {
        var fixedSetting = Read("""
            {"pi_left": 0.1, "pi_right": 0.2,
             "cuff_level": {"level_pct_sbp": 80, "E": 2.0, "applicable": true,
                            "setting": {"mode": "manual", "exposure_step": -5, "gain": 1},
                            "window_E": [1.5, "x", 2.5], "window_centres_s": [5.0, 10.0, 15.0]}}
            """);
        var level = fixedSetting.CuffLevel!;
        Assert.NotNull(level);
        Assert.Equal(80, level.LevelPct);
        Assert.True(level.Applicable);
        Assert.Equal("manual", level.SettingMode);
        Assert.Equal(-5, level.ExposureStep);
        Assert.Empty(level.Cuts);
        // 숫자가 아닌 칸은 NaN으로 두어 시각과의 짝을 지킵니다.
        Assert.Equal(3, level.WindowE.Count);
        Assert.Equal(1.5, level.WindowE[0]);
        Assert.True(double.IsNaN(level.WindowE[1]));
        Assert.Equal(new[] { 5.0, 10.0, 15.0 }, level.WindowCentresS);
        var old = Read(Data("102528_summary.json"));
        Assert.Null(old.CuffLevel);
        Assert.True(old.Classified);
        Assert.Null(Read("""{"pi_left": 0.1, "pi_right": 0.2, "cuff_level": null}""").CuffLevel);
    }

    // 경계별 규칙 판정(v7)의 요약(맥 실측 185205, 2026-10-28): 강한 발 맥박 SNR 12.4 dB가 기준 14 dB보다 낮아
    // level_pct_sbp가 null입니다. 항목은 있으므로 특징 값, 기준값, 품질 이유를 읽습니다.
    [Fact]
    public void ReadsRuleCuffLevelWithoutLevel()
    {
        var s = Read(Data("185205_rule_summary.json"));
        Assert.Equal("v7", s.Variant);
        Assert.Equal("1f47dea7a3a34ad87cb04fa270f6b5d432f0c6fa", s.UpstreamCommit);
        var c = s.CuffLevel!;
        Assert.NotNull(c);
        Assert.False(c.HasLevel);
        Assert.False(c.QualityOk);
        Assert.Equal(0, c.LevelIfQualityIgnored);
        Assert.Equal("none of the cuff rules", c.Rule);
        Near(12.3912, c.SnrDb, 1e-9);
        Near(0.7897, c.H1re30, 1e-9);
        Near(-140.3551, c.Delay2_30Ms, 1e-9);
        Near(0.7219, c.PiRatio, 1e-9);
        Assert.Equal("right", c.LowerSide);
        Near(0.2639, c.H1re30Below, 1e-12);
        Near(28.8, c.Delay2_30AboveMs, 1e-12);
        Near(0.64, c.PiRatioBelow, 1e-12);
        Near(14.0, c.QualitySnrDb, 1e-12);
        Assert.False(c.Applicable);
        Assert.Equal(4, c.Gain);
        Assert.True(double.IsNaN(c.E));  // 로지스틱 회귀의 값은 없습니다
        Assert.Empty(c.Cuts);
    }

    // v8(연구 92c1416)의 요약(노트북 측정 커프 40%, 맥에서 v8로 분석, 2026-10-28): CNN 발 분할, 발 셀, 규칙 판정.
    // 발 셀 수와 분할 방법을 읽고, 커프 단계는 v7과 같은 키로 읽습니다.
    [Fact]
    public void ReadsV8FootCellSummary()
    {
        var s = Read(Data("160307_v8_summary.json"));
        Assert.Equal("v8", s.Variant);
        Assert.Equal("92c1416d962b20423f9d3b0ac140c880c7b6e927", s.UpstreamCommit);
        Near(0.03493811350089473, s.PiLeft, 1e-12);
        Near(0.10729357930758017, s.PiRight, 1e-12);
        Assert.Equal("footseg_dl", s.Left.MaskMethod);
        Near(0.9846497774124146, s.Left.DetectionScore, 1e-12);
        Assert.Equal(100, s.Left.Cells);
        Assert.Equal(95, s.CellsUsed);
        Assert.Equal(95, s.CellsCommon);
        Near(0.025689333154924866, s.Left.PiHeel, 1e-12);
        var c = s.CuffLevel!;
        Assert.NotNull(c);
        Assert.True(c.HasLevel);
        Assert.Equal(40, c.LevelPct);
        Assert.Equal("pi_ratio 0.36 < 0.69", c.Rule);
        Assert.True(c.QualityOk);
        Near(0.3591, c.PiRatio, 1e-12);
        Near(0.689, c.PiRatioBelow, 1e-12);
        Near(11.5, c.QualitySnrDb, 1e-12);
        Assert.False(c.Applicable);  // 92c1416은 노출 -5만 고정 설정으로 인정했습니다
        Assert.False(s.Classified);
        Assert.Null(s.Quality);  // 92c1416에는 qc가 없습니다
    }

    // 연구 3a9d830으로 같은 촬영을 다시 분석한 요약(맥, 2026-10-29): 측정 품질 안내(qc)와 노출 -9 인정.
    [Fact]
    public void ReadsMeasurementQuality()
    {
        var s = Read(Data("160307_v8_qc_summary.json"));
        Assert.True(s.CuffLevel!.Applicable);
        var q = s.Quality!;
        Assert.NotNull(q);
        Assert.Equal("check", q.Status);
        Assert.Equal("측정 완료 (확인 필요)", q.StatusKo);
        Assert.Equal("양말을 신고 5~10분 기다린 뒤 측정하세요.", q.ActionKo);
        Assert.Equal(8, q.Items.Count);
        Assert.Equal("camera_setting", q.Items[0].Code);
        Assert.Equal("manual, 노출 -9, 게인 1", q.Items[0].Value);
        Assert.Equal("common_cells", q.Items[2].Code);
        Assert.Equal("95", q.Items[2].Value);
        Assert.Equal("개", q.Items[2].Unit);
        var warmth = q.Items[6];
        Assert.Equal("foot_warmth", warmth.Code);
        Assert.Equal("warn", warmth.Severity);
        Assert.Equal("59 / 66", warmth.Value);
        Assert.Equal("subject", warmth.Reason);
        Assert.Equal("왼발이 차갑습니다(배경과 온도 차이가 작음). 혈류 신호가 약해질 수 있습니다.", warmth.MessageKo);
        Assert.Equal("na", q.Items[7].Severity);
        Assert.Equal("", q.Items[7].Value);  // null은 빈 문자열
        Assert.Equal("", q.Items[7].Reason);
        Near(59.3, q.ThermalContrastLeft, 1e-12);
        Near(66.2, q.ThermalContrastRight, 1e-12);
        Assert.Equal("subject", q.WorstReason());
    }

    // v9(연구 3056dd9, 번들 v5)의 요약: 연구 코드로 2026-10-30 17:35:35 촬영(수동, 노출 -9, 게인 1, 손가락 PPG, 커프 없음)을
    // 분석한 파일입니다. 상태는 문자이고 level_pct_sbp가 없으며, qc version 2에 맥박 타이밍 기준과 1 Hz 깜빡임 항목이 있습니다.
    [Fact]
    public void ReadsCuffStateSummary()
    {
        var s = Read(Data("173535_v9_summary.json"));
        Assert.Equal("v9", s.Variant);
        var c = s.CuffLevel!;
        Assert.NotNull(c);
        Assert.True(c.States);
        Assert.True(c.HasLevel);
        Assert.Equal("0", c.LevelCode);
        Assert.Equal("정상", c.LevelKo);
        Assert.Equal(0, c.LevelPct);
        Assert.False(c.Pad);
        Near(0.0001, c.PadProbability, 1e-12);
        Near(0.7972, c.PState0, 1e-12);
        Near(0.2027, c.PStateV, 1e-12);
        Near(0.0001, c.PStateD, 1e-12);
        Near(0.0, c.PStateO, 1e-12);
        Assert.Equal("0", c.LevelCodeIfQualityIgnored);
        Assert.Equal(-1, c.LevelIfQualityIgnored);
        Assert.True(c.Applicable);
        Assert.True(double.IsNaN(c.PiRatioBelow));  // 번들 v5에는 규칙 기준값이 없습니다
        Near(17.9772, c.SnrDb, 1e-12);
        var items = s.Quality!.Items;
        Assert.Equal(10, items.Count);
        Assert.Equal("pulse_timing", items[8].Code);
        Assert.Equal("v106w-foot right", items[8].Value);
        Assert.Equal("light_flicker", items[9].Code);
        Assert.Equal("warn", items[9].Severity);
        Assert.Equal("0.2845", items[9].Value);
        Assert.Equal("equipment", items[9].Reason);
    }

    // v9에서 강한 발의 맥박 SNR이 기준보다 낮으면 상태, 이름, pad는 null이고, PAD 확률과 품질을 뺀 상태 문자는 남습니다.
    // 같은 요약에서 cuff_dl.predict_v5와 plantar_perfusion.cuff_level이 품질 미달 때 쓰는 값으로 바꾼 파일입니다.
    [Fact]
    public void ReadsCuffStateWithoutQuality()
    {
        var s = Read(Data("173535_v9_weak_summary.json"));
        var c = s.CuffLevel!;
        Assert.NotNull(c);
        Assert.True(c.States);
        Assert.False(c.HasLevel);
        Assert.Empty(c.LevelCode);
        Assert.Empty(c.LevelKo);
        Assert.Null(c.Pad);
        Assert.False(c.QualityOk);
        Assert.False(c.Applicable);
        Assert.Equal("0", c.LevelCodeIfQualityIgnored);
        Near(0.0001, c.PadProbability, 1e-12);
    }

    // v8까지의 요약은 커프 상태가 아닙니다.
    [Fact]
    public void PercentSummaryIsNotCuffStates()
    {
        var s = Read(Data("160307_v8_qc_summary.json"));
        var c = s.CuffLevel!;
        Assert.NotNull(c);
        Assert.False(c.States);
        Assert.Empty(c.LevelCode);
        Assert.Null(c.Pad);
        Assert.Equal(40, c.LevelIfQualityIgnored);
    }

    // qc가 null이거나 없으면 null이고, 가장 심한 항목은 fail이 warn보다 앞섭니다.
    [Fact]
    public void MeasurementQualityWorstReason()
    {
        var none = Read("""{"pi_left": 0.1, "pi_right": 0.1, "qc": null}""");
        Assert.Null(none.Quality);
        var q = new MeasurementQuality
        {
            Items =
            [
                new QualityItem { Code = "brightness", Severity = "warn", Reason = "equipment" },
                new QualityItem { Code = "pulse", Severity = "fail", Reason = "subject" },
                new QualityItem { Code = "foot_found", Severity = "fail", Reason = "posture" },
            ],
        };
        Assert.Equal("subject", q.WorstReason());
        q = q with { Items = [new QualityItem { Code = "pulse", Severity = "ok" }] };
        Assert.Equal("", q.WorstReason());
    }

    // v7까지의 요약에는 발 셀이 없어 셀 수가 NaN입니다. 결과 창은 이 값으로 v8 결과인지 가립니다.
    [Fact]
    public void OlderSummaryHasNoFootCells()
    {
        var s = Read(Data("185205_rule_summary.json"));
        Assert.True(double.IsNaN(s.CellsUsed));
        Assert.True(double.IsNaN(s.Left.Cells));
    }

    // 규칙 판정이 단계를 내면 HasLevel이 true이고, 로지스틱 회귀 요약은 rule이 비고 품질은 통과로 봅니다.
    [Fact]
    public void ReadsRuleCuffLevelWithLevel()
    {
        var rule = Read("""
            {"pi_left": 0.1, "pi_right": 0.2,
             "cuff_level": {"level_pct_sbp": 120, "level_if_quality_ignored": 120, "rule": "h1re_30 < 0.2639",
                            "quality_ok": true, "applicable": true, "features": {"h1re_30": 0.1, "hi_snr1_30": 20.5}}}
            """).CuffLevel!;
        Assert.NotNull(rule);
        Assert.True(rule.HasLevel);
        Assert.Equal(120, rule.LevelPct);
        Assert.Equal("h1re_30 < 0.2639", rule.Rule);
        Assert.True(rule.QualityOk);
        Assert.Equal(20.5, rule.SnrDb);
        Assert.True(double.IsNaN(rule.PiRatio));
        var logistic = Read(Data("185205_cuff_level_summary.json"));
        Assert.True(logistic.CuffLevel!.HasLevel);
        Assert.Empty(logistic.CuffLevel.Rule);
        Assert.True(logistic.CuffLevel.QualityOk);
        Assert.Equal(-1, logistic.CuffLevel.LevelIfQualityIgnored);
        Assert.Equal("", logistic.Variant);
    }

    // 관류 분석 프로그램이 열화상을 뺀 이유 코드를 읽고 문장으로 바꿉니다.
    [Fact]
    public void ReadsThermalSkipReason()
    {
        var json = Replace(Data("102528_summary.json"), "\"thermal_skipped\": null", "\"thermal_skipped\": \"thermal_too_short\"");
        var s = Read(json);
        Assert.Equal("thermal_too_short", s.ThermalSkipped);
        Assert.Contains("7.5Hz", PerfusionResults.DescribeThermalSkip(s.ThermalSkipped));
        Assert.Contains("new_code", PerfusionResults.DescribeThermalSkip("new_code"));
        Assert.Equal("", PerfusionResults.DescribeThermalSkip(""));
    }

    // 녹화 중 미리 분할, 미리 읽기를 쓰지 못한 이유 코드를 읽습니다. 썼거나(null) 꺼져 있으면(키 없음) 빈 문자열입니다.
    [Fact]
    public void ReadsSpeedupSkipReasons()
    {
        var skipped = Read("""
            {"pi_left": 0.1, "pi_right": 0.2, "dfr": {
              "presegment": {"enabled": true, "threads": 2, "used": [], "missed": [{"reason": "model_not_loaded"}],
                             "reason": "model_not_loaded"},
              "prestream": {"used": false, "reason": "no_presegment", "catchup_s": 0.0}}}
            """);
        Assert.Equal("model_not_loaded", skipped.PresegmentSkipped);
        Assert.Equal("no_presegment", skipped.PrestreamSkipped);
        Assert.Contains("분할 모델", PerfusionResults.DescribeSpeedupSkip(skipped.PresegmentSkipped));
        var used = Read("""
            {"pi_left": 0.1, "pi_right": 0.2, "dfr": {
              "presegment": {"enabled": true, "reason": null}, "prestream": {"used": true, "reason": null}}}
            """);
        Assert.Equal("", used.PresegmentSkipped);
        Assert.Equal("", used.PrestreamSkipped);
        var off = Read("""
            {"pi_left": 0.1, "pi_right": 0.2,
             "dfr": {"presegment": {"enabled": false}, "prestream": {"enabled": false}}}
            """);
        Assert.Equal("", off.PrestreamSkipped);
        Assert.Contains("new_code", PerfusionResults.DescribeSpeedupSkip("new_code"));
        Assert.Equal("", PerfusionResults.DescribeSpeedupSkip(""));
    }

    // pi_left나 pi_right 키가 없거나 JSON이 깨지면 읽지 않습니다.
    [Fact]
    public void MissingPiKeyFails()
    {
        Assert.Null(PerfusionResults.ReadPerfusionSummary(Replace(Data("102528_summary.json"), "\"pi_left\":", "\"pi_l\":"), out var error));
        Assert.Contains("pi_left", error);
        Assert.Null(PerfusionResults.ReadPerfusionSummary("""{"pi_right": 0.1}"""));
        Assert.Null(PerfusionResults.ReadPerfusionSummary("""{"pi_left": 0.1, "pi_right": 0.2"""));
        Assert.Null(PerfusionResults.ReadPerfusionSummary(""));
    }

    // 판정 항목이 없으면 판정 없음, 나머지 키가 없으면 기본값입니다.
    [Fact]
    public void MissingOptionalKeysUseDefaults()
    {
        var s = Read("""{"pi_left": 0.1, "pi_right": 0.1}""");
        Assert.False(s.Classified);
        Assert.Equal(PadVerdict.None, PerfusionResults.PadVerdictOf(s));
        Assert.Null(s.LowerFoot);  // 같은 값
        Assert.Equal(1.0, s.PiRatio, 12);  // 키가 없어 왼발 / 오른발로 구함
        Assert.Equal(0, s.WindowCount);
        Assert.Equal(0, s.WindowUs);
        Assert.True(double.IsNaN(s.Fps));
        // p_pad만 있고 threshold가 없으면 판정으로 보지 않습니다.
        var half = Read("""{"pi_left": 0.1, "pi_right": 0.2, "classification": {"p_pad": 0.7}}""");
        Assert.False(half.Classified);
    }

    // 파이썬이 쓴 NaN은 값이 없는 것으로 읽고, 더 낮은 발과 비를 지어내지 않습니다.
    [Fact]
    public void NanValuesStayUnknown()
    {
        var s = Read("""
            {"pi_left": NaN, "pi_right": 0.09, "pi_ratio_left_right": NaN,
             "classification": {"p_pad": NaN, "threshold": 0.45, "decision": "normal", "window_probs": [0.1, NaN, 0.2]}}
            """);
        Assert.True(double.IsNaN(s.PiLeft));
        Near(0.09, s.PiRight, 1e-12);
        Assert.True(double.IsNaN(s.PiRatio));
        Assert.Null(s.LowerFoot);
        Assert.True(s.Classified);
        Assert.True(double.IsNaN(s.PPad));
        Assert.Equal(3, s.WindowProbs.Count);
        Assert.True(double.IsNaN(s.WindowProbs[1]));
        // 비 키가 없고 PI 하나가 NaN이면 비를 구하지 않습니다.
        var noRatio = Read("""{"pi_left": NaN, "pi_right": 0.09}""");
        Assert.True(double.IsNaN(noRatio.PiRatio));
    }

    // 관류 분석 프로그램이 모르는 판정 낱말을 적으면 어느 쪽으로도 짐작하지 않습니다.
    [Fact]
    public void UnknownDecisionIsNone()
    {
        var s = Read("""{"pi_left": 0.1, "pi_right": 0.2, "classification": {"p_pad": 0.9, "threshold": 0.4, "decision": "PAD"}}""");
        Assert.Equal(PadVerdict.None, PerfusionResults.PadVerdictOf(s));
    }

    // 분석결과 폴더에는 요약, 보고서, 그림, GIF만 들어갑니다(npz, 다른 촬영 이름, 관류 분석 프로그램 기록 제외).
    [Fact]
    public void SelectsCollectedFiles()
    {
        Assert.True(PerfusionResults.IsCollectedResultFile("102528_summary.json", "102528"));
        Assert.True(PerfusionResults.IsCollectedResultFile("102528_report.txt", "102528"));
        Assert.True(PerfusionResults.IsCollectedResultFile("102528_frame_timing.csv", "102528"));
        Assert.True(PerfusionResults.IsCollectedResultFile("102528_frame_windows.csv", "102528"));
        Assert.False(PerfusionResults.IsCollectedResultFile("102528_other.csv", "102528"));
        Assert.True(PerfusionResults.IsCollectedResultFile("102528_perfusion.gif", "102528"));
        Assert.True(PerfusionResults.IsCollectedResultFile("102528_temporal_mean.png", "102528"));
        Assert.False(PerfusionResults.IsCollectedResultFile("102528_perfusion.npz", "102528"));
        Assert.False(PerfusionResults.IsCollectedResultFile("analyzer_log.txt", "102528"));
        Assert.False(PerfusionResults.IsCollectedResultFile("102529_summary.json", "102528"));
        Assert.False(PerfusionResults.IsCollectedResultFile("102528_.png", ""));
        Assert.Equal("a/perfusion/102528_perfusion.gif", PerfusionResults.PerfusionGifPath("a/perfusion", "102528"));
    }

    private static string FreshBase(string name)
    {
        var path = Path.Combine(Path.GetTempPath(), name);
        if (Directory.Exists(path)) Directory.Delete(path, recursive: true);
        return path;
    }

    // 결과 파일을 <저장 위치>/results/<촬영 폴더 경로 단계>_*로 복사합니다. npz는 복사하지 않습니다.
    [Fact]
    public void CollectsResultsUnderShotPrefix()
    {
        var baseDir = FreshBase("diabeticfoot_test_collect");
        var output = Path.Combine(baseDir, "shot", "perfusion");
        Directory.CreateDirectory(output);
        foreach (var name in new[] { "run_summary.json", "run_perfusion.gif", "run_waveforms.png", "run_perfusion.npz" })
        {
            File.WriteAllText(Path.Combine(output, name), name);
        }
        var layout = FolderLayout("test_19551103/0/20261020_102528");
        const string prefix = "test_19551103_0_20261020_102528";
        var copied = PerfusionResults.CollectPerfusionResults(output, "run", Path.Combine(baseDir, "root"), layout);
        Assert.NotNull(copied);
        Assert.Equal(3, copied!.Files.Count);
        Assert.Equal(3, copied.Copied);
        Assert.Equal(1, copied.Reanalysis);
        var collect = Path.Combine(baseDir, "root", "results");
        Assert.Equal("run_summary.json", File.ReadAllText(Path.Combine(collect, prefix + "_summary.json")));
        Assert.True(File.Exists(Path.Combine(collect, prefix + "_perfusion.gif")));
        Assert.True(File.Exists(Path.Combine(collect, prefix + "_waveforms.png")));
        Assert.False(File.Exists(Path.Combine(collect, prefix + "_perfusion.npz")));
        Assert.Null(PerfusionResults.CollectPerfusionResults(Path.Combine(baseDir, "none"), "run", Path.Combine(baseDir, "root"), FolderLayout("x"), "", out var error));
        Assert.NotEmpty(error);
        Assert.Null(PerfusionResults.CollectPerfusionResults(output, "run", Path.Combine(baseDir, "root"), new ShotLayout()));
    }

    // 판 이름(tag)을 주면 앞부분 뒤에 붙여, 판마다 이름이 같은 결과 파일이 분석결과 폴더에서 서로 덮지 않습니다.
    [Fact]
    public void CollectsVariantsUnderTaggedPrefix()
    {
        var baseDir = FreshBase("diabeticfoot_test_collect_tag");
        var root = Path.Combine(baseDir, "root");
        var layout = FolderLayout("p");
        foreach (var variant in new[] { "v6", "v7" })
        {
            var output = Path.Combine(baseDir, "perfusion", variant);
            Directory.CreateDirectory(output);
            File.WriteAllText(Path.Combine(output, "run_summary.json"), variant);
            var collected = PerfusionResults.CollectPerfusionResults(output, "run", root, layout, variant, out var error);
            Assert.True(collected is not null, error);
            Assert.Equal(1, collected!.Copied);
            Assert.Equal(1, collected.Reanalysis);
        }
        var collect = Path.Combine(baseDir, "root", "results");
        Assert.Equal("v6", File.ReadAllText(Path.Combine(collect, "p_v6_summary.json")));
        Assert.Equal("v7", File.ReadAllText(Path.Combine(collect, "p_v7_summary.json")));
        Assert.False(File.Exists(Path.Combine(collect, "p_summary.json")));
    }

    // 같은 내용으로 다시 모으면 통과하고, 내용이 다른 결과(다시 분석)는 덮어쓰지 않고 _reanalysis2, _reanalysis3으로 새로 모읍니다.
    [Fact]
    public void CollectNeverOverwritesAndNumbersReanalysis()
    {
        var baseDir = FreshBase("diabeticfoot_test_collect_again");
        var output = Path.Combine(baseDir, "perfusion");
        Directory.CreateDirectory(output);
        File.WriteAllText(Path.Combine(output, "run_summary.json"), "first");
        File.WriteAllText(Path.Combine(output, "run_waveforms.png"), "png");
        var root = Path.Combine(baseDir, "root");
        var layout = FolderLayout("p");
        Assert.NotNull(PerfusionResults.CollectPerfusionResults(output, "run", root, layout));
        var again = PerfusionResults.CollectPerfusionResults(output, "run", root, layout);
        Assert.NotNull(again);
        Assert.Equal(2, again!.Files.Count);
        Assert.Equal(0, again.Copied);
        Assert.Equal(1, again.Reanalysis);

        // 다시 분석해 요약이 바뀌면 처음 모은 파일은 그대로 두고, 이번 결과 전체를 2번으로 모읍니다.
        File.WriteAllText(Path.Combine(output, "run_summary.json"), "changed");
        File.WriteAllText(Path.Combine(output, "run_perfusion.gif"), "gif");
        var second = PerfusionResults.CollectPerfusionResults(output, "run", root, layout);
        Assert.NotNull(second);
        Assert.Equal(3, second!.Files.Count);
        Assert.Equal(3, second.Copied);
        Assert.Equal(2, second.Reanalysis);
        var collect = Path.Combine(baseDir, "root", "results");
        Assert.Equal(collect, second.Folder);
        Assert.Equal("first", File.ReadAllText(Path.Combine(collect, "p_summary.json")));
        Assert.False(File.Exists(Path.Combine(collect, "p_perfusion.gif")));
        Assert.Equal("changed", File.ReadAllText(Path.Combine(collect, "p_reanalysis2_summary.json")));
        Assert.True(File.Exists(Path.Combine(collect, "p_reanalysis2_perfusion.gif")));
        Assert.True(File.Exists(Path.Combine(collect, "p_reanalysis2_waveforms.png")));
        Assert.Equal("p_reanalysis2", RecordFunctions.CollectedResultPrefix(layout, 2));

        // 2번과 같은 결과를 다시 모으면 새 번호를 만들지 않고, 또 바뀌면 3번으로 모읍니다.
        Assert.NotNull(PerfusionResults.CollectPerfusionResults(output, "run", root, layout));
        Assert.False(File.Exists(Path.Combine(collect, "p_reanalysis3_summary.json")));
        File.WriteAllText(Path.Combine(output, "run_summary.json"), "third");
        var third = PerfusionResults.CollectPerfusionResults(output, "run", root, layout);
        Assert.NotNull(third);
        Assert.Equal(3, third!.Reanalysis);
        Assert.Equal("third", File.ReadAllText(Path.Combine(collect, "p_reanalysis3_summary.json")));
    }
}
