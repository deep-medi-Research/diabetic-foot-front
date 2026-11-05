#ifndef HWLAB_PLATFORM_PERFUSION_ANALYSIS_H_
#define HWLAB_PLATFORM_PERFUSION_ANALYSIS_H_

#include <cstdint>
#include <optional>
#include <string>

namespace hwlab::platform {

/**
 * 관류 분석 프로그램(dfr_worker.py)에 보내는 관류 분석 요청 하나입니다.
 *
 * 관류 분석 프로그램은 표준 입력으로 JSON 한 줄을 받고, 좌우 발 영상과 촬영 폴더 안의 `thermal_left.npy`,
 * `thermal_right.npy`(있을 때)를 읽어 `<output_dir>/<name>_summary.json` 등을 씁니다.
 * 영상 경로는 둘 다 주거나 둘 다 비웁니다. 둘 다 비우면 관류 분석 프로그램은 촬영 폴더의 `left.avi`, `right.avi`를
 * 찾습니다(기존 앱과 같은 동작). 장 수를 주면 관류 분석 프로그램은 영상 머리글의 장 수와 다를 때 분석을 거부합니다.
 *
 * @note 상주 관류 분석 프로그램(dfr_worker.py)만 씁니다. 단독 스크립트를 건마다 띄우는 길은 두지 않습니다.
 *       그 스크립트는 영상 이름을 `left.avi`, `right.avi`로 고정해 FFV1 MKV를 받지 못하고, 매번 모델을 다시 올립니다.
 * @note 열화상은 관류 분석 프로그램이 촬영 폴더에서 `thermal_{left,right}.npy` 이름으로 찾으므로 경로 키가 없습니다.
 * @note 그 밖의 기본값은 기존 앱(PerfusionRunner)이 보내던 값과 같습니다.
 */
struct PerfusionRequest {
  /** 촬영 폴더(UTF-8). 요청 키 `rec_dir`입니다. */
  std::string recording_dir;
  /** 왼발 영상 경로(UTF-8, 피험자 기준). 비어 있으면 요청 키 `left_video`를 넣지 않습니다. */
  std::string left_video;
  /** 오른발 영상 경로(UTF-8, 피험자 기준). 비어 있으면 요청 키 `right_video`를 넣지 않습니다. */
  std::string right_video;
  /** 왼발 영상에 쓴 장 수(사이드카 기록). 없으면 요청 키 `left_frames`를 넣지 않습니다. */
  std::optional<int64_t> left_frames;
  /** 오른발 영상에 쓴 장 수(사이드카 기록). 없으면 요청 키 `right_frames`를 넣지 않습니다. */
  std::optional<int64_t> right_frames;
  /** 결과 폴더(UTF-8). 요청 키 `output`입니다. */
  std::string output_dir;
  /** 결과 파일 이름의 앞부분. 비우면 관류 분석 프로그램이 폴더 이름으로 짓습니다. 요청 키 `name`입니다. */
  std::string name;
  /** 커프를 감은 발. 기존 앱은 `none`을 보냅니다. 요청 키 `cuff_side`입니다. */
  std::string cuff_side = "none";
  /** 맥동 템플릿. 판정은 `v106w`에서만 동작합니다. 요청 키 `template`입니다. */
  std::string pulse_template = "v106w";
  /** 판정 단계를 돌릴지. 끄면 커프 단계도 내지 않습니다. 요청 키 `classify`입니다. */
  bool classify = true;
  /**
   * 이진 PAD 판정(P(PAD 의심))도 낼지. 요청 키 `pad_classifier`입니다.
   *
   * @note 끄면 커프 단계만 내고, 판정용 계산과 열화상 준비를 건너뛰어 분석이 빨라집니다.
   *       2026-10-26부터 판정을 커프 단계로 바꿔 기본값이 false입니다.
   */
  bool pad_classifier = false;
  /** 관류 지도 애니메이션(GIF)을 만들지. 요청 키 `gif`입니다. */
  bool gif = true;
  /**
   * 그림(PNG)과 GIF를 분석 완료 뒤에 그릴지. 요청 키 `defer_figures`입니다.
   *
   * @note 켜면 관류 분석 프로그램이 요약과 npz를 쓴 뒤 `[dfr] done`을 찍고, 그림을 다 그리면 `[dfr] figures`를 찍습니다.
   *       결과 창은 done에서 값을 보이고 figures에서 GIF를 다시 읽습니다. 노트북에서 결과가 약 2초 먼저 보입니다.
   */
  bool defer_figures = true;
};

/**
 * 분석 요청 한 줄(JSON)을 만듭니다. 끝에 줄바꿈 문자를 넣지 않습니다.
 *
 * @param request 요청
 * @param error 만들지 못하면 이유를 씁니다. nullptr이면 쓰지 않습니다
 * @return 키 순서가 고정된 JSON 한 줄. 한글은 UTF-8 그대로, 역슬래시와 따옴표와 제어 문자는 이스케이프합니다.
 *         영상 경로가 한쪽만 있거나 장 수가 음수이면 nullopt
 * @note 관류 분석 프로그램도 경로 한쪽만 온 요청을 거부하지만, 보내기 전에 막아 원인을 앱에서 알 수 있게 합니다.
 */
std::optional<std::string> BuildPerfusionRequestLine(const PerfusionRequest& request, std::string* error = nullptr);

/**
 * 분할 모델만 미리 올려 두는 요청 한 줄 `{"preload":true}`을 만듭니다.
 *
 * @return JSON 한 줄. 관류 분석 프로그램은 이 요청에 `[dfr] preloaded`로 답하고 `done`을 찍지 않습니다
 */
std::string BuildPreloadLine();

/**
 * 녹화 중인 발 영상의 첫 장을 미리 분할하게 하는 요청 한 줄을 만듭니다.
 *
 * @param left_video 왼발 영상 경로(UTF-8, 피험자 기준)
 * @param right_video 오른발 영상 경로(UTF-8, 피험자 기준)
 * @return `{"presegment":true,"left_video":...,"right_video":...}`. 관류 분석 프로그램은 발마다
 *         `[dfr] presegmented {JSON}`으로 답하고 `done`을 찍지 않습니다
 * @note 관류 분석 프로그램을 `--presegment-threads`와 함께 띄웠을 때만 분할합니다. 아니면 실패 줄 하나로 답합니다.
 */
std::string BuildPresegmentLine(const std::string& left_video, const std::string& right_video);

/** 관류 분석 프로그램 출력 한 줄의 종류입니다. */
enum class AnalyzerLineKind : uint8_t {
  kOther,          // 진행 단계가 아닌 관류 분석 프로그램의 글. 기록에만 남깁니다
  kReady,          // `[dfr] ready` 또는 `[dfr] ready {JSON}`: 관류 분석 프로그램이 요청을 받을 준비가 됨
  kPreloaded,      // `[dfr] preloaded`
  kPreloadFailed,  // `[dfr] preload-failed <이유>`
  kPresegmented,   // `[dfr] presegmented {JSON}`: 녹화 중 첫 장 미리 분할 한 발의 결과
  kStage,          // 줄 맨 앞의 `[n/m] 문구`: 진행 단계
  kSubStage,       // 들여 쓴 `  [k/n] 문구`: 진행 단계 안의 하위 단계(열화상 판정의 7단계)
  kDone,           // `[dfr] done {"ok":...,"error":"..."}`: 요청 한 건의 끝
  kFigures,        // `[dfr] figures {JSON}`: done 뒤로 미룬 그림과 GIF를 다 그림(defer_figures)
  kIdleExit,       // `[dfr] idle-exit`: 관류 분석 프로그램이 쉬다가 스스로 끝남
};

/** 관류 분석 프로그램 출력 한 줄을 해석한 값입니다. */
struct AnalyzerLine {
  AnalyzerLineKind kind = AnalyzerLineKind::kOther;
  /** kStage일 때 단계 번호(0부터)와 전체 단계 수, kSubStage일 때 하위 단계 번호(1부터)와 하위 단계 수입니다. */
  int32_t step = 0;
  int32_t total = 0;
  /**
   * kStage와 kSubStage의 문구(끝의 경과 시간 `[12 s]`, `(0 s)`를 뗀 것), kPreloadFailed의 이유, kOther의 원문,
   * kPresegmented와 kFigures의 JSON입니다.
   */
  std::string message;
  /**
   * kReady 뒤에 JSON이 있을 때 `worker_version`과 `upstream_commit` 값입니다. 없으면 빈 문자열입니다.
   *
   * @note 버전을 찍지 않는 기존 관류 분석 프로그램(`[dfr] ready`만 찍음)과도 동작합니다.
   */
  std::string worker_version;
  std::string upstream_commit;
  /** kDone일 때 성공 여부입니다. 완료 JSON을 읽지 못하면 false입니다. */
  bool ok = false;
  /** kDone일 때 관류 분석 프로그램이 적은 오류(이스케이프를 푼 UTF-8)입니다. 완료 JSON을 읽지 못하면 그 이유입니다. */
  std::string error;
};

/**
 * 관류 분석 프로그램 출력 한 줄을 해석합니다.
 *
 * @param line ChildProcess::ReadLine이 돌려준 줄(줄 끝 문자 없음)
 * @return 해석한 값. 알아보지 못한 줄은 kOther입니다
 * @note 줄 맨 앞의 `[n/m]`은 진행 단계(kStage), 들여 쓴 `  [k/n]`은 하위 단계(kSubStage)입니다. 둘을 나누어
 *       하위 단계가 진행 단계 번호를 되돌리지 않게 합니다. 기존 앱은 들여 쓴 줄을 버렸습니다.
 */
AnalyzerLine ParseAnalyzerLine(const std::string& line);

/** 요약 JSON에 관류 분석 프로그램이 넣은 버전 정보(`dfr` 항목) 중 버전 값입니다. */
struct WorkerStamp {
  /** `dfr.worker_version`. 없으면 빈 문자열입니다. */
  std::string worker_version;
  /** `dfr.upstream_commit`: 관류 분석 프로그램이 소스 커밋으로 적은 값. 없으면 빈 문자열입니다. */
  std::string upstream_commit;
};

/**
 * 요약 JSON 내용에서 관류 분석 프로그램 버전 정보를 읽습니다.
 *
 * @param summary_json `<name>_summary.json`의 내용
 * @return 찾은 값. `dfr` 항목이 없거나 JSON을 읽지 못하면 빈 값입니다
 * @note 관류지수와 판정 같은 요약의 나머지 값은 읽지 않습니다.
 */
WorkerStamp ReadWorkerStamp(const std::string& summary_json);

/**
 * 분석이 끝나면 생기는 요약 JSON의 경로입니다. 성공 판정은 이 파일로 합니다.
 *
 * @param output_dir 요청의 결과 폴더
 * @param name 요청의 결과 이름
 * @return `<output_dir>/<name>_summary.json`. 구분자는 '/'입니다
 */
std::string PerfusionSummaryPath(const std::string& output_dir, const std::string& name);

/** 분석 결과를 쓰는 촬영 폴더 안 하위 폴더 이름입니다. */
inline constexpr char kPerfusionFolderName[] = "perfusion";

/**
 * 분석 판 하나의 분석 결과 폴더입니다.
 *
 * @param shot_folder 촬영 폴더(UTF-8)
 * @param variant 판 이름(AnalyzerVariant::id). 비우면 판을 나누기 전(2026-10-28 이전)의 결과 폴더입니다
 * @return `<shot_folder>/perfusion/<variant>`. variant가 비면 `<shot_folder>/perfusion`입니다. 구분자는 '/'입니다
 * @note 판마다 폴더를 나눠 결과 파일 이름(`<촬영 이름>_summary.json` 등)은 판과 관계없이 같습니다.
 */
std::string PerfusionOutputDir(const std::string& shot_folder, const std::string& variant);

/**
 * 촬영 폴더의 매니페스트와 좌우 영상 사이드카로 분석 요청을 만듭니다.
 *
 * 매니페스트는 `<촬영 폴더>/<폴더 이름>_shot.json`이고, 그 `files`의 `left_video`, `right_video`,
 * `left_sidecar`, `right_sidecar` 이름을 씁니다. 장 수는 사이드카의 `frames.written`입니다.
 *
 * @param folder 촬영 폴더(UTF-8). 끝의 경로 구분자는 무시합니다
 * @param error 만들지 못하면 이유를 씁니다. nullptr이면 쓰지 않습니다
 * @return recording_dir는 folder, 영상 경로는 `<folder>/<파일 이름>`, output_dir는 `<folder>/perfusion`,
 *         name은 폴더 이름입니다. 매니페스트나 사이드카를 읽지 못하거나 좌우 영상 이름이 없으면 nullopt
 * @note 결과 이름을 촬영 폴더 이름으로 정합니다. 기존 앱은 이름_생년월일_기준_시각으로 지었지만, 촬영 폴더 안에
 *       결과를 두므로 폴더 이름으로 충분하고 경로 길이(Windows 259자)를 줄입니다.
 */
std::optional<PerfusionRequest> PerfusionRequestForShot(const std::string& folder, std::string* error = nullptr);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_PERFUSION_ANALYSIS_H_
