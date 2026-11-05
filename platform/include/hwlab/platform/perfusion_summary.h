#ifndef HWLAB_PLATFORM_PERFUSION_SUMMARY_H_
#define HWLAB_PLATFORM_PERFUSION_SUMMARY_H_

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/record/shot.h"
#include "hwlab/record/sidecar.h"

namespace hwlab::platform {

/** 요약 JSON에 없는 숫자입니다. 0과 구별하려고 NaN을 씁니다. */
inline constexpr double kNoNumber = std::numeric_limits<double>::quiet_NaN();

/**
 * 분석 결과와 함께 항상 보이는 문장입니다. 결과 창과 결과 파일이 같은 문장을 씁니다.
 *
 * @note 커프 단계는 소수 피험자의 촬영으로 만든 모델이 추정하므로, 처음 찍는 사람은 실제보다 한 단계 낮게 나올 수 있습니다.
 */
inline constexpr const char kPerfusionDisclaimer[] =
    "선별을 돕는 참고값이며 진단이 아닙니다. 커프 단계는 소수 피험자로 학습해, 처음 찍는 사람은 실제보다 한 단계 낮게 "
    "나올 수 있습니다.";

/**
 * 관류 분석 프로그램이 추정한 커프 단계입니다(요약의 `cuff_level` 항목).
 *
 * 분석 판 v9부터는 커프 상태 0(정상), V(정상 경계, 정맥만 막힘), D(경증, 동맥 부분 압박), O(폐색) 중
 * 하나입니다(states가 true). 네 상태 확률(p_state_*)과 PAD 확률(pad_probability)을 함께 내고 가장 큰 상태를 고릅니다.
 * 그 전 판은 커프 압력을 수축기 혈압 대비 퍼센트로 0, 40, 80, 120 네 단계 중 하나로 추정합니다.
 *
 * @note 추정은 고정 카메라 설정(수동, 게인 1, 노출 -5 또는 -9)으로 찍은 촬영에 맞췄습니다. 다른 설정이면 applicable이
 *       false이고 값은 참고용입니다.
 * @note 발목 혈압 단계 문자 0, V, D, M, S를 쓰는 옛 요약도 states로 읽습니다. M과 S는 폐색입니다.
 */
struct CuffLevel {
  /**
   * 단계가 있으면 true입니다. v8까지는 `level_pct_sbp`가 숫자인지, v9는 `level`이 문자인지로 정합니다. 맥박 품질 부족으로
   * 단계를 내지 않으면(`null`) false입니다.
   */
  bool has_level = true;
  /** `level_pct_sbp`: 추정 단계(수축기 혈압 대비 %). 0, 40, 80, 120 중 하나입니다. has_level이 false이거나 v9 요약이면 0입니다. */
  int32_t level_pct = 0;
  /** 상태를 문자로 낸 요약(v9)이면 true입니다. `level_if_quality_ignored`가 문자이면 그렇습니다. */
  bool states = false;
  /** `level`: 추정 상태 0, V, D, O(옛 요약은 0, V, D, M, S). 품질 미달(`null`)이거나 v8까지의 요약이면 비었습니다. */
  std::string level_code;
  /** `level_ko`: 분석 프로그램이 정한 상태 이름(정상, 정상 (경계), 경증, 폐색). 품질 미달이거나 없으면 비었습니다. */
  std::string level_ko;
  /** v9의 `level_if_quality_ignored`: 품질 검사를 무시했을 때의 상태 문자입니다. 없으면 비었습니다. */
  std::string level_code_if_quality_ignored;
  /** `pad_probability`: 네트워크가 낸 PAD(D 또는 O) 확률(0~1). 품질 미달이어도 있습니다. 없으면 kNoNumber입니다. */
  double pad_probability = kNoNumber;
  /** `pad`: 고른 상태가 D 또는 O이면 true입니다. 품질 미달(`null`)이거나 없으면 nullopt입니다. */
  std::optional<bool> pad;
  /** `state_probabilities`: 네 상태 0, V, D, O의 확률(0~1). 없으면 kNoNumber입니다. */
  double p_state_0 = kNoNumber;
  double p_state_v = kNoNumber;
  double p_state_d = kNoNumber;
  double p_state_o = kNoNumber;
  /** `E`: 세 확률의 합(0~3). */
  double e = kNoNumber;
  /** `p_ge40`, `p_ge80`, `p_120`: 단계가 40 이상, 80 이상, 120일 확률(0~1). */
  double p_ge40 = kNoNumber;
  double p_ge80 = kNoNumber;
  double p_120 = kNoNumber;
  /** `cuts`: E를 나누는 기준값 세 개(작은 순). */
  std::vector<double> cuts;
  /** `applicable`: 고정 카메라 설정으로 찍은 촬영이면 true입니다. */
  bool applicable = false;
  /** `setting.mode`: 촬영의 카메라 노출 방식. `manual` 또는 `auto`이고, 기록이 없으면 빈 문자열입니다. */
  std::string setting_mode;
  /** `setting.exposure_step`: 노출 단계. 자동이거나 기록이 없으면 kNoNumber입니다. */
  double exposure_step = kNoNumber;
  /** `setting.gain`: 게인. 기록이 없으면 kNoNumber입니다. */
  double gain = kNoNumber;
  /** `window_E`: 10초 구간별 E(0~3). e는 이 값들의 평균입니다. 2026-10-26 이전 요약에는 없습니다. */
  std::vector<double> window_e;
  /** `window_centres_s`: 구간 가운데 시각(초). window_e와 같은 순서입니다. */
  std::vector<double> window_centres_s;

