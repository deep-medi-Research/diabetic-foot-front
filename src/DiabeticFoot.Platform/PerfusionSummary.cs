using System.Globalization;
using DiabeticFoot.Core.Record;

namespace DiabeticFoot.Platform;

/// <summary>
/// 관류 분석 프로그램이 추정한 커프 단계입니다(요약의 `cuff_level` 항목).
/// </summary>
/// <remarks>
/// 연구 코드 판 v9(번들 v5)부터는 커프 상태 0(정상), V(정상 경계, 정맥만 막힘), D(경증, 동맥 부분 압박), O(폐색) 중
/// 하나입니다(<see cref="States"/>가 true). 네트워크 하나가 네 상태 확률(p_state_*)과 PAD 확률(pad_probability)을 내고 가장 큰 상태를 고릅니다.
/// 그 전 판은 커프 압력을 수축기 혈압 대비 퍼센트로 0, 40, 80, 120 네 단계 중 하나로 추정합니다(v6 누적 로지스틱 회귀의 E와
/// cuts, v7, v8 경계별 규칙).
/// 모델은 고정 카메라 설정(수동, 게인 1, 노출 -5 또는 -9)으로 찍은 촬영에 맞췄습니다. 다른 설정이면 <see cref="Applicable"/>이
/// false이고 값은 참고용입니다.
/// 연구 쪽 번들 v3 요약(발목 혈압 단계 문자 0, V, D, M, S)도 states로 읽습니다. M과 S는 폐색입니다.
/// 요약에 없는 숫자는 <see cref="PerfusionResults.NoNumber"/>(NaN)입니다.
/// </remarks>
public sealed record CuffLevel
{
    /// <summary>
    /// 단계가 있으면 true입니다. v8까지는 `level_pct_sbp`가 숫자인지, v9는 `level`이 문자인지로 정합니다. 맥박 품질 부족으로
    /// 단계를 내지 않으면(`null`) false입니다.
    /// </summary>
    public bool HasLevel { get; init; } = true;

    /// <summary>`level_pct_sbp`: 추정 단계(수축기 혈압 대비 %). 0, 40, 80, 120 중 하나입니다. HasLevel이 false이거나 v9 요약이면 0입니다.</summary>
    public int LevelPct { get; init; }

    /// <summary>상태를 문자로 낸 요약(v9)이면 true입니다. `level_if_quality_ignored`가 문자이면 그렇습니다.</summary>
    public bool States { get; init; }

    /// <summary>`level`: 추정 상태 0, V, D, O(번들 v3은 0, V, D, M, S). 품질 미달(`null`)이거나 v8까지의 요약이면 비었습니다.</summary>
    public string LevelCode { get; init; } = "";

    /// <summary>`level_ko`: 연구 쪽 상태 이름(정상, 정상 (경계), 경증, 폐색). 품질 미달이거나 없으면 비었습니다.</summary>
    public string LevelKo { get; init; } = "";

    /// <summary>v9의 `level_if_quality_ignored`: 품질 검사를 무시했을 때의 상태 문자입니다. 없으면 비었습니다.</summary>
    public string LevelCodeIfQualityIgnored { get; init; } = "";

