#ifndef HWLAB_PLATFORM_ANALYZER_H_
#define HWLAB_PLATFORM_ANALYZER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/platform/child_process.h"
#include "hwlab/platform/perfusion_analysis.h"

namespace hwlab::platform {

/** 관류 분석 프로그램 프로세스의 상태입니다. */
enum class AnalyzerState : uint8_t {
  kStarting,  // 프로세스를 띄웠고 `[dfr] ready`와 모델 미리 올리기(preload)를 기다림
  kIdle,      // 요청을 받을 수 있음. 대기열이 있으면 보류(SetHold) 중
  kBusy,      // 요청 한 건을 분석 중
  kFailed,    // 띄우지 못했거나 준비 전에 끝남. Submit이나 Restart로 다시 띄움
  kStopped,   // Stop 뒤
};

/** 마지막으로 끝난 요청 한 건입니다. */
struct AnalyzerOutcome {
  /** 요청의 결과 이름(촬영 폴더 이름). 끝난 요청이 없으면 빈 문자열입니다. */
  std::string name;
  /** 요청의 촬영 폴더(UTF-8)입니다. */
  std::string recording_dir;
  /** 관류 분석 프로그램이 성공을 알렸고 요약 JSON이 있으면 true입니다. */
  bool ok = false;
  /** 사용자가 취소해 끝났으면 true입니다. 이때 ok는 false입니다. */
  bool cancelled = false;
  /** 실패 이유(UTF-8). 성공이면 빈 문자열입니다. */
  std::string error;
  /** 요약 JSON 경로(`<output_dir>/<name>_summary.json`). 실패해도 요청에서 정한 경로를 둡니다. */
  std::string summary_path;
  /** 요청을 보낸 때부터 끝날 때까지의 시간입니다. */
  int64_t elapsed_ms = 0;
  /** 요약 JSON의 `dfr.worker_version`. 없으면 준비 줄에서 받은 값입니다. */
  std::string worker_version;
  /** 요약 JSON의 `dfr.upstream_commit`. 없으면 준비 줄에서 받은 값입니다. */
  std::string upstream_commit;
};

/** Analyzer::Status의 결과입니다. */
struct AnalyzerStatus {
  AnalyzerState state = AnalyzerState::kStarting;
  /** kBusy일 때 마지막 진행 줄의 단계 번호와 전체 단계 수입니다. 진행 줄이 아직 없으면 0입니다. */
  int32_t step = 0;
  int32_t total = 0;
  /** kBusy일 때 진행 줄의 문구, kFailed일 때 실패 이유, kStarting일 때 지금 기다리는 것입니다. */
  std::string message;
  /** kBusy일 때 현재 단계 안의 하위 단계 번호(1부터)와 하위 단계 수입니다. 하위 단계 줄이 없거나 단계가 바뀌면 0입니다. */
  int32_t sub_step = 0;
  int32_t sub_total = 0;
  /** 마지막 하위 단계 줄의 문구입니다. */
  std::string sub_message;
  /** `[dfr] ready` 뒤 JSON의 값입니다. 버전을 찍지 않는 관류 분석 프로그램이면 빈 문자열입니다. */
  std::string worker_version;
  std::string upstream_commit;
  /** kBusy일 때 분석 중인 요청의 이름입니다. */
  std::string current_name;
  /** kBusy일 때 요청을 보낸 뒤 지난 시간입니다. */
  int64_t elapsed_ms = 0;
  /** 보내지 않고 기다리는 요청 수입니다. 분석 중인 요청은 세지 않습니다. */
  int32_t queued = 0;
  /** SetHold(true) 상태인지입니다. */
  bool held = false;
  /** 끝난 요청 수. 값이 바뀌면 last_done이 새 결과입니다. */
  int64_t finished_count = 0;
  /** 마지막으로 끝난 요청입니다. finished_count가 0이면 빈 값입니다. */
  AnalyzerOutcome last_done;
  /** 받은 `[dfr] presegmented` 줄 수(발 하나에 한 줄)입니다. 다시 띄워도 이어서 셉니다. */
  int64_t presegmented = 0;
  /** 마지막 `[dfr] presegmented` 줄의 JSON입니다. */
  std::string last_presegment;
  /** 받은 `[dfr] figures` 줄 수입니다. 값이 바뀌면 마지막으로 끝난 요청(last_done)의 그림을 다 그렸습니다. */
  int64_t figures_count = 0;
  /** 마지막 `[dfr] figures` 줄의 JSON입니다. */
  std::string last_figures;
};

/** 관류 분석 프로그램을 다루는 시간 값입니다. 시험에서 짧게 줍니다. */
struct AnalyzerTiming {
  /**
   * 진행 줄이 이 시간 동안 없으면 관류 분석 프로그램을 강제 종료하고 다시 띄웁니다. 준비 줄과 미리 올리기 응답에도 씁니다.
   *
   * @note 실험 노트북에서 한 건 전체가 약 45초이고 단계 사이 간격은 그보다 짧습니다(2026-10-14 현장 PC 실측).
   */
  int64_t stage_timeout_ms = 180'000;
  /** Stop에서 입력을 닫은 뒤 스스로 끝나기를 기다리는 시간입니다. 넘으면 강제 종료합니다. */
  int64_t stop_wait_ms = 3'000;
};

/**
 * 관류 분석 프로그램(dfr_worker.py) 프로세스 하나와 요청 대기열입니다.
 *
 * 프로세스를 띄워 둔 채 요청을 먼저 온 순서대로 한 건씩 보냅니다. 내부 스레드 하나가 출력 줄을 읽어 상태를
 * 바꾸고, 호출자는 Status를 주기적으로 읽습니다. 요청 한 건이 끝나는 동안 나온 출력은 모두 그 요청의
 * `<output_dir>/analyzer_log.txt`에 덧붙입니다(관류 분석 프로그램의 예외 기록을 남기기 위함).
 *
 * - 준비 줄(`[dfr] ready`)을 받으면, 보류 중이 아닐 때 모델 미리 올리기 요청을 보내고 응답을 받은 뒤 요청을 보냅니다
 * - 분석 중 진행 줄이 AnalyzerTiming::stage_timeout_ms 동안 없으면 강제 종료하고 그 요청을 실패로 두고 다시 띄웁니다
 * - 분석 중이나 대기 중에 프로세스가 끝나면 그 요청을 실패로 두고 다시 띄웁니다. 준비 전에 끝나면 kFailed가 됩니다
 * - 성공은 완료 줄의 ok와 요약 JSON 파일이 함께 있을 때입니다
 *
 * @note 모든 함수는 여러 스레드에서 불러도 됩니다.
 */
class Analyzer {
 public:
  /**
   * 관류 분석 프로그램을 띄웁니다.
   *
   * @param launch 관류 분석 프로그램 실행 정보(AnalyzerLaunch의 결과)
   * @param timing 시간 값
   * @param error 실패하면 이유를 씁니다. nullptr이면 쓰지 않습니다
   * @return 관류 분석 프로그램. 프로세스를 띄우지 못하면 nullptr
   */
  static std::unique_ptr<Analyzer> Start(const ProcessLaunch& launch, const AnalyzerTiming& timing,
                                         std::string* error);
  static std::unique_ptr<Analyzer> Start(const ProcessLaunch& launch, std::string* error) {
    return Start(launch, AnalyzerTiming{}, error);
  }