  // 아래는 경계별 규칙 판정(2026-10-28 이후 요약)의 값입니다. 로지스틱 회귀 판정의 요약에는 없습니다.

  /** `rule`: 단계를 정한 규칙 문구. 예: `<지표> < <기준값>`, `none of the cuff rules`. 비어 있으면 로지스틱 회귀 판정입니다. */
  std::string rule;
  /** `quality_ok`: 강한 발의 맥박 SNR이 기준 이상이면 true입니다. 키가 없으면 true입니다. */
  bool quality_ok = true;
  /** v8까지의 `level_if_quality_ignored`: 품질 검사를 무시했을 때의 단계(%)입니다. 없거나 문자이면 -1입니다. */
  int32_t level_if_quality_ignored = -1;
  /** `features.h1re_30`: 약한 발과 강한 발의 맥박 기본파 비의 실수부(30초). 120% 규칙에 씁니다. */
  double h1re_30 = kNoNumber;
  /** `features.delay2_30`: 약한 발 맥박 2차 고조파의 위상 지연(ms, 30초). 80% 규칙에 씁니다. */
  double delay2_30_ms = kNoNumber;
  /** `features.pi_ratio`: 약한 발 PI / 강한 발 PI. 40% 규칙에 씁니다. */
  double pi_ratio = kNoNumber;
  /** `features.hi_snr1_30`: 강한 발의 맥박 SNR(dB, 30초). 품질 검사에 씁니다. */
  double snr_db = kNoNumber;
  /** `features.lower_side`: PI가 낮은 발(`left`, `right`). 없으면 빈 문자열입니다. */
  std::string lower_side;
  /**
   * `thresholds`: 규칙의 기준값. h1re_30이 이보다 작으면 120%, delay2_30이 이보다 크면 80%, pi_ratio가 이보다 작으면 40%입니다.
   *
   * @note v9에는 기준값이 없어 모두 kNoNumber입니다.
   */
  double h1re_30_below = kNoNumber;
  double delay2_30_above_ms = kNoNumber;
  double pi_ratio_below = kNoNumber;
  /** `quality_snr_db`: 품질 검사 기준(dB). snr_db가 이보다 작으면 단계를 내지 않습니다. */
  double quality_snr_db = kNoNumber;
};

/**
 * 요약의 `qc`: 측정자에게 보이는 측정 품질 안내입니다. 관류 분석 프로그램 v8 이후 요약에 있습니다.
 *
 * @note 형식은 `docs/analysis-contract.md`가 정합니다.
 */
struct MeasurementQuality {
  /** `status`: `ok`(측정 완료), `check`(측정 완료, 확인 필요), `retake`(재측정 필요). */
  std::string status;
  /** `status_ko`: status의 한국어 문장입니다. */
  std::string status_ko;
  /** `action_ko`: 가장 심한 항목의 조치 문장입니다. 할 일이 없으면 빈 문자열입니다. */
  std::string action_ko;