    /// <summary>`pad_probability`: 네트워크가 낸 PAD(D 또는 O) 확률(0~1). 품질 미달이어도 있습니다.</summary>
    public double PadProbability { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`pad`: 고른 상태가 D 또는 O이면 true입니다. 품질 미달(`null`)이거나 없으면 null입니다.</summary>
    public bool? Pad { get; init; }

    /// <summary>`state_probabilities`: 네 상태 0, V, D, O의 확률(0~1).</summary>
    public double PState0 { get; init; } = PerfusionResults.NoNumber;

    public double PStateV { get; init; } = PerfusionResults.NoNumber;

    public double PStateD { get; init; } = PerfusionResults.NoNumber;

    public double PStateO { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`E`: 세 확률의 합(0~3).</summary>
    public double E { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`p_ge40`: 단계가 40 이상일 확률(0~1)입니다.</summary>
    public double PGe40 { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`p_ge80`: 단계가 80 이상일 확률(0~1)입니다.</summary>
    public double PGe80 { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`p_120`: 단계가 120일 확률(0~1)입니다.</summary>
    public double P120 { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`cuts`: E를 나누는 기준값 세 개(작은 순).</summary>
    public IReadOnlyList<double> Cuts { get; init; } = Array.Empty<double>();

    /// <summary>`applicable`: 고정 카메라 설정으로 찍은 촬영이면 true입니다.</summary>
    public bool Applicable { get; init; }

    /// <summary>`setting.mode`: 촬영의 카메라 노출 방식. `manual` 또는 `auto`이고, 기록이 없으면 빈 문자열입니다.</summary>
    public string SettingMode { get; init; } = "";

    /// <summary>`setting.exposure_step`: 노출 단계. 자동이거나 기록이 없으면 NoNumber입니다.</summary>
    public double ExposureStep { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`setting.gain`: 게인. 기록이 없으면 NoNumber입니다.</summary>
    public double Gain { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`window_E`: 10초 구간별 E(0~3). E는 이 값들의 평균입니다. 2026-10-26 이전 요약에는 없습니다.</summary>
    public IReadOnlyList<double> WindowE { get; init; } = Array.Empty<double>();

    /// <summary>`window_centres_s`: 구간 가운데 시각(초). WindowE와 같은 순서입니다.</summary>
    public IReadOnlyList<double> WindowCentresS { get; init; } = Array.Empty<double>();

    // 아래는 경계별 규칙 판정(연구 저장소 9cc243d 이후, 2026-10-28)의 값입니다. 로지스틱 회귀 판정의 요약에는 없습니다.

    /// <summary>`rule`: 단계를 정한 규칙 문구. 예: `h1re_30 &lt; 0.2639`, `none of the cuff rules`. 비어 있으면 로지스틱 회귀 판정입니다.</summary>
    public string Rule { get; init; } = "";

    /// <summary>`quality_ok`: 강한 발의 맥박 SNR이 기준 이상이면 true입니다. 키가 없으면 true입니다.</summary>
    public bool QualityOk { get; init; } = true;

    /// <summary>v8까지의 `level_if_quality_ignored`: 품질 검사를 무시했을 때의 단계(%)입니다. 없거나 문자이면 -1입니다.</summary>
    public int LevelIfQualityIgnored { get; init; } = -1;

    /// <summary>`features.h1re_30`: 약한 발과 강한 발의 맥박 기본파 비의 실수부(30초). 120% 규칙에 씁니다.</summary>
    public double H1re30 { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`features.delay2_30`: 약한 발 맥박 2차 고조파의 위상 지연(ms, 30초). 80% 규칙에 씁니다.</summary>
    public double Delay2_30Ms { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`features.pi_ratio`: 약한 발 PI / 강한 발 PI. 40% 규칙에 씁니다.</summary>
    public double PiRatio { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`features.hi_snr1_30`: 강한 발의 맥박 SNR(dB, 30초). 품질 검사에 씁니다.</summary>
    public double SnrDb { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`features.lower_side`: PI가 낮은 발(`left`, `right`). 없으면 빈 문자열입니다.</summary>
    public string LowerSide { get; init; } = "";

    /// <summary>`thresholds.h1re_30`: h1re_30이 이보다 작으면 120%입니다. v9에는 기준값이 없어 NoNumber입니다.</summary>
    public double H1re30Below { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`thresholds.delay2_30`: delay2_30이 이보다 크면 80%입니다.</summary>
    public double Delay2_30AboveMs { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`thresholds.pi_ratio`: pi_ratio가 이보다 작으면 40%입니다.</summary>
    public double PiRatioBelow { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`quality_snr_db`: 품질 검사 기준(dB). SnrDb가 이보다 작으면 단계를 내지 않습니다.</summary>
    public double QualitySnrDb { get; init; } = PerfusionResults.NoNumber;
}

/// <summary>`qc.items[]`의 항목 하나입니다.</summary>
public sealed record QualityItem
{
    /// <summary>camera_setting, foot_found, common_cells, brightness, noise_floor, pulse, foot_warmth, heart_rate 등</summary>
    public string Code { get; init; } = "";

    /// <summary>항목 이름입니다.</summary>
    public string NameKo { get; init; } = "";

    /// <summary>ok, warn, fail, na(계산 안 함)</summary>
    public string Severity { get; init; } = "";

    /// <summary>값. 숫자는 요약에 적힌 그대로, 없으면 빈 문자열입니다.</summary>
    public string Value { get; init; } = "";

    /// <summary>단위. 없으면 빈 문자열입니다.</summary>
    public string Unit { get; init; } = "";

    /// <summary>기준 문장입니다.</summary>
    public string Threshold { get; init; } = "";

    /// <summary>경고 설명. ok이면 비었습니다.</summary>
    public string MessageKo { get; init; } = "";

    /// <summary>조치 문장. ok이면 비었습니다.</summary>
    public string ActionKo { get; init; } = "";

    /// <summary>IRB 측정 실패 사유: equipment, subject, posture, other. ok이면 비었습니다.</summary>
    public string Reason { get; init; } = "";
}

/// <summary>
/// 요약의 `qc`: 측정자에게 보이는 측정 품질 안내입니다. 관류 분석 프로그램 v8 `3a9d830`부터 있습니다.
/// 형식과 기준값은 연구 저장소 `docs/app_streaming_handoff.md` 10절과 `pad_clf/quality.py`가 정합니다.
/// </summary>
public sealed record MeasurementQuality
{
    /// <summary>`status`: `ok`(측정 완료), `check`(측정 완료, 확인 필요), `retake`(재측정 필요).</summary>
    public string Status { get; init; } = "";

    /// <summary>`status_ko`: status의 한국어 문장입니다.</summary>
    public string StatusKo { get; init; } = "";

    /// <summary>`action_ko`: 가장 심한 항목의 조치 문장입니다. 할 일이 없으면 빈 문자열입니다.</summary>
    public string ActionKo { get; init; } = "";

    public IReadOnlyList<QualityItem> Items { get; init; } = Array.Empty<QualityItem>();

    /// <summary>`thermal_contrast.left`: 왼발 열화상 원시값 상위 10%와 하위 25%의 차이(발 - 배경). 없으면 NoNumber입니다.</summary>
    public double ThermalContrastLeft { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`thermal_contrast.right`: 오른발의 같은 값입니다.</summary>
    public double ThermalContrastRight { get; init; } = PerfusionResults.NoNumber;

    /// <summary>가장 심한 항목(action_ko를 낸 항목)의 reason입니다. 모든 항목이 ok나 na이면 빈 문자열입니다.</summary>
    public string WorstReason()
    {
        // 관류 분석 프로그램(quality.assess)과 같이 fail, warn 순으로 가장 심한 항목 중 앞의 것을 고릅니다.
        static int Rank(string severity) => severity == "fail" ? 2 : severity == "warn" ? 1 : 0;
        QualityItem? worst = null;
        foreach (var item in Items)
        {
            if (Rank(item.Severity) > 0 && (worst == null || Rank(item.Severity) > Rank(worst.Severity))) worst = item;
        }
        return worst?.Reason ?? "";
    }
}

/// <summary>발 하나의 부위별 관류지수와 발 인식 품질입니다. 값이 없으면 NoNumber입니다.</summary>
public sealed record PerfusionFoot
{
    /// <summary>`per_side.&lt;발&gt;.pi_toes`: 발가락의 관류지수(%).</summary>
    public double PiToes { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`per_side.&lt;발&gt;.pi_fore`: 앞발의 관류지수(%).</summary>
    public double PiFore { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`per_side.&lt;발&gt;.pi_mid`: 발 중간의 관류지수(%).</summary>
    public double PiMid { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`per_side.&lt;발&gt;.pi_heel`: 뒤꿈치의 관류지수(%).</summary>
    public double PiHeel { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`foot_mask.&lt;발&gt;.method`: 발 영역을 찾은 방법. `footseg_dl`(v8 CNN), `segmentation`(v7까지 DINO + SAM) 또는 `colour`(색 기준)입니다.</summary>
    public string MaskMethod { get; init; } = "";

    /// <summary>`foot_mask.&lt;발&gt;.det_score`: v7까지는 DINO의 발 검출 점수, v8은 CNN이 발바닥으로 본 화소의 평균 확률(0~1).</summary>
    public double DetectionScore { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`foot_mask.&lt;발&gt;.sam_iou`: v7까지는 SAM이 예상한 마스크 정확도, v8은 det_score와 같은 값(0~1).</summary>
    public double MaskIou { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`foot_mask.&lt;발&gt;.blocks`: 발 영역 안의 32x16 블록 수. v7까지는 이 블록으로 관류를 계산했습니다.</summary>
    public double Blocks { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`foot_mask.&lt;발&gt;.cells`: v8에서 이 발에 보인 발 셀 수(20x5 중).</summary>
    public double Cells { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`classification.thermal_mask.&lt;발&gt;.top_dice_vs_thermal_sam`: 열화상 발 마스크와 RGB에서 옮긴 마스크의 겹침(0~1).</summary>
    public double ThermalMaskDice { get; init; } = PerfusionResults.NoNumber;
}

/// <summary>
/// 관류 분석 프로그램이 쓴 `&lt;name&gt;_summary.json`에서 결과 창이 쓰는 값입니다.
/// 숫자가 요약에 없거나 NaN이면 <see cref="PerfusionResults.NoNumber"/>입니다. 관류지수(PI)는 퍼센트이고 좌우는 피험자 기준입니다.
/// </summary>
public sealed record PerfusionSummary
{
    /// <summary>`pi_left`: 왼발 관류지수(%).</summary>
    public double PiLeft { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`pi_right`: 오른발 관류지수(%).</summary>
    public double PiRight { get; init; } = PerfusionResults.NoNumber;

    /// <summary>
    /// `pi_ratio_left_right`: 왼발 PI / 오른발 PI. 키가 없으면 두 PI가 모두 있고 오른발 PI가 0보다 클 때 왼발 / 오른발로 구합니다.
    /// </summary>
    public double PiRatio { get; init; } = PerfusionResults.NoNumber;

    /// <summary>PI가 더 낮은 발. 두 PI가 같거나 하나라도 없으면 null입니다.</summary>
    public FootSide? LowerFoot { get; init; }

    /// <summary>
    /// `cuff_level`: 커프 단계. 요약에 `cuff_level` 항목이 없거나 `null`이면 null입니다.
    /// 2026-10-26 이전 결과(이진 판정만 낸 요약)에는 없습니다.
    /// </summary>
    public CuffLevel? CuffLevel { get; init; }

    /// <summary>
    /// `classification` 항목에 `p_pad`와 `threshold`가 모두 있으면 true입니다. 관류 분석 프로그램이 판정을 건너뛰면 false입니다.
    /// 이진 PAD 판정입니다. 앱은 2026-10-26부터 이 판정을 요청하지 않아(PerfusionRequest.PadClassifier) 이전 결과에만 있습니다.
    /// </summary>
    public bool Classified { get; init; }

    /// <summary>`classification.p_pad`: 판정 모델의 점수(0~1). 시간 창별 점수의 평균입니다.</summary>
    public double PPad { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`classification.threshold`: 점수가 이 값 이상이면 `PAD-like`입니다.</summary>
    public double Threshold { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`classification.decision`: 관류 분석 프로그램이 적은 판정 낱말. `PAD-like` 또는 `normal`입니다.</summary>
    public string Decision { get; init; } = "";

    /// <summary>`classification.kind`: 판정 모델 종류. 예: `multimodal (RGB + thermal)`, `RGB logistic regression`.</summary>
    public string Model { get; init; } = "";

    /// <summary>`classification.affected_side`: 판정 측 발. `PAD-like`일 때만 있습니다.</summary>
    public FootSide? AffectedFoot { get; init; }

    /// <summary>`classification.window_probs`: 판정 모델의 시간 창별 점수입니다.</summary>
    public IReadOnlyList<double> WindowProbs { get; init; } = Array.Empty<double>();

    /// <summary>판정에 열화상을 함께 썼는지. 판정이 있고 모델 종류에 `multimodal`이 들어 있으면 true입니다.</summary>
    public bool ThermalInVerdict { get; init; }

    /// <summary>
    /// `dfr.thermal_skipped`: 판정에서 열화상을 뺀 이유 코드. 빼지 않았거나 기록이 없으면 빈 문자열입니다.
    /// 코드는 관류 분석 프로그램(dfr_worker.py)의 thermal_skip_reason과 check_inputs가 정합니다. <see cref="PerfusionResults.DescribeThermalSkip"/>으로 문장을 만듭니다.
    /// </summary>
    public string ThermalSkipped { get; init; } = "";

    /// <summary>
    /// `dfr.presegment.reason`: 녹화 중 첫 장 미리 분할 결과를 분석이 쓰지 못한 이유 코드. 썼거나 꺼져 있었거나 기록이 없으면 빈 문자열입니다.
    /// </summary>
    public string PresegmentSkipped { get; init; } = "";

    /// <summary>
    /// `dfr.prestream.reason`: 녹화 중 영상 미리 읽기 값을 분석이 쓰지 못하고 영상을 다시 읽은 이유 코드. 썼거나 꺼져 있었거나 기록이 없으면 빈 문자열입니다.
    /// 코드는 관류 분석 프로그램(dfr_worker.py)의 Presegment.miss_reason과 Prestream.REASONS 주석이 정합니다.
    /// <see cref="PerfusionResults.DescribeSpeedupSkip"/>으로 문장을 만듭니다. 결과 값은 어느 경우든 같고 시간만 달라집니다.
    /// </summary>
    public string PrestreamSkipped { get; init; } = "";

    /// <summary>`dfr.variant`: 결과를 낸 연구 코드 판(AnalyzerVariant.Id). 판을 나누기 전의 결과에는 없어 빈 문자열입니다.</summary>
    public string Variant { get; init; } = "";

    /// <summary>`n_windows`: 분석 시간 창 수. 관류 지도 GIF의 장 수와 같습니다. 없으면 0입니다.</summary>
    public int WindowCount { get; init; }

    /// <summary>`window_s`: 시간 창 길이(마이크로초). 없으면 0입니다.</summary>
    public long WindowUs { get; init; }

    /// <summary>`window_stride_s`: 시간 창 시작 간격(마이크로초). 없으면 0입니다.</summary>
    public long WindowStrideUs { get; init; }

    /// <summary>`fps`: 관류 분석 프로그램이 쓴 영상 fps입니다.</summary>
    public double Fps { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`n_frames`: 분석한 장 수입니다. 없으면 0입니다.</summary>
    public long Frames { get; init; }

    /// <summary>`dfr.upstream_commit`: 관류 분석 프로그램 사본이 가져온 연구 코드 커밋. 없으면 빈 문자열입니다.</summary>
    public string UpstreamCommit { get; init; } = "";

    public PerfusionFoot Left { get; init; } = new();

    public PerfusionFoot Right { get; init; } = new();

    /// <summary>`template_corr_lr`: 좌우 발 맥박 파형의 상관(-1~1). 두 발에서 같은 심박을 찾았는지 보는 값입니다.</summary>
    public double TemplateCorr { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`n_light_ref_min`: 조명 잡음 제거에 쓴 배경 블록 수의 최솟값. 0이면 배경 기준 없이 계산한 구간이 있습니다.</summary>
    public double LightRefMin { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`foot_cells.used`: v8에서 PI를 계산한 셀 수. 두 발 모두에서 보이고 잡음이 적은 셀입니다(100개 중).</summary>
    public double CellsUsed { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`foot_cells.common`: v8에서 두 발 모두에서 보인 셀 수. used와의 차이는 잡음이 커 뺀 셀입니다.</summary>
    public double CellsCommon { get; init; } = PerfusionResults.NoNumber;

    /// <summary>`classification.window_centres_s`: 판정 구간 가운데 시각(초). WindowProbs와 같은 순서입니다.</summary>
    public IReadOnlyList<double> WindowCentresS { get; init; } = Array.Empty<double>();

    /// <summary>`qc`: 측정 품질 안내. `3a9d830` 이전 결과이거나 관류 분석 프로그램이 계산하지 못해 `null`이면 null입니다.</summary>
    public MeasurementQuality? Quality { get; init; }
}

/// <summary>판정의 종류입니다. 화면의 색과 문구를 정합니다.</summary>
public enum PadVerdict : byte
{
    /// <summary>판정이 없거나 관류 분석 프로그램이 모르는 낱말을 적음</summary>
    None,

    /// <summary>`normal`: 점수가 기준보다 낮음</summary>
    Normal,

    /// <summary>`PAD-like`: 점수가 기준 이상</summary>
    PadLike,
}

/// <summary><see cref="PerfusionResults.CollectPerfusionResults"/>의 결과입니다.</summary>
public sealed record CollectedResults
{
    /// <summary>분석결과 폴더(OS 구분자)입니다.</summary>
    public string Folder { get; init; } = "";

    /// <summary>분석결과 폴더 안 결과 파일의 경로(이름 순). 이미 같은 내용으로 있던 파일도 넣습니다.</summary>
    public IReadOnlyList<string> Files { get; init; } = Array.Empty<string>();

    /// <summary>이번에 새로 복사한 파일 수입니다. 같은 결과를 다시 모으면 0입니다.</summary>
    public int Copied { get; init; }

    /// <summary>모은 번호입니다. 1이면 번호 없는 이름이고, 2 이상이면 이름 앞부분 끝에 `_reanalysis&lt;번호&gt;`가 있습니다.</summary>
    public int Reanalysis { get; init; } = 1;
}

/// <summary>
/// 관류 분석 프로그램의 결과 파일(요약 JSON, 결과 배열, GIF, 그림)을 읽고 분석결과 폴더로 모읍니다.
/// </summary>
public static partial class PerfusionResults
{
    /// <summary>요약 JSON에 없는 숫자입니다. 0과 구별하려고 NaN을 씁니다.</summary>
    public const double NoNumber = double.NaN;

    /// <summary>
    /// 분석 결과와 함께 항상 보이는 문장입니다. 결과 창과 결과 파일이 같은 문장을 씁니다.
    /// </summary>
    /// <remarks>
    /// 커프 단계 모델은 이 장비로 찍은 소수 피험자의 고정 카메라 설정 촬영으로 학습했습니다. 2026-10-26 노트북 실측에서
    /// 학습에 쓰지 않은 피험자는 8건 중 5건이 실제보다 한 단계 낮게 나왔습니다.
    /// </remarks>
    public const string Disclaimer =
        "선별을 돕는 참고값이며 진단이 아닙니다. 커프 단계는 소수 피험자로 학습해, 처음 찍는 사람은 실제보다 한 단계 낮게 " +
        "나올 수 있습니다.";

    // 숫자이면 그 값, NaN이나 무한대이면 NoNumber, 없거나 숫자가 아니면 null입니다.
    private static double? NumberAt(string json, params string[] path)
    {
        var scalar = new JsonReader(json).FindScalar(path);
        if (scalar is not { } found) return null;
        if (found.Kind == JsonKind.NonFinite) return NoNumber;
        return found.Kind == JsonKind.Number ? ParseDouble(found.Text) : null;
    }

    // 앱이 문화권을 바꿔도 소수점을 '.'로 읽도록 고정 문화권으로 읽습니다. 범위를 넘는 값은 읽지 못한 것으로 봅니다.
    private static double? ParseDouble(string text) =>
        double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var value) && double.IsFinite(value) ? value : null;

    // 숫자 배열입니다. 숫자가 아닌 칸은 NoNumber로 두어 순서를 지킵니다. 키가 없으면 빈 배열입니다.
    private static double[] Numbers(string json, params string[] path)
    {
        var items = new JsonReader(json).FindArray(path);
        if (items == null) return [];
        var output = new double[items.Count];
        for (var i = 0; i < output.Length; ++i)
        {
            output[i] = (items[i].Kind == JsonKind.Number ? ParseDouble(items[i].Text) : NoNumber) ?? NoNumber;
        }
        return output;
    }

    private static string StringAt(string json, params string[] path) => new JsonReader(json).FindString(path) ?? "";

    private static double Number(string json, params string[] path) => NumberAt(json, path) ?? NoNumber;

    private static FootSide? FootOf(string word) => word switch
    {
        "left" => FootSide.Left,
        "right" => FootSide.Right,
        _ => null,
    };

    private static long SecondsToUs(double? seconds) =>
        seconds is { } s && double.IsFinite(s) && s > 0.0 ? (long)Math.Round(s * 1e6, MidpointRounding.AwayFromZero) : 0;

    private static int Rounded(double value) => (int)Math.Round(value, MidpointRounding.AwayFromZero);

    /// <summary>
    /// 요약 JSON 내용을 읽습니다.
    /// </summary>
    /// <param name="json">`&lt;name&gt;_summary.json`의 내용</param>
    /// <param name="error">읽지 못하면 이유를 씁니다. 읽었으면 빈 문자열입니다</param>
    /// <returns>읽은 값. JSON 문법이 틀리거나 `pi_left`, `pi_right` 키가 없으면 null</returns>
    /// <remarks>`pi_left`, `pi_right`가 NaN이면 키가 있으므로 읽고 값은 NoNumber입니다. 나머지 키는 없으면 기본값으로 둡니다.</remarks>
    public static PerfusionSummary? ReadPerfusionSummary(string json, out string error)
    {
        error = "";
        if (NumberAt(json, "pi_left") is not { } piLeft || NumberAt(json, "pi_right") is not { } piRight)
        {
            error = "요약에 pi_left 또는 pi_right가 없거나 JSON을 읽지 못했습니다";
            return null;
        }
        var both = double.IsFinite(piLeft) && double.IsFinite(piRight);
        var ratio = NoNumber;
        if (NumberAt(json, "pi_ratio_left_right") is { } given) ratio = given;
        else if (both && piRight > 0.0) ratio = piLeft / piRight;
        FootSide? lower = null;
        if (both && piLeft < piRight) lower = FootSide.Left;
        if (both && piRight < piLeft) lower = FootSide.Right;

        var summary = new PerfusionSummary
        {
            PiLeft = piLeft,
            PiRight = piRight,
            PiRatio = ratio,
            LowerFoot = lower,
            CuffLevel = ReadCuffLevel(json),
            Left = ReadFoot(json, "left"),
            Right = ReadFoot(json, "right"),
            TemplateCorr = Number(json, "template_corr_lr"),
            LightRefMin = Number(json, "n_light_ref_min"),
            CellsUsed = Number(json, "foot_cells", "used"),
            CellsCommon = Number(json, "foot_cells", "common"),
            Quality = ReadQuality(json),
            ThermalSkipped = StringAt(json, "dfr", "thermal_skipped"),
            PresegmentSkipped = StringAt(json, "dfr", "presegment", "reason"),
            PrestreamSkipped = StringAt(json, "dfr", "prestream", "reason"),
            UpstreamCommit = StringAt(json, "dfr", "upstream_commit"),
            Variant = StringAt(json, "dfr", "variant"),
            WindowUs = SecondsToUs(NumberAt(json, "window_s")),
            WindowStrideUs = SecondsToUs(NumberAt(json, "window_stride_s")),
            Fps = Number(json, "fps"),
        };
        if (NumberAt(json, "n_windows") is { } count && double.IsFinite(count) && count > 0) summary = summary with { WindowCount = (int)count };
        if (NumberAt(json, "n_frames") is { } frames && double.IsFinite(frames) && frames > 0) summary = summary with { Frames = (long)frames };

        // classification 안에서만 찾습니다. thermal_mask, foot_mask에도 left, method 같은 키가 있습니다.
        if (NumberAt(json, "classification", "p_pad") is { } pPad && NumberAt(json, "classification", "threshold") is { } threshold)
        {
            var model = StringAt(json, "classification", "kind");
            summary = summary with
            {
                Classified = true,
                PPad = pPad,
                Threshold = threshold,
                Decision = StringAt(json, "classification", "decision"),
                Model = model,
                AffectedFoot = FootOf(StringAt(json, "classification", "affected_side")),
                WindowProbs = Numbers(json, "classification", "window_probs"),
                ThermalInVerdict = model.Contains("multimodal", StringComparison.Ordinal),
                WindowCentresS = Numbers(json, "classification", "window_centres_s"),
            };
        }
        return summary;
    }

    public static PerfusionSummary? ReadPerfusionSummary(string json) => ReadPerfusionSummary(json, out _);

    // 규칙 판정은 맥박 품질이 부족하면 level_pct_sbp를 null로 둡니다. 그래도 특징 값과 이유를 보이도록 항목을 읽습니다.
    private static CuffLevel? ReadCuffLevel(string json)
    {
        if (new JsonReader(json).FindScalar("cuff_level", "applicable") is not { } applicable) return null;
        var level = NumberAt(json, "cuff_level", "level_pct_sbp");
        var numericLevel = level is { } l && double.IsFinite(l);
        var hasLevel = numericLevel;
        // v9는 상태를 문자로 내고 level_pct_sbp가 없어, 단계가 있는지를 level 문자로 정합니다.
        var levelCode = StringAt(json, "cuff_level", "level");
        var levelCodeIfIgnored = StringAt(json, "cuff_level", "level_if_quality_ignored");
        var states = levelCodeIfIgnored.Length != 0;
        if (states) hasLevel = levelCode.Length != 0;
        var cuff = new CuffLevel
        {
            HasLevel = hasLevel,
            LevelPct = numericLevel ? Rounded(level!.Value) : 0,
            E = Number(json, "cuff_level", "E"),
            PGe40 = Number(json, "cuff_level", "p_ge40"),
            PGe80 = Number(json, "cuff_level", "p_ge80"),
            P120 = Number(json, "cuff_level", "p_120"),
            Cuts = Numbers(json, "cuff_level", "cuts"),
            Applicable = applicable.Kind == JsonKind.True,
            SettingMode = StringAt(json, "cuff_level", "setting", "mode"),
            ExposureStep = Number(json, "cuff_level", "setting", "exposure_step"),
            Gain = Number(json, "cuff_level", "setting", "gain"),
            WindowE = Numbers(json, "cuff_level", "window_E"),
            WindowCentresS = Numbers(json, "cuff_level", "window_centres_s"),
            Rule = StringAt(json, "cuff_level", "rule"),
            LevelCode = levelCode,
            LevelKo = StringAt(json, "cuff_level", "level_ko"),
            LevelCodeIfQualityIgnored = levelCodeIfIgnored,
            States = states,
            PadProbability = Number(json, "cuff_level", "pad_probability"),
            PState0 = Number(json, "cuff_level", "state_probabilities", "0"),
            PStateV = Number(json, "cuff_level", "state_probabilities", "V"),
            PStateD = Number(json, "cuff_level", "state_probabilities", "D"),
            PStateO = Number(json, "cuff_level", "state_probabilities", "O"),
            H1re30 = Number(json, "cuff_level", "features", "h1re_30"),
            Delay2_30Ms = Number(json, "cuff_level", "features", "delay2_30"),
            PiRatio = Number(json, "cuff_level", "features", "pi_ratio"),
            SnrDb = Number(json, "cuff_level", "features", "hi_snr1_30"),
            LowerSide = StringAt(json, "cuff_level", "features", "lower_side"),
            H1re30Below = Number(json, "cuff_level", "thresholds", "h1re_30"),
            Delay2_30AboveMs = Number(json, "cuff_level", "thresholds", "delay2_30"),
            PiRatioBelow = Number(json, "cuff_level", "thresholds", "pi_ratio"),
            QualitySnrDb = Number(json, "cuff_level", "quality_snr_db"),
        };
        if (new JsonReader(json).FindScalar("cuff_level", "quality_ok") is { } ok) cuff = cuff with { QualityOk = ok.Kind != JsonKind.False };
        if (NumberAt(json, "cuff_level", "level_if_quality_ignored") is { } ignored && double.IsFinite(ignored))
        {
            cuff = cuff with { LevelIfQualityIgnored = Rounded(ignored) };
        }
        if (new JsonReader(json).FindScalar("cuff_level", "pad") is { Kind: JsonKind.True or JsonKind.False } pad)
        {
            cuff = cuff with { Pad = pad.Kind == JsonKind.True };
        }
        return cuff;
    }

    private static PerfusionFoot ReadFoot(string json, string side) => new()
    {
        PiToes = Number(json, "per_side", side, "pi_toes"),
        PiFore = Number(json, "per_side", side, "pi_fore"),
        PiMid = Number(json, "per_side", side, "pi_mid"),
        PiHeel = Number(json, "per_side", side, "pi_heel"),
        MaskMethod = StringAt(json, "foot_mask", side, "method"),
        DetectionScore = Number(json, "foot_mask", side, "det_score"),
        MaskIou = Number(json, "foot_mask", side, "sam_iou"),
        Blocks = Number(json, "foot_mask", side, "blocks"),
        Cells = Number(json, "foot_mask", side, "cells"),
        ThermalMaskDice = Number(json, "classification", "thermal_mask", side, "top_dice_vs_thermal_sam"),
    };

    // 측정 품질 안내(qc)는 status가 있을 때만 읽습니다. items는 객체 배열이라 번호로 하나씩 꺼냅니다.
    private static MeasurementQuality? ReadQuality(string json)
    {
        var status = StringAt(json, "qc", "status");
        if (status.Length == 0) return null;
        var items = new List<QualityItem>();
        for (var i = 0; ; ++i)
        {
            var at = i.ToString(CultureInfo.InvariantCulture);
            if (new JsonReader(json).FindScalar("qc", "items", at, "code") is not { Kind: JsonKind.String } code) break;
            string Text(string key) =>
                new JsonReader(json).FindScalar("qc", "items", at, key) is { Kind: JsonKind.String or JsonKind.Number } v ? v.Text : "";
            items.Add(new QualityItem
            {
                Code = code.Text,
                NameKo = Text("name_ko"),
                Severity = Text("severity"),
                Value = Text("value"),
                Unit = Text("unit"),
                Threshold = Text("threshold"),
                MessageKo = Text("message_ko"),
                ActionKo = Text("action_ko"),
                Reason = Text("reason"),
            });
        }
        return new MeasurementQuality
        {
            Status = status,
            StatusKo = StringAt(json, "qc", "status_ko"),
            ActionKo = StringAt(json, "qc", "action_ko"),
            Items = items,
            ThermalContrastLeft = Number(json, "qc", "thermal_contrast", "left"),
            ThermalContrastRight = Number(json, "qc", "thermal_contrast", "right"),
        };
    }

    /// <summary>
    /// 요약의 판정 낱말을 판정 종류로 바꿉니다.
    /// </summary>
    /// <param name="summary"><see cref="ReadPerfusionSummary(string, out string)"/>의 결과</param>
    /// <returns>판정이 없거나 낱말이 `PAD-like`, `normal`이 아니면 <see cref="PadVerdict.None"/>입니다. 모르는 낱말을 어느 한쪽으로 짐작하지 않습니다</returns>
    public static PadVerdict PadVerdictOf(PerfusionSummary summary)
    {
        if (!summary.Classified) return PadVerdict.None;
        return summary.Decision switch
        {
            "PAD-like" => PadVerdict.PadLike,
            "normal" => PadVerdict.Normal,
            _ => PadVerdict.None,
        };
    }

    /// <summary>
    /// 열화상 판정 제외 이유 코드를 문장으로 바꿉니다.
    /// </summary>
    /// <param name="code"><see cref="PerfusionSummary.ThermalSkipped"/></param>
    /// <returns>한 문장. 모르는 코드는 코드를 그대로 넣은 문장입니다. 빈 코드는 빈 문자열입니다</returns>
    public static string DescribeThermalSkip(string code) => code switch
    {
        "" => "",
        "model_missing" => "열화상 판정 모델 파일이 없습니다.",
        "rgb_segmentation_failed" => "RGB 영상에서 발 분할에 실패한 발이 있습니다.",
        "too_short" => "RGB 영상이 30fps 900장보다 짧습니다.",
        "no_thermal_files" => "열화상 분석 파일(thermal_left.npy, thermal_right.npy)이 없습니다.",
        "thermal_foot_not_found" => "열화상에서 발을 찾지 못했습니다.",
        "thermal_not_portrait" => "열화상 발 영역이 세로보다 가로가 길어 발 검출기 입력 조건에 맞지 않습니다.",
        "thermal_too_short" => "열화상 장 수가 30초 동안 7.5Hz 구간을 모두 채우지 못합니다.",
        _ => "관류 분석 프로그램이 적은 이유 코드: " + code,
    };

    /// <summary>
    /// 녹화 중 미리 분할, 미리 읽기를 분석이 쓰지 못한 이유 코드를 문장으로 바꿉니다.
    /// </summary>
    /// <param name="code"><see cref="PerfusionSummary.PresegmentSkipped"/> 또는 <see cref="PerfusionSummary.PrestreamSkipped"/></param>
    /// <returns>한 문장. 모르는 코드는 코드를 그대로 넣은 문장입니다. 빈 코드는 빈 문자열입니다</returns>
    public static string DescribeSpeedupSkip(string code) => code switch
    {
        "" => "",
        "model_not_loaded" => "녹화가 시작될 때 발 분할 모델이 아직 올라 있지 않았습니다.",
        "first_frame_unreadable" => "녹화 중인 영상에서 첫 장을 읽지 못했습니다.",
        "not_requested" or "not_started" => "이번 녹화의 미리 계산 요청이 관류 분석 프로그램에 닿지 않았습니다.",
        "first_frame_mismatch" => "녹화 중에 읽은 첫 장과 저장된 영상의 첫 장이 다릅니다.",
        "file_not_created" => "녹화 시작 뒤 20초 안에 영상 파일이 생기지 않았습니다.",
        "open_failed" => "녹화 중인 영상을 열지 못했습니다.",
        "not_30fps" => "영상이 30fps가 아닙니다.",
        "no_presegment" => "첫 장의 발 분할 결과가 없어 미리 읽지 않았습니다.",
        "cancelled" => "다음 녹화가 시작되어 미리 읽기를 멈췄습니다.",
        "frame_count_mismatch" => "미리 읽은 장 수가 분석할 장 수와 다릅니다.",
        "verify_failed" => "미리 읽은 값이 저장된 영상에서 다시 읽은 값과 다릅니다.",
        _ => "관류 분석 프로그램이 적은 이유 코드: " + code,
    };

    /// <summary>
    /// 관류 지도 GIF의 경로입니다.
    /// </summary>
    /// <param name="outputDir">분석 요청의 결과 폴더</param>
    /// <param name="name">분석 요청의 결과 이름</param>
    /// <returns>`&lt;outputDir&gt;/&lt;name&gt;_perfusion.gif`. 구분자는 '/'입니다</returns>
    public static string PerfusionGifPath(string outputDir, string name) => ResultPath(outputDir, name + "_perfusion.gif");

    private static string ResultPath(string outputDir, string file)
    {
        var path = outputDir;
        if (path.Length > 0 && path[^1] != '/' && path[^1] != '\\') path += '/';
        return path + file;
    }

    /// <summary>
    /// 분석결과 폴더에 복사하는 결과 파일인지. 이름이 `&lt;name&gt;_`로 시작하고 요약, 보고서, 그림(.png), GIF, 프레임 간격 CSV이면 true입니다.
    /// </summary>
    public static bool IsCollectedResultFile(string fileName, string name)
    {
        var head = name + "_";
        if (name.Length == 0 || fileName.Length <= head.Length || !fileName.StartsWith(head, StringComparison.Ordinal)) return false;
        var rest = fileName[head.Length..];
        return rest == "summary.json" || rest == "report.txt" || rest == "perfusion.gif" || rest.EndsWith(".png", StringComparison.Ordinal) ||
               rest == "frame_timing.csv" || rest == "frame_windows.csv";
    }

    /// <summary>
    /// 분석 결과 중 요약, 보고서, 그림, GIF를 `&lt;root&gt;/results/&lt;앞부분&gt;_*`로 복사합니다.
    /// </summary>
    /// <param name="outputDir">분석 요청의 결과 폴더</param>
    /// <param name="name">분석 요청의 결과 이름</param>
    /// <param name="root">저장 위치</param>
    /// <param name="layout">촬영 폴더 배치. 앞부분을 만드는 데 Folder만 씁니다</param>
    /// <param name="tag">비어 있지 않으면 앞부분 뒤에 `_&lt;tag&gt;`를 붙입니다(연구 코드 판 이름). 판마다 결과 파일 이름이 같아 서로 덮지 않게 합니다</param>
    /// <param name="error">실패하면 이유를 씁니다. 성공하면 빈 문자열입니다</param>
    /// <returns>
    /// 분석결과 폴더, 결과 파일 경로, 새로 복사한 수, 모은 번호. 결과 폴더를 읽지 못하거나, 앞부분이 비었거나,
    /// 번호 <see cref="ShotLayout.MaxReanalysisNumber"/>까지 모두 내용이 다른 파일이 있거나, 복사하지 못하면 null
    /// </returns>
    /// <remarks>
    /// 결과 파일 이름 앞의 `&lt;name&gt;`을 앞부분(<see cref="RecordFunctions.CollectedResultPrefix"/>(layout, 번호))으로 바꿉니다. 번호 1부터
    /// 대상 이름을 모두 확인하고, 같은 이름의 파일이 이미 있으면 내용을 바이트로 비교합니다. 모두 없거나 같으면 그 번호로
    /// 복사합니다(같은 결과를 다시 모으면 복사하지 않고 통과). 하나라도 내용이 다르면 그 번호에는 아무것도 복사하지 않고
    /// 다음 번호(_reanalysis2, _reanalysis3)를 확인합니다. 기존 파일은 덮어쓰지 않습니다.
    /// 분석 결과 배열 파일(.npz)과 관류 분석 프로그램 기록은 복사하지 않습니다. 파일 입출력이 있으므로 화면 스레드에서 부르지 않습니다.
    /// </remarks>
    public static CollectedResults? CollectPerfusionResults(string outputDir, string name, string root, ShotLayout layout, string tag, out string error)
    {
        error = "";
        if (RecordFunctions.CollectedResultPrefix(layout).Length == 0)
        {
            error = "분석결과 이름 앞부분이 비었습니다";
            return null;
        }
        var names = new List<string>();
        try
        {
            foreach (var path in Directory.EnumerateFiles(outputDir))
            {
                var file = Path.GetFileName(path);
                if (IsCollectedResultFile(file, name)) names.Add(file);
            }
        }
        catch (Exception e) when (IsFileError(e))
        {
            error = $"분석 결과 폴더를 읽지 못함: {outputDir} ({e.Message})";
            return null;
        }
        names.Sort(StringComparer.Ordinal);
        var target = Path.Combine(root, ShotLayout.CollectedResultFolder).Replace(Path.AltDirectorySeparatorChar, Path.DirectorySeparatorChar);

        // 복사 전에 모두 확인해, 내용이 다른 파일이 하나라도 있으면 그 번호에는 일부만 복사된 상태를 남기지 않습니다.
        var plans = new List<(string From, string To, bool Present)>();
        var conflict = "";
        var number = 1;
        for (; number <= ShotLayout.MaxReanalysisNumber; ++number)
        {
            var prefix = RecordFunctions.CollectedResultPrefix(layout, number) + (tag.Length == 0 ? "" : "_" + tag);
            plans.Clear();
            conflict = "";
            foreach (var file in names)
            {
                var from = Path.Combine(outputDir, file);
                var to = Path.Combine(target, prefix + file[name.Length..]);
                var present = false;
                if (File.Exists(to) || Directory.Exists(to))
                {
                    if (SameBytes(from, to) is not { } same)
                    {
                        error = "분석결과 파일을 비교하지 못함: " + to;
                        return null;
                    }
                    if (!same)
                    {
                        conflict = to;
                        break;
                    }
                    present = true;
                }
                plans.Add((from, to, present));
            }
            if (conflict.Length == 0) break;
        }
        if (conflict.Length != 0)
        {
            error = $"재분석 번호(_reanalysis) {ShotLayout.MaxReanalysisNumber}까지 내용이 다른 같은 이름 파일이 있음: {conflict}";
            return null;
        }
        try
        {
            Directory.CreateDirectory(target);
        }
        catch (Exception e) when (IsFileError(e))
        {
            error = $"분석결과 폴더를 만들지 못함: {target} ({e.Message})";
            return null;
        }
        var copied = 0;
        var files = new List<string>();
        foreach (var (from, to, present) in plans)
        {
            if (!present)
            {
                try
                {
                    File.Copy(from, to, overwrite: false);
                }
                catch (Exception e) when (IsFileError(e))
                {
                    error = $"복사하지 못함: {to} ({e.Message})";
                    return null;
                }
                ++copied;
            }
            files.Add(to);
        }
        return new CollectedResults { Folder = target, Files = files, Copied = copied, Reanalysis = number };
    }

    public static CollectedResults? CollectPerfusionResults(string outputDir, string name, string root, ShotLayout layout) =>
        CollectPerfusionResults(outputDir, name, root, layout, "", out _);

    // 두 파일의 바이트가 같은지. 읽지 못하면 null입니다.
    private static bool? SameBytes(string a, string b)
    {
        try
        {
            using var fa = new FileStream(a, FileMode.Open, FileAccess.Read, FileShare.ReadWrite, 1 << 16);
            using var fb = new FileStream(b, FileMode.Open, FileAccess.Read, FileShare.ReadWrite, 1 << 16);
            if (fa.Length != fb.Length) return false;
            var ba = new byte[1 << 16];
            var bb = new byte[1 << 16];
            for (;;)
            {
                var na = fa.Read(ba, 0, ba.Length);
                if (na == 0) return true;
                fb.ReadExactly(bb, 0, na);
                if (!ba.AsSpan(0, na).SequenceEqual(bb.AsSpan(0, na))) return false;
            }
        }
        catch (Exception e) when (IsFileError(e))
        {
            return null;
        }
    }

    private static bool IsFileError(Exception e) =>
        e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException;
}