  /** Stop을 부르고 내부 스레드를 거둡니다. */
  ~Analyzer();
  Analyzer(const Analyzer&) = delete;
  Analyzer& operator=(const Analyzer&) = delete;

  /**
   * 요청을 대기열 끝에 넣습니다.
   *
   * @param request 분석 요청. output_dir와 name이 있어야 합니다
   * @param error 받지 않으면 이유를 씁니다. nullptr이면 쓰지 않습니다
   * @return 받았으면 true. 요청 줄을 만들 수 없거나 Stop 뒤이면 false
   * @note kFailed 상태이면 관류 분석 프로그램을 다시 띄웁니다.
   */
  bool Submit(const PerfusionRequest& request, std::string* error = nullptr);

  /**
   * 분석 중인 요청을 취소합니다. 관류 분석 프로그램을 강제 종료하고 다시 띄운 뒤 모델을 미리 올립니다.
   *
   * @return 분석 중인 요청이 있었으면 true
   * @note 대기열의 다른 요청은 그대로 두고, 다시 띄운 관류 분석 프로그램이 이어서 받습니다.
   */
  bool Cancel();

  /**
   * 새 요청과 모델 미리 올리기를 보내지 않게 하거나 다시 보내게 합니다.
   *
   * @param hold true이면 보내지 않습니다. 이미 보낸 요청은 계속 분석합니다
   * @note 안정화와 녹화 중에 켭니다. 관류 분석 프로그램 시작(모델 약 400MB 읽기)이 영상 쓰기와 디스크를 나눠 쓰지 않게 하기 위함입니다.
   */
  void SetHold(bool hold);