  /** `items[]`의 항목 하나입니다. */
  struct Item {
    std::string code;        // camera_setting, foot_found, common_cells, brightness, noise_floor, pulse, foot_warmth, heart_rate
    std::string name_ko;     // 항목 이름
    std::string severity;    // ok, warn, fail, na(계산 안 함)
    std::string value;       // 값. 숫자는 요약에 적힌 그대로, 없으면 빈 문자열
    std::string unit;        // 단위. 없으면 빈 문자열
    std::string threshold;   // 기준 문장
    std::string message_ko;  // 경고 설명. ok이면 비었습니다
    std::string action_ko;   // 조치 문장. ok이면 비었습니다
    std::string reason;      // IRB 측정 실패 사유: equipment, subject, posture, other. ok이면 비었습니다
  };
  std::vector<Item> items;

  /** `thermal_contrast.left`, `.right`: 발마다 열화상 원시값 상위 10%와 하위 25%의 차이(발 - 배경). 없으면 kNoNumber입니다. */
  double thermal_contrast_left = kNoNumber;
  double thermal_contrast_right = kNoNumber;

  /** 가장 심한 항목(action_ko를 낸 항목)의 reason입니다. 모든 항목이 ok나 na이면 빈 문자열입니다. */
  std::string WorstReason() const;
};

/**
 * 관류 분석 프로그램이 쓴 `<name>_summary.json`에서 결과 창이 쓰는 값입니다.
 *
 * 숫자가 요약에 없거나 NaN이면 kNoNumber입니다. 관류지수(PI)는 퍼센트이고 좌우는 피험자 기준입니다.
 */
struct PerfusionSummary {
  /** `pi_left`: 왼발 관류지수(%). */
  double pi_left = kNoNumber;
  /** `pi_right`: 오른발 관류지수(%). */
  double pi_right = kNoNumber;
  /**
   * `pi_ratio_left_right`: 왼발 PI / 오른발 PI.
   *
   * @note 키가 없으면 두 PI가 모두 있고 오른발 PI가 0보다 클 때 왼발 / 오른발로 구합니다.
   */
  double pi_ratio = kNoNumber;
  /** PI가 더 낮은 발. 두 PI가 같거나 하나라도 없으면 nullopt입니다. */
  std::optional<record::FootSide> lower_foot;

  /**
   * `cuff_level`: 커프 단계. 요약에 `cuff_level` 항목이 없거나 `null`이면 nullopt입니다.
   *
   * @note 2026-10-26 이전 결과(이진 판정만 낸 요약)에는 없습니다.
   */
  std::optional<CuffLevel> cuff_level;

  /**
   * `classification` 항목에 `p_pad`와 `threshold`가 모두 있으면 true입니다. 관류 분석 프로그램이 판정을 건너뛰면 false입니다.
   *
   * @note 이진 PAD 판정입니다. 앱은 2026-10-26부터 이 판정을 요청하지 않아(PerfusionRequest::pad_classifier) 이전 결과에만
   *       있습니다.
   */
  bool classified = false;
  /** `classification.p_pad`: 판정 모델의 점수(0~1). 시간 창별 점수의 평균입니다. */
  double p_pad = kNoNumber;
  /** `classification.threshold`: 점수가 이 값 이상이면 `PAD-like`입니다. */
  double threshold = kNoNumber;
  /** `classification.decision`: 관류 분석 프로그램이 적은 판정 낱말. `PAD-like` 또는 `normal`입니다. */
  std::string decision;
  /** `classification.kind`: 판정 모델 종류. 예: `multimodal (RGB + thermal)`, `RGB logistic regression`. */
  std::string model;
  /** `classification.affected_side`: 판정 측 발. `PAD-like`일 때만 있습니다. */
  std::optional<record::FootSide> affected_foot;
  /** `classification.window_probs`: 판정 모델의 시간 창별 점수입니다. */
  std::vector<double> window_probs;

