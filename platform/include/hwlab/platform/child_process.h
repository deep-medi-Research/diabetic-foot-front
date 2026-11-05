#ifndef HWLAB_PLATFORM_CHILD_PROCESS_H_
#define HWLAB_PLATFORM_CHILD_PROCESS_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hwlab::platform {

/** 자식 프로세스를 띄울 때 넘기는 값입니다. 문자열은 모두 UTF-8입니다. */
struct ProcessLaunch {
  /** 실행 파일 경로. 경로 구분자가 없으면 PATH에서 찾습니다. */
  std::string program;
  /** 프로그램 이름 뒤에 붙는 인자. 공백, 따옴표, 한글을 그대로 넘깁니다. */
  std::vector<std::string> arguments;
  /** 작업 폴더. 비어 있으면 이 프로세스의 작업 폴더를 그대로 씁니다. */
  std::string working_dir;
  /** 상속한 환경 변수에 더하거나 덮어쓸 (이름, 값) 목록입니다. */
  std::vector<std::pair<std::string, std::string>> extra_env;
  /**
   * 낮은 우선순위로 띄울지. Windows는 BELOW_NORMAL_PRIORITY_CLASS, macOS는 nice 값을 kLowPriorityNice만큼 올립니다.
   *
   * @note 분석 중에 다음 녹화를 하면 FFV1 쓰기(실험 노트북에서 CPU 코어 약 3.8개)와 CPU를 나눠 씁니다.
   *       녹화 쪽 스레드가 지연되지 않게 관류 분석 프로그램 우선순위를 낮춥니다.
   */
  bool low_priority = false;
};

/** low_priority일 때 macOS에서 올리는 nice 값입니다. */
inline constexpr int32_t kLowPriorityNice = 10;

/** ReadLine의 결과입니다. */
enum class LineStatus : uint8_t {
  kLine,     // 한 줄을 읽음
  kTimeout,  // 제한 시간 안에 줄이 오지 않음
  kClosed,   // 출력이 닫혔고 남은 줄이 없음
};

/** Kill로 끝난 프로세스의 종료 코드입니다. macOS와 Windows가 같은 값을 냅니다. */
inline constexpr int32_t kKilledExitCode = -9;

/**
 * 자기가 띄운 자식 프로세스 하나와 그 표준 입출력입니다.
 *
 * 표준 출력과 표준 오류는 하나의 스트림으로 합쳐 받습니다. 파이썬은 예외를 표준 오류로 내므로, 따로 받으면
 * 진행 줄과 오류 줄의 순서를 맞출 수 없습니다. 출력은 내부 스레드가 계속 읽어 줄 단위로 쌓으므로,
 * 호출자가 ReadLine을 부르지 않아도 자식이 출력 파이프에서 막히지 않습니다.
 *
 * @note 소멸자는 아직 실행 중인 자식을 Kill하고 거둡니다. 강제 종료는 이 객체가 띄운 프로세스와
 *       그 프로세스가 띄운 하위 프로세스에만 닿습니다(macOS는 프로세스 그룹, Windows는 작업 개체).
 * @note 모든 함수는 여러 스레드에서 불러도 됩니다.
 */
class ChildProcess {
 public:
  /**
   * 자식 프로세스를 띄웁니다.
   *
   * @param launch 프로그램, 인자, 작업 폴더, 환경 변수
   * @param error 실패하면 이유를 씁니다. nullptr이면 쓰지 않습니다
   * @return 띄운 프로세스. 실행 파일이 없거나 실행하지 못하면 nullptr
   */
  static std::unique_ptr<ChildProcess> Start(const ProcessLaunch& launch, std::string* error);

  ~ChildProcess();
  ChildProcess(const ChildProcess&) = delete;
  ChildProcess& operator=(const ChildProcess&) = delete;

  /**
   * 표준 입력에 한 줄을 씁니다. 끝에 '\n'을 붙입니다.
   *
   * @param line 줄 내용(UTF-8). '\n'을 넣지 않습니다
   * @return 다 썼으면 true. 자식이 입력을 닫았거나 끝났으면 false
   * @note 자식이 입력을 읽지 않는 동안 파이프 버퍼(수 KB 이상)를 넘게 쓰면 읽을 때까지 기다립니다.
   */
  bool WriteLine(const std::string& line);

  /** 표준 입력을 닫습니다. 자식은 입력 끝(EOF)을 읽습니다. */
  void CloseInput();

  /**
   * 출력 한 줄을 읽습니다. 줄 끝의 '\n'과 '\r'은 제거합니다.
   *
   * @param line 읽은 줄을 씁니다
   * @param timeout_ms 줄을 기다리는 최대 시간. 0이면 기다리지 않습니다
   * @return kLine, kTimeout, kClosed
   * @note 자식이 끝나며 '\n' 없이 남긴 마지막 조각도 한 줄로 돌려줍니다.
   */
  LineStatus ReadLine(std::string* line, int64_t timeout_ms);

  /**
   * 자식이 끝나기를 기다립니다.
   *
   * @param timeout_ms 최대 대기 시간. 0이면 기다리지 않고 지금 상태만 봅니다
   * @return 종료 코드. 시간 안에 끝나지 않으면 nullopt
   * @note macOS에서 신호로 끝나면 신호 번호에 음수를 붙여 돌려줍니다. Kill로 끝나면 kKilledExitCode입니다.
   */
  std::optional<int32_t> Wait(int64_t timeout_ms);

  /** 자식과 그 하위 프로세스를 강제로 끝냅니다. 이미 끝났으면 아무것도 하지 않습니다. */
  void Kill();

  /** 운영체제의 프로세스 번호입니다. */
  int64_t Id() const;

  struct Impl;

 private:
  explicit ChildProcess(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_CHILD_PROCESS_H_