  /**
   * 녹화 중인 발 영상의 첫 장을 미리 분할하게 합니다. 보류(SetHold)와 대기열을 거치지 않고 바로 보냅니다.
   *
   * @param left_video 왼발 영상 경로(UTF-8)
   * @param right_video 오른발 영상 경로(UTF-8)
   * @param error 보내지 않으면 이유를 씁니다. nullptr이면 쓰지 않습니다
   * @return 보냈으면 true. 관류 분석 프로그램이 준비 전(모델 미리 올리기 전)이거나 실패, 멈춤 상태이면 false
   * @note 보류의 유일한 예외입니다. 녹화 중에 보내므로, 모델 파일을 읽지 않도록 미리 올리기가 끝난 뒤에만 보냅니다.
   *       관류 분석 프로그램은 낮은 우선순위로 돌고, 분할 스레드 수는 `--presegment-threads`로 정합니다.
   *       분석 중(kBusy)이면 관류 분석 프로그램이 그 분석을 마친 뒤 읽습니다.
   */
  bool Presegment(const std::string& left_video, const std::string& right_video, std::string* error = nullptr);

  /** kFailed 상태의 관류 분석 프로그램을 다시 띄웁니다. 다른 상태이면 아무것도 하지 않습니다. */
  void Restart();

  /** 입력을 닫아 관류 분석 프로그램을 끝냅니다. 분석 중인 요청은 취소로 끝나고 대기열은 비웁니다. */
  void Stop();

  /** 지금 상태입니다. */
  AnalyzerStatus Status() const;

  struct Impl;