  /** 판정에 열화상을 함께 썼는지. 판정이 있고 모델 종류에 `multimodal`이 들어 있으면 true입니다. */
  bool thermal_in_verdict = false;
  /**
   * `dfr.thermal_skipped`: 판정에서 열화상을 뺀 이유 코드. 빼지 않았거나 기록이 없으면 빈 문자열입니다.
   *
   * @note 코드는 `docs/analysis-contract.md`가 정합니다. DescribeThermalSkip으로 문장을 만듭니다.
   */
  std::string thermal_skipped;
  /**
   * `dfr.presegment.reason`: 녹화 중 첫 장 미리 분할 결과를 분석이 쓰지 못한 이유 코드. 썼거나 꺼져 있었거나 기록이 없으면
   * 빈 문자열입니다.
   */
  std::string presegment_skipped;
  /**
   * `dfr.prestream.reason`: 녹화 중 영상 미리 읽기 값을 분석이 쓰지 못하고 영상을 다시 읽은 이유 코드. 썼거나 꺼져 있었거나
   * 기록이 없으면 빈 문자열입니다.
   *
   * @note 코드는 `docs/analysis-contract.md`의 이유 코드 표가 정합니다.
   *       DescribeSpeedupSkip으로 문장을 만듭니다. 결과 값은 어느 경우든 같고 시간만 달라집니다.
   */
  std::string prestream_skipped;
  /** `dfr.variant`: 결과를 낸 분석 판(AnalyzerVariant::id). 판을 나누기 전의 결과에는 없어 빈 문자열입니다. */
  std::string variant;

  /** `n_windows`: 분석 시간 창 수. 관류 지도 GIF의 장 수와 같습니다. 없으면 0입니다. */
  int32_t window_count = 0;
  /** `window_s`: 시간 창 길이(마이크로초). 없으면 0입니다. */
  int64_t window_us = 0;
  /** `window_stride_s`: 시간 창 시작 간격(마이크로초). 없으면 0입니다. */
  int64_t window_stride_us = 0;
  /** `fps`: 관류 분석 프로그램이 쓴 영상 fps입니다. */
  double fps = kNoNumber;
  /** `n_frames`: 분석한 장 수입니다. 없으면 0입니다. */
  int64_t frames = 0;

  /** `dfr.upstream_commit`: 관류 분석 프로그램이 소스 커밋으로 적은 값. 없으면 빈 문자열입니다. */
  std::string upstream_commit;

  /** 발 하나의 부위별 관류지수와 발 인식 품질입니다. 값이 없으면 kNoNumber입니다. */
  struct Foot {
    /** `per_side.<발>.pi_toes`, `pi_fore`, `pi_mid`, `pi_heel`: 발가락, 앞발, 발 중간, 뒤꿈치의 관류지수(%). */
    double pi_toes = kNoNumber;
    double pi_fore = kNoNumber;
    double pi_mid = kNoNumber;
    double pi_heel = kNoNumber;
    /**
     * `foot_mask.<발>.method`: 발 영역을 찾은 방법의 이름입니다. 분석 판이 정하고, 색 기준이면 `colour`입니다.
     */
    std::string mask_method;
    /** `foot_mask.<발>.det_score`: 발 검출 점수(0~1). */
    double detection_score = kNoNumber;
    /** `foot_mask.<발>.sam_iou`: 발 마스크 정확도 예상값(0~1). v8은 det_score와 같은 값입니다. */
    double mask_iou = kNoNumber;
    /** `foot_mask.<발>.blocks`: 발 영역 안의 32x16 블록 수. v7까지는 이 블록으로 관류를 계산했습니다. */
    double blocks = kNoNumber;
    /** `foot_mask.<발>.cells`: v8에서 이 발에 보인 발 셀 수(20x5 중). */
    double cells = kNoNumber;
    /** `classification.thermal_mask.<발>.top_dice_vs_thermal_sam`: 열화상 발 마스크와 RGB에서 옮긴 마스크의 겹침(0~1). */
    double thermal_mask_dice = kNoNumber;
  };
  Foot left;
  Foot right;
  /** `template_corr_lr`: 좌우 발 맥박 파형의 상관(-1~1). 두 발에서 같은 심박을 찾았는지 보는 값입니다. */
  double template_corr = kNoNumber;
  /** `n_light_ref_min`: 조명 잡음 제거에 쓴 배경 블록 수의 최솟값. 0이면 배경 기준 없이 계산한 구간이 있습니다. */
  double light_ref_min = kNoNumber;
  /** `foot_cells.used`: v8에서 PI를 계산한 셀 수. 두 발 모두에서 보이고 잡음이 적은 셀입니다(100개 중). */
  double cells_used = kNoNumber;
  /** `foot_cells.common`: v8에서 두 발 모두에서 보인 셀 수. used와의 차이는 잡음이 커 뺀 셀입니다. */
  double cells_common = kNoNumber;
  /** `classification.window_centres_s`: 판정 구간 가운데 시각(초). window_probs와 같은 순서입니다. */
  std::vector<double> window_centres_s;
  /** `qc`: 측정 품질 안내. `qc`가 없는 옛 결과이거나 관류 분석 프로그램이 계산하지 못해 `null`이면 nullopt입니다. */
  std::optional<MeasurementQuality> quality;
};

/**
 * 요약 JSON 내용을 읽습니다.
 *
 * @param json `<name>_summary.json`의 내용(UTF-8)
 * @param error 읽지 못하면 이유를 씁니다. nullptr이면 쓰지 않습니다
 * @return 읽은 값. JSON 문법이 틀리거나 `pi_left`, `pi_right` 키가 없으면 nullopt
 * @note `pi_left`, `pi_right`가 NaN이면 키가 있으므로 읽고 값은 kNoNumber입니다. 나머지 키는 없으면 기본값으로 둡니다.
 */
std::optional<PerfusionSummary> ReadPerfusionSummary(const std::string& json, std::string* error = nullptr);

/** 판정의 종류입니다. 화면의 색과 문구를 정합니다. */
enum class PadVerdict : uint8_t {
  kNone,     ///< 판정이 없거나 관류 분석 프로그램이 모르는 낱말을 적음
  kNormal,   ///< `normal`: 점수가 기준보다 낮음
  kPadLike,  ///< `PAD-like`: 점수가 기준 이상
};

/**
 * 요약의 판정 낱말을 판정 종류로 바꿉니다.
 *
 * @param summary ReadPerfusionSummary의 결과
 * @return 판정이 없거나 낱말이 `PAD-like`, `normal`이 아니면 kNone
 * @note 모르는 낱말을 어느 한쪽으로 짐작하지 않습니다.
 */
PadVerdict PadVerdictOf(const PerfusionSummary& summary);

/**
 * 열화상 판정 제외 이유 코드를 문장으로 바꿉니다.
 *
 * @param code PerfusionSummary::thermal_skipped
 * @return 한 문장. 모르는 코드는 코드를 그대로 넣은 문장입니다. 빈 코드는 빈 문자열입니다
 */
std::string DescribeThermalSkip(const std::string& code);

/**
 * 녹화 중 미리 분할, 미리 읽기를 분석이 쓰지 못한 이유 코드를 문장으로 바꿉니다.
 *
 * @param code PerfusionSummary::presegment_skipped 또는 prestream_skipped
 * @return 한 문장. 모르는 코드는 코드를 그대로 넣은 문장입니다. 빈 코드는 빈 문자열입니다
 */
std::string DescribeSpeedupSkip(const std::string& code);

/**
 * 관류 지도 GIF의 경로입니다.
 *
 * @param output_dir 분석 요청의 결과 폴더
 * @param name 분석 요청의 결과 이름
 * @return `<output_dir>/<name>_perfusion.gif`. 구분자는 '/'입니다
 */
std::string PerfusionGifPath(const std::string& output_dir, const std::string& name);

/** 분석결과 폴더에 복사하는 결과 파일인지. 이름이 `<name>_`로 시작하고 요약, 보고서, 그림(.png), GIF, 프레임 간격 CSV(frame_timing.h)이면 true입니다. */
bool IsCollectedResultFile(const std::string& file_name, const std::string& name);

/** CollectPerfusionResults의 결과입니다. */
struct CollectedResults {
  /** 분석결과 폴더(UTF-8, OS 구분자)입니다. */
  std::string folder;
  /** 분석결과 폴더 안 결과 파일의 경로(UTF-8, 이름 순). 이미 같은 내용으로 있던 파일도 넣습니다. */
  std::vector<std::string> files;
  /** 이번에 새로 복사한 파일 수입니다. 같은 결과를 다시 모으면 0입니다. */
  int32_t copied = 0;
  /** 모은 번호입니다. 1이면 번호 없는 이름이고, 2 이상이면 이름 앞부분 끝에 `_reanalysis<번호>`가 있습니다. */
  int32_t reanalysis = 1;
};

/**
 * 분석 결과 중 요약, 보고서, 그림, GIF를 `<root>/results/<앞부분>_*`로 복사합니다.
 *
 * 결과 파일 이름 앞의 `<name>`을 앞부분(record::CollectedResultPrefix(layout, 번호))으로 바꿉니다. 번호 1부터
 * 대상 이름을 모두 확인하고, 같은 이름의 파일이 이미 있으면 내용을 바이트로 비교합니다. 모두 없거나 같으면 그 번호로
 * 복사합니다(같은 결과를 다시 모으면 복사하지 않고 통과). 하나라도 내용이 다르면 그 번호에는 아무것도 복사하지 않고
 * 다음 번호(_reanalysis2, _reanalysis3)를 확인합니다. 기존 파일은 덮어쓰지 않습니다.
 *
 * @param output_dir 분석 요청의 결과 폴더(UTF-8)
 * @param name 분석 요청의 결과 이름
 * @param root 저장 위치(UTF-8)
 * @param layout 촬영 폴더 배치. 앞부분을 만드는 데 folder만 씁니다
 * @param tag 비어 있지 않으면 앞부분 뒤에 `_<tag>`를 붙입니다(분석 판 이름). 판마다 결과 파일 이름이 같아 서로 덮지 않게 합니다
 * @param error 실패하면 이유를 씁니다. nullptr이면 쓰지 않습니다
 * @return 분석결과 폴더, 결과 파일 경로, 새로 복사한 수, 모은 번호. 결과 폴더를 읽지 못하거나, 앞부분이 비었거나,
 *         번호 record::kMaxReanalysisNumber까지 모두 내용이 다른 파일이 있거나, 복사하지 못하면 nullopt
 * @note 분석 결과 배열 파일(.npz)과 관류 분석 프로그램 기록은 복사하지 않습니다. 파일 입출력이 있으므로 화면 스레드에서 부르지 않습니다.
 */
std::optional<CollectedResults> CollectPerfusionResults(const std::string& output_dir, const std::string& name,
                                                       const std::string& root, const record::ShotLayout& layout,
                                                       const std::string& tag = {}, std::string* error = nullptr);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_PERFUSION_SUMMARY_H_