 private:
  explicit Analyzer(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

/**
 * 배포 폴더에서 관류 분석 프로그램을 띄우는 방법을 정합니다. 파일이 있는지는 보지 않습니다.
 *
 * 기본은 `<app_dir>/python/python.exe`(Windows가 아니면 `<app_dir>/python/bin/python3`)로
 * `<app_dir>/analysis/dfr_worker.py --idle-exit 0`을 낮은 우선순위로 띄웁니다. 환경 변수
 * PYTHONUNBUFFERED=1, PYTHONUTF8=1, PYTHONIOENCODING=utf-8을 더하고, 작업 폴더는 스크립트가 있는 폴더입니다.
 *
 * @param app_dir 앱 실행 파일이 있는 폴더(UTF-8)
 * @param python_override 비어 있지 않으면 이 파이썬을 씁니다(개발용 명령줄 인자)
 * @param script_override 비어 있지 않으면 이 스크립트를 씁니다(개발용 명령줄 인자)
 * @return 실행 정보. 경로 구분자는 '/'입니다
 * @note 배포 폴더의 파이썬이 없다고 시스템 PATH의 파이썬으로 물러나지 않습니다. 패키지 판이 다른 파이썬으로 분석하면 결과가 달라질 수 있습니다.
 *       개발용 인자의 이름만 준 파이썬을 PATH에서 찾는 일은 ResolveAnalyzerOverride가 합니다.
 */
ProcessLaunch AnalyzerLaunch(const std::string& app_dir, const std::string& python_override,
                             const std::string& script_override);

/** 개발용 인자 `--analyzer-python`, `--analyzer-script`를 풀어 쓴 값입니다. 비어 있으면 배포 폴더 기본값을 씁니다. */
struct AnalyzerOverrides {
  std::string python;
  std::string script;
};

/**
 * 개발용 인자 `--analyzer-python`, `--analyzer-script` 값을 앱이 시작한 폴더 기준으로 풀어 줍니다.
 *
 * 관류 분석 프로그램은 스크립트가 있는 폴더를 작업 폴더로 띄우므로, 상대 경로는 그 폴더 기준으로 읽혀 어긋납니다.
 * 그래서 시작할 때 한 번 절대 경로로 바꿉니다. 비어 있는 값은 그대로 비워 둡니다(배포 폴더 기본값을 씁니다).
 *
 * - 스크립트: 상대 경로이면 `cwd`를 앞에 붙입니다.
 * - 파이썬: 경로 구분자가 없는 이름(`python3`, `python`)은 `path_env`의 폴더를 앞에서부터 찾아 처음 나온 실행 파일의
 *   절대 경로로 바꿉니다. 구분자가 있는 상대 경로는 `cwd`를 앞에 붙입니다. 이름을 찾지 못하면 이름 그대로 두므로
 *   AnalyzerFilesProblem이 그 이름을 넣어 "파이썬 없음"으로 알립니다.
 *
 * @param python `--analyzer-python` 값(UTF-8). 비어 있을 수 있습니다
 * @param script `--analyzer-script` 값(UTF-8). 비어 있을 수 있습니다
 * @param cwd 앱이 시작한 작업 폴더(UTF-8)
 * @param path_env 환경 변수 PATH 값(UTF-8). Windows는 ';', 그 밖에는 ':'로 나뉩니다
 * @return 풀어 쓴 파이썬과 스크립트. 경로 구분자는 '/'입니다
 */
AnalyzerOverrides ResolveAnalyzerOverrides(const std::string& python, const std::string& script,
                                           const std::string& cwd, const std::string& path_env);

/** 관류 분석 프로그램의 분석 판 하나입니다. `<analysis>/variants/<판>/VARIANT.json`에서 읽습니다. */
struct AnalyzerVariant {
  /** 릴리즈 자산 하나입니다. */
  struct Asset {
    /** 릴리즈 자산 이름(`name`). */
    std::string name;
    /** 판 폴더 안 파일의 SHA-256(`sha256`, 소문자 16진수). */
    std::string sha256;
    /** 릴리즈에 올라온 날(`uploaded`, YYYY-MM-DD). */
    std::string uploaded;
  };
  /** 판 이름(`id`, 폴더 이름과 같음). 관류 분석 프로그램 인자 `--variant`와 결과 폴더 이름(PerfusionOutputDir)에 씁니다. */
  std::string id;
  /** 화면에 보이는 이름(`label`). */
  std::string label;
  /** 한 줄 설명(`summary`). 예: "규칙 판정". */
  std::string summary;
  /** 소스 저장소 커밋(`upstream.commit`)과 그 날짜(`upstream.date`, YYYY-MM-DD). */
  std::string upstream_commit;
  std::string upstream_date;
  /** 모델 파일을 올린 릴리즈 태그(`release.tag`). */
  std::string release_tag;
  /** 커프 단계 판정 파일(`release.classifier`)입니다. */
  Asset classifier;
  /** 맥동 템플릿(`release.template`)입니다. */
  Asset pulse_template;
};

/**
 * 관류 분석 프로그램 폴더의 분석 판을 찾습니다.
 *
 * @param analysis_dir `dfr_worker.py`가 있는 폴더(UTF-8)
 * @param error 읽지 못한 판이 있거나 판이 하나도 없으면 이유를 씁니다. nullptr이면 쓰지 않습니다
 * @return `variants/` 아래 `VARIANT.json`에 `id`가 있고 폴더 이름과 같은 판. 판 이름 순입니다
 */
std::vector<AnalyzerVariant> FindAnalyzerVariants(const std::string& analysis_dir, std::string* error = nullptr);

/**
 * 판 하나를 올리는 관류 분석 프로그램 실행 정보입니다.
 *
 * @param launch AnalyzerLaunch의 결과
 * @param variant_id 판 이름(AnalyzerVariant::id)
 * @return launch의 인자 끝에 `--variant <판>`을 더한 것
 */
ProcessLaunch AnalyzerVariantLaunch(ProcessLaunch launch, const std::string& variant_id);

/**
 * 관류 분석 프로그램 실행 파일과 스크립트가 있는지 확인합니다.
 *
 * @param launch AnalyzerLaunch의 결과
 * @return 문제가 없으면 nullopt. 파이썬이나 스크립트가 없거나 크기가 0이면 그 이유(UTF-8, 경로 포함)
 */
std::optional<std::string> AnalyzerFilesProblem(const ProcessLaunch& launch);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_ANALYZER_H_
