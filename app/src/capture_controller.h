#ifndef HWLAB_APP_CAPTURE_CONTROLLER_H_
#define HWLAB_APP_CAPTURE_CONTROLLER_H_

#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include "hwlab/flow/capture_spec.h"
#include "hwlab/pixel/foot_warmth.h"
#include "hwlab/platform/capture.h"
#include "hwlab/platform/rgb_camera.h"
#include "hwlab/platform/shot_folder.h"
#include "hwlab/platform/thermal_camera.h"
#include "hwlab/record/analysis_inputs.h"
#include "hwlab/record/shot.h"
#include "foot_regions.h"
#include "light_controller.h"
#include "ppg_controller.h"
#include "readback_model.h"
#include "rgb_trace.h"
#include "shot_form.h"
#include "thermal_view.h"

/**
 * 화면의 ViewModel입니다.
 *
 * Capture 상태를 0.2초마다 읽어 바뀐 속성만 알리고, 사용자 동작을 Capture에 넘깁니다.
 * 화면(QML)은 이 값을 그리기만 하고 촬영 판단은 하지 않습니다.
 */
class CaptureController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("main에서 카메라와 촬영 선언으로 만듭니다")

  Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged)
  Q_PROPERTY(bool recording READ recording NOTIFY phaseChanged)
  Q_PROPERTY(bool settling READ settling NOTIFY phaseChanged)
  Q_PROPERTY(bool canEdit READ canEdit NOTIFY phaseChanged)
  Q_PROPERTY(int settleRemaining READ settleRemaining NOTIFY settleRemainingChanged)
  Q_PROPERTY(int written READ written NOTIFY countsChanged)
  Q_PROPERTY(int missing READ missing NOTIFY countsChanged)
  Q_PROPERTY(int dropped READ dropped NOTIFY countsChanged)
  Q_PROPERTY(int target READ target CONSTANT)
  Q_PROPERTY(double measuredFps READ measuredFps NOTIFY measuredFpsChanged)
  Q_PROPERTY(double targetFps READ targetFps CONSTANT)
  Q_PROPERTY(QString lastResult READ lastResult NOTIFY lastResultChanged)
  /** 마지막 촬영에서 fps가 떨어진 구간 문장입니다(platform::DescribeSlowFrames). 없으면 빈 문자열이고 경고 색으로 보입니다. */
  Q_PROPERTY(QString lastResultWarning READ lastResultWarning NOTIFY lastResultChanged)
  Q_PROPERTY(QString error READ error NOTIFY errorChanged)
  Q_PROPERTY(QString deviceName READ deviceName CONSTANT)
  /** RGB 카메라를 찾았지만 촬영 선언의 형식으로 열지 못한 이유입니다(RgbCamera::OpenProblem). 없으면 빈 문자열입니다. */
  Q_PROPERTY(QString cameraProblem READ cameraProblem NOTIFY cameraProblemChanged)
  Q_PROPERTY(ReadbackModel* readback READ readback CONSTANT)
  Q_PROPERTY(bool autoMode READ autoMode NOTIFY settingsChanged)
  Q_PROPERTY(int exposureStep READ exposureStep NOTIFY settingsChanged)
  Q_PROPERTY(int gain READ gain NOTIFY settingsChanged)
  Q_PROPERTY(int whiteBalance READ whiteBalance NOTIFY settingsChanged)
  /**
   * 화면에서 적용한 카메라 설정을 다음 실행의 기본값으로 쓸지입니다. 켜 두면 적용할 때마다 남기고, 다음 실행은 촬영 선언 파일
   * 대신 남긴 값으로 시작합니다. 앱이 저장 위치를 기억하지 않는 실행(--fake 등)에서는 남기지 않습니다.
   */
  Q_PROPERTY(bool rememberSettings READ rememberSettings WRITE setRememberSettings NOTIFY settingsChanged)
  /** 이번 실행이 촬영 선언 파일 대신 기억한 설정으로 시작했으면 true입니다. */
  Q_PROPERTY(bool startedWithRememberedSettings READ startedWithRememberedSettings CONSTANT)
  Q_PROPERTY(int previewRevision READ previewRevision NOTIFY previewChanged)
  Q_PROPERTY(ShotForm* shot READ shot CONSTANT)
  Q_PROPERTY(bool canShoot READ canShoot NOTIFY canShootChanged)
  Q_PROPERTY(ThermalView* thermal READ thermal CONSTANT)
  /** 미리보기 옆 RGB 패널의 두 발 상자와 관찰 영역 기록입니다. */
  Q_PROPERTY(RgbTrace* rgb READ rgb CONSTANT)
  Q_PROPERTY(FootRegions* rgbRegions READ rgbRegions CONSTANT)
  Q_PROPERTY(FootRegions* thermalRegions READ thermalRegions CONSTANT)
  Q_PROPERTY(QString leftBoxFoot READ leftBoxFoot NOTIFY footChanged)
  Q_PROPERTY(QString rightBoxFoot READ rightBoxFoot NOTIFY footChanged)
  Q_PROPERTY(QString savedVideosText READ savedVideosText CONSTANT)
  Q_PROPERTY(QString lightingText READ lightingText CONSTANT)
  Q_PROPERTY(QString thermalPlanText READ thermalPlanText NOTIFY canShootChanged)
  Q_PROPERTY(QString analysisText READ analysisText NOTIFY analysisChanged)
  Q_PROPERTY(QString analysisState READ analysisState NOTIFY analysisChanged)
  Q_PROPERTY(QString thermalPrompt READ thermalPrompt NOTIFY thermalPromptChanged)
  /**
   * 미리보기 열화상으로 본 두 발의 상태입니다. "checking"(판정에 쓸 장이 모자람), "warm", "cold", 열화상을 녹화하지 않거나
   * 발 상자가 준비되지 않았으면 빈 문자열입니다(pixel::FootWarmthCheck).
   */
  Q_PROPERTY(QString footWarmthState READ footWarmthState NOTIFY footWarmthChanged)
  /** 발 상태 문장입니다. 예: "왼발이 차갑습니다 (발과 바닥의 열화상 차이 38, 기준 60)". 따뜻하면 빈 문자열입니다. */
  Q_PROPERTY(QString footWarmthText READ footWarmthText NOTIFY footWarmthChanged)
  /** 녹화 버튼을 눌렀을 때 발이 차가워 진행할지 묻는 안내의 제목입니다. 닫혀 있으면 빈 문자열입니다. */
  Q_PROPERTY(QString coldFootPrompt READ coldFootPrompt NOTIFY coldFootPromptChanged)
  /**
   * RGB 카메라 설정이 커프 단계 기준값을 맞춘 설정(수동, 게인 1, 노출 -5 또는 -9)과 다르면 그 문장입니다. 같으면 빈 문자열입니다.
   *
   * @note 관류 분석 프로그램의 측정 품질 검사(qc camera_setting)와 같은 기준입니다. 녹화를 막지는 않습니다.
   */
  Q_PROPERTY(QString cameraSettingWarning READ cameraSettingWarning NOTIFY settingsChanged)
  /**
   * 렌즈 셰이딩 보정 안내 종류입니다. ""(닫힘), "missing"(보정 파일이 없어 보정을 권함, 열화상을 처음 받을 때 한 번),
   * "unreadable"(보정 파일을 불러오지 못해 다시 보정을 권함, missing과 같이 한 번), "confirm"(보정 버튼을 눌러 렌즈를
   * 가렸는지 물음)입니다.
   */
  Q_PROPERTY(QString lensShadingPrompt READ lensShadingPrompt NOTIFY lensShadingPromptChanged)
  // 앱 종료 때 카메라 복원(Capture::Stop) 결과입니다. shutdown을 부른 뒤에 정해집니다
  Q_PROPERTY(bool shuttingDown READ shuttingDown NOTIFY shutdownChanged)
  Q_PROPERTY(bool closeReady READ closeReady NOTIFY shutdownChanged)
  Q_PROPERTY(bool savingBeforeClose READ savingBeforeClose NOTIFY shutdownChanged)
  Q_PROPERTY(bool shotInProgress READ shotInProgress NOTIFY canShootChanged)
  Q_PROPERTY(bool savingShot READ savingShot NOTIFY canShootChanged)
  Q_PROPERTY(QString restoreReport READ restoreReport NOTIFY shutdownChanged)
  /**
   * 저장 위치의 남은 공간 상태입니다. ""(충분하거나 읽지 못함), "low"(10 GB 미만이라 촬영 중 모자랄 수 있음, 노란색),
   * "short"(촬영 한 번을 저장하기에 모자라 촬영 버튼을 막음, 빨간색)입니다.
   */
  Q_PROPERTY(QString freeSpaceLevel READ freeSpaceLevel NOTIFY freeSpaceChanged)
  /** freeSpaceLevel이 low나 short일 때 남은 공간과 할 일을 적은 문장입니다. 아니면 빈 문자열입니다. */
  Q_PROPERTY(QString freeSpaceText READ freeSpaceText NOTIFY freeSpaceChanged)
  Q_PROPERTY(LightController* light READ light CONSTANT)
  /** PPG 장치입니다. main이 AttachPpg로 넘기며, 없으면 null입니다. */
  Q_PROPERTY(PpgController* ppg READ ppg CONSTANT)

 public:
  /**
   * @param camera 실행할 카메라
   * @param spec 촬영 선언
   * @param out_dir 처음 저장 위치
   * @param remembered_values 저장 위치, 좌우 영역, 화소 왼쪽 발, 카메라 설정을 기억합니다. 이 객체보다 오래 있어야 합니다
   * @param thermal 함께 녹화할 열화상 카메라(열지 않은 상태). 없으면 nullptr
   * @param thermal_absent_reason thermal이 nullptr일 때 열화상 자리에 보일 문장
   * @param thermal_absence thermal이 nullptr인 이유(끔, 장치 없음). 매니페스트에 남깁니다
   * @param videos 저장하는 RGB 영상. 기본은 좌우 발 영상만입니다
   * @param lighting 매니페스트에 남길 조명 정보
   */
  CaptureController(std::unique_ptr<hwlab::platform::RgbCamera> camera, hwlab::flow::CaptureSpec spec,
                    QString out_dir, RememberedValues* remembered_values,
                    std::unique_ptr<hwlab::platform::ThermalCamera> thermal, QString thermal_absent_reason,
                    hwlab::record::ThermalAbsence thermal_absence, hwlab::platform::SavedVideos videos,
                    hwlab::record::LightingInfo lighting);
  ~CaptureController() override;

  /**
   * 촬영 파이프라인을 시작하고 0.2초마다 상태를 읽기 시작합니다.
   *
   * @return 시작했으면 true. 실패하면 error에 이유
   */
  bool Start(QString* error);

  /**
   * 촬영 선언 파일의 이름과 SHA-256을 정합니다. 매니페스트의 spec에 남깁니다.
   *
   * @param file 파일 이름(경로 없이)
   * @param sha256 파일 내용의 SHA-256(소문자 16진수)
   */
  void SetSpecSource(const QString& file, const QString& sha256);

  /** 마지막으로 받은 미리보기 영상입니다. 화면 스레드에서 부릅니다. */
  QImage LatestPreview();

  QString phase() const;
  bool recording() const { return phase_ == hwlab::flow::Phase::kRecording; }
  bool settling() const { return phase_ == hwlab::flow::Phase::kSettling; }
  bool canEdit() const { return phase_ == hwlab::flow::Phase::kPreview; }
  int settleRemaining() const { return settle_remaining_; }
  int written() const { return static_cast<int>(counts_.written); }
  int missing() const { return static_cast<int>(counts_.missing); }
  int dropped() const { return static_cast<int>(counts_.dropped); }
  int target() const { return static_cast<int>(spec_.target_frames); }
  double measuredFps() const { return measured_fps_; }
  double targetFps() const { return spec_.settings.sdk_frame_rate; }
  QString lastResult() const { return last_result_; }
  QString lastResultWarning() const { return last_result_warning_; }
  QString error() const { return error_; }
  QString deviceName() const { return QString::fromStdString(camera_->DeviceName()); }
  QString cameraProblem() const { return camera_problem_; }
  ReadbackModel* readback() { return &readback_; }
  bool autoMode() const { return settings_.mode == hwlab::SettingMode::kAuto; }
  int exposureStep() const { return settings_.exposure_step; }
  int gain() const { return settings_.gain; }
  int whiteBalance() const { return settings_.white_balance_k; }
  bool rememberSettings() const { return remember_settings_; }
  void setRememberSettings(bool on);
  bool startedWithRememberedSettings() const { return started_with_remembered_; }
  int previewRevision() const { return preview_revision_; }
  ShotForm* shot() { return &shot_; }
  bool canShoot() const;
  ThermalView* thermal() { return &thermal_view_; }
  RgbTrace* rgb() { return &rgb_trace_; }
  FootRegions* rgbRegions() { return &rgb_regions_; }
  FootRegions* thermalRegions() { return &thermal_regions_; }
  QString leftBoxFoot() const;
  QString rightBoxFoot() const;
  QString savedVideosText() const;
  QString lightingText() const;
  QString thermalPlanText() const;
  QString analysisText() const { return analysis_text_; }
  QString analysisState() const { return analysis_state_; }
  QString thermalPrompt() const { return thermal_prompt_; }
  QString footWarmthState() const;
  QString footWarmthText() const;
  QString coldFootPrompt() const { return cold_foot_prompt_; }
  QString cameraSettingWarning() const;
  QString lensShadingPrompt() const { return lens_shading_prompt_; }
  bool shuttingDown() const { return shutting_down_; }
  bool closeReady() const { return close_ready_; }
  bool savingBeforeClose() const { return saving_before_close_; }
  // 녹화를 준비하거나 쓰는 중. 창을 닫으면 녹화를 멈추고 지금까지 찍은 것을 저장한 뒤 닫으므로 화면이 먼저 묻습니다
  bool shotInProgress() const { return pending_.has_value(); }
  // 녹화는 끝났지만 쓰기 대기열을 파일에 다 쓰지 않은 동안입니다. 디스크 쓰기가 멈추면 수십 초까지 걸립니다(2026-11-02 실측 4~27초)
  bool savingShot() const { return saving_shot_; }
  QString restoreReport() const { return restore_report_; }
  QString freeSpaceLevel() const;
  QString freeSpaceText() const { return QString::fromStdString(space_.text); }

  /**
   * 앱을 닫기 전에 촬영 파이프라인을 멈추고 카메라를 실행 전 상태로 되돌립니다. 화면 스레드 밖에서 실행합니다.
   *
   * 끝나면 closeReady가 true가 되고 shutdownFinished를 보냅니다. 실패한 명령이나 실행 전과 다른 항목이 있으면
   * restoreReport에 사람이 읽을 문장을 둡니다. 실행 전 값을 몰라 되돌리지 않은 항목을 포함해 결과는 로그 파일에 남깁니다.
   *
   * @note 같은 카메라를 다른 사람도 쓰므로 복원이 맞지 않으면 화면에서 알려야 합니다.
   */
  Q_INVOKABLE void shutdown();

  /**
   * 디스크 여유를 읽지 않고 이 값으로 정합니다. 명령줄 시험 옵션(--fake-free-bytes)에만 씁니다.
   *
   * @param bytes 남은 바이트로 볼 값
   */
  void SetFreeBytesOverride(int64_t bytes);

  /**
   * 저장 위치의 남은 공간을 다시 읽어 freeSpaceLevel과 freeSpaceText를 정합니다. 시작할 때, 저장 위치를 바꿀 때, 촬영이 끝날 때,
   * 촬영하지 않는 동안 10초마다 부릅니다. 화면 스레드에서 부릅니다.
   */
  void RefreshFreeSpace();

  /**
   * LED 조명 화면을 연결합니다. Start 전에 부릅니다. 연결하면 Capture에 조명을 넘기고, 매니페스트 lighting과 사이드카 light를
   * 이 화면의 상태로 정합니다. 연결하지 않으면 생성자의 lighting으로 기록합니다.
   *
   * @param light 이 객체보다 오래 있어야 합니다
   */
  void AttachLight(LightController* light) { light_ = light; }
  LightController* light() { return light_; }
  /** 촬영 버튼과 저장에 맞춰 PPG 기록을 시작하고 끝낼 장치를 정합니다. Start 전에 부릅니다. */
  void AttachPpg(PpgController* ppg) { ppg_ = ppg; }
  PpgController* ppg() { return ppg_; }

  /** 화소 왼쪽에 찍히는 환자의 발을 정합니다. 명령줄 시험 옵션에 쓰며 다음 실행에 남기지 않습니다. */
  void SetPixelLeftFoot(hwlab::record::FootSide foot);

  /** 화면 왼쪽 상자와 오른쪽 상자의 발을 서로 바꿉니다. 상자의 발 이름을 누르면 부릅니다. */
  Q_INVOKABLE void toggleFoot();

  /**
   * 촬영 버튼입니다. 환자 정보로 촬영 폴더를 새로 만들고 매니페스트를 쓴 뒤 영상과 사이드카 녹화를 시작합니다.
   *
   * RGB는 화면의 좌우 영역으로 잘라 왼발 영상과 오른발 영상으로 쓰고, 전체 화면 영상은 선택했을 때만 씁니다.
   * 열화상을 녹화하면(PlanThermal) 열화상 원시값, 열화상 사이드카, 확인용 AVI도 함께 녹화하고, 녹화가 끝나면
   * 열화상 좌우 영역으로 좌우 열화상 .npy를 화면 스레드 밖에서 쓴 뒤 분석 입력을 확인합니다(record::CheckAnalysisInputs).
   *
   * @note 입력이 검증을 통과하지 않거나 같은 폴더가 99번째까지 모두 있으면 시작하지 않고 error에 이유를 남깁니다.
   *       영상을 하나도 쓰지 못하고 끝나면(안정화 중 중지, fps 불일치) 만든 매니페스트와 빈 폴더를 지웁니다.
   *       열화상이 수신 중이 아니면(장치 없음, 끊김, 첫 프레임 대기) 녹화하지 않고 thermalPrompt에 안내 종류를 둡니다.
   *       사용자가 startWithoutThermal로 진행하면 RGB만 녹화하고 결과 문장과 매니페스트에 남깁니다.
   *       실행할 때 열화상을 끈 경우는 묻지 않고 RGB만 녹화합니다.
   */
  Q_INVOKABLE void startRecording();

  /** 열화상 없이 찍을지 묻는 안내에서 진행을 골랐습니다. 매니페스트에 사용자 확인을 남기고 촬영을 시작합니다. */
  Q_INVOKABLE void startWithoutThermal();

  /** 열화상 없이 찍을지 묻는 안내를 닫습니다. 녹화하지 않습니다. */
  Q_INVOKABLE void cancelThermalPrompt();

  /** 발이 차가워 진행할지 묻는 안내에서 진행을 골랐습니다. 매니페스트 foot_warmth에 사용자 확인을 남기고 촬영을 시작합니다. */
  Q_INVOKABLE void startWithColdFoot();

  /** 발이 차가워 진행할지 묻는 안내를 닫습니다. 녹화하지 않습니다. */
  Q_INVOKABLE void cancelColdFootPrompt();

  /** 중지 버튼입니다. */
  Q_INVOKABLE void stopRecording();

  /** 열화상 칸의 렌즈 보정 버튼입니다. 렌즈를 가렸는지 묻는 안내(lensShadingPrompt "confirm")를 엽니다. */
  Q_INVOKABLE void askLensShading();

  /**
   * 렌즈 셰이딩 보정 안내에서 렌즈를 가렸다고 골랐습니다. 안내를 닫고 보정과 저장을 요청합니다(Capture::CorrectThermalLensShading).
   *
   * @note 미리보기가 아니면 요청하지 않습니다. 결과는 thermal의 lensShading에 보입니다.
   */
  Q_INVOKABLE void correctLensShading();

  /** 렌즈 셰이딩 보정 안내를 닫습니다. 보정하지 않습니다. */
  Q_INVOKABLE void cancelLensShadingPrompt();

  /**
   * 화면에서 고친 설정을 카메라에 적용합니다('적용' 버튼).
   *
   * @return 받아들였으면 true. 미리보기가 아니면 false
   */
  Q_INVOKABLE bool applySettings(bool auto_mode, int exposure_step, int gain, int white_balance);

 signals:
  void phaseChanged();
  void settleRemainingChanged();
  void countsChanged();
  void measuredFpsChanged();
  void cameraProblemChanged();
  void lastResultChanged();
  void errorChanged();
  void freeSpaceChanged();
  void settingsChanged();
  void previewChanged();
  void canShootChanged();
  void footChanged();
  void analysisChanged();
  void thermalPromptChanged();
  void footWarmthChanged();
  void coldFootPromptChanged();
  void lensShadingPromptChanged();
  void shutdownChanged();
  void shutdownFinished();
  /**
   * 좌우 발 영상과 사이드카가 저장되어 RGB 분석 입력을 확인했습니다. 녹화가 끝나면 PPG 파일을 쓴 뒤, 열화상 배열을 쓰기 전에
   * 한 번 내고, 그 뒤에 shotSaved를 냅니다. 분석은 RGB 영상과 PPG 파일만 쓰므로 이때 시작합니다(AnalysisController::onRgbSaved).
   *
   * @note 관류 분석 프로그램은 PPG 파일을 맥박 기준 후보로 읽습니다. PPG를 연 촬영이면 이 신호 전에 PPG 파일이 있습니다.
   *
   * @param folder 촬영 폴더(전체 경로)
   * @param ready 분석을 막는 문제가 없으면 true. 막는 문제는 모두 RGB 쪽이라 shotSaved의 ready와 같습니다
   */
  void rgbSaved(const QString& folder, bool ready);
  /**
   * 촬영 폴더를 저장하고 분석 입력을 확인했습니다. 녹화가 끝나고 좌우 열화상 배열을 쓴 뒤 한 번 냅니다.
   *
   * @param folder 촬영 폴더(전체 경로)
   * @param ready 분석 입력 확인 결과가 ready나 warning이면 true, blocked이면 false
   */
  void shotSaved(const QString& folder, bool ready);
  /**
   * 녹화를 시작해 첫 장을 좌우 발 영상 파일에 썼습니다. 녹화마다 한 번 냅니다.
   *
   * @param left_video 왼발 영상 경로(UTF-8, 피험자 기준)
   * @param right_video 오른발 영상 경로(UTF-8, 피험자 기준)
   * @note 좌우 발 영상을 저장할 때만 냅니다. 녹화 중 첫 장 미리 분할(AnalysisController::onFirstFramesWritten)이 씁니다.
   */
  void firstFramesWritten(const QString& left_video, const QString& right_video);

 private:
  struct Counts {
    int64_t written = 0;
    int64_t missing = 0;
    int64_t dropped = 0;
    bool operator==(const Counts&) const = default;
  };

  // 녹화를 시작한 촬영 폴더입니다. 끝난 뒤 영상이 없으면 지우고, 있으면 결과에 폴더를 적습니다.
  struct PendingShot {
    hwlab::platform::PreparedShot prepared;
    bool thermal = false;      // 이번 녹화에 열화상을 넣었는지
    bool seen_active = false;  // 안정화나 녹화 상태를 한 번이라도 읽었는지
    // 촬영 버튼을 누른 시점의 열화상 좌우 영역. 비어 있으면 가운데 세로선에서 나눕니다
    std::optional<std::pair<hwlab::pixel::Rect, hwlab::pixel::Rect>> thermal_regions;
    hwlab::record::ThermalAbsence thermal_absence = hwlab::record::ThermalAbsence::kNone;  // 열화상을 넣지 않은 이유
    bool first_frames_sent = false;  // 이번 녹화에서 firstFramesWritten을 냈는지
    // PPG 기록을 파일로 쓰고 촬영 결과 줄을 돌려주는 함수(PpgController::EndShot). 마무리 작업 스레드에서 부릅니다
    std::function<PpgSaveResult()> save_ppg;
  };

  void Poll();
  void StopDevices();
  void FinishShot(bool finished);
  void FinishInBackground(const PendingShot& shot, bool save_failed);
  void Begin(bool skip_confirmed, bool cold_confirmed = false);
  void SetColdFootPrompt(const QString& title);
  // 발 대비를 잴 열화상 상자를 ThermalView에 넘깁니다. 상자가 바뀌면 쌓은 판정을 지웁니다.
  void UpdateWarmthRegions();
  // 촬영 폴더 구조에서 같은 날, 같은 환자, 같은 발목 혈압 단계로 이미 찍은 촬영 수를 셉니다.
  int32_t EarlierShotsToday(const hwlab::record::ShotInfo& info) const;
  void SetThermalPrompt(const QString& kind);
  void SetLensShadingPrompt(const QString& kind);
  void UpdateFootNames();
  void SetAnalysis(const QString& text, const QString& state);
  void AppendResult(const QString& line);
  void SetAppError(const QString& message);
  // 시험 옵션으로 정한 값이 있으면 그 값, 없으면 folder가 있는 디스크의 남은 바이트입니다.
  std::optional<int64_t> FreeBytesAt(const std::string& folder) const;
  // 촬영을 끝까지 저장하지 못했음을 오류 줄에 남깁니다. 남은 공간을 지금 다시 읽어 공간 부족이면 원인으로 적습니다.
  // 돌려주는 문장은 촬영 결과 줄에 붙일 문장입니다.
  QString ReportSaveFailure(const std::string& folder, const QString& detail);
  std::vector<ReadbackModel::Row> ReadbackRows(const hwlab::platform::CaptureStatus& status) const;

  // 값이 바뀌었을 때만 바꾸고 알립니다.
  template <typename T>
  void Assign(T& field, const T& value, void (CaptureController::*signal)()) {
    if (field == value) return;
    field = value;
    emit(this->*signal)();
  }

  std::unique_ptr<hwlab::platform::RgbCamera> camera_;
  std::unique_ptr<hwlab::platform::ThermalCamera> thermal_camera_;  // Capture보다 먼저 소멸하면 안 되므로 capture_ 앞에 둡니다
  hwlab::flow::CaptureSpec spec_;
  hwlab::flow::SettingsSpec settings_;
  ShotForm shot_;
  ThermalView thermal_view_;  // Capture의 열화상 수신 스레드가 부르므로 capture_ 앞에 둡니다
  RgbTrace rgb_trace_;        // Capture의 RGB 수신 스레드가 부르므로 capture_ 앞에 둡니다
  std::optional<PendingShot> pending_;
  bool finishing_ = false;  // 좌우 열화상 .npy를 쓰고 분석 입력을 확인하는 중. 끝날 때까지 다음 촬영을 막습니다
  RememberedValues* const remembered_values_;
  const hwlab::record::ThermalAbsence thermal_absence_;
  const hwlab::platform::SavedVideos saved_videos_;
  const hwlab::record::LightingInfo lighting_;
  LightController* light_ = nullptr;  // AttachLight. 없으면 조명을 제어하지 않습니다
  PpgController* ppg_ = nullptr;      // AttachPpg. 없으면 PPG를 기록하지 않습니다
  hwlab::record::FootSide pixel_left_foot_ = hwlab::record::ShotInfo{}.pixel_left_foot;
  FootRegions rgb_regions_;
  FootRegions thermal_regions_;
  QString analysis_text_;
  QString analysis_state_;  // "", "ready", "warning", "blocked"
  QString thermal_prompt_;  // "", "missing"(장치 없음, 끊김), "preparing"(첫 프레임 대기)
  hwlab::pixel::FootWarmthCheck foot_warmth_;  // 화면 스레드. ThermalView::footContrast로 장마다 받습니다
  std::array<int32_t, 2> last_contrast_{};     // 마지막 장의 화소 왼쪽, 오른쪽 상자 대비
  std::optional<std::pair<hwlab::pixel::Rect, hwlab::pixel::Rect>> warmth_rects_;  // ThermalView에 넘긴 상자
  QString cold_foot_prompt_;
  QString lens_shading_prompt_;          // "", "missing", "confirm"
  bool lens_shading_prompt_shown_ = false;  // 보정 파일이 없거나 불러오지 못했다는 안내를 이번 실행에 띄움
  bool shutting_down_ = false;
  bool close_ready_ = false;
  bool saving_before_close_ = false;  // 창을 닫았지만 촬영 마무리(PPG, 열화상 배열 저장)가 끝나기를 기다리는 중
  QString restore_report_;  // 앱 종료 때 복원이 맞지 않은 내용. 비면 문제 없음
  std::optional<int64_t> free_bytes_override_;  // 명령줄 시험 옵션으로 정한 디스크 여유
  QString app_error_;  // 화면이 시작 전에 막은 이유. Capture의 오류가 없을 때 보여 줍니다
  QString save_failure_;  // 이번 촬영을 끝까지 저장하지 못했을 때의 문장. 있으면 오류 줄에 다른 오류보다 먼저 보입니다. 다음 촬영을 시작하면 지웁니다
  hwlab::platform::FreeSpaceNotice space_;  // 저장 위치의 남은 공간 판정과 문장
  QString space_root_;                      // space_을 읽은 저장 위치
  QTimer space_timer_;
  std::unique_ptr<hwlab::platform::Capture> capture_;
  QTimer poll_timer_;
  ReadbackModel readback_;
  hwlab::flow::Phase phase_ = hwlab::flow::Phase::kPreview;
  bool saving_shot_ = false;
  int settle_remaining_ = 0;
  Counts counts_;
  QString last_result_;
  QString last_result_warning_;  // 마지막 촬영의 fps 저하 문장
  QString error_;
  double measured_fps_ = 0.0;
  QString camera_problem_;
  int64_t last_received_ = 0;
  // 마지막으로 읽은 열화상 연결 상태. 촬영 가능 여부(PlanThermal)를 다시 정할 때 씁니다
  hwlab::platform::ThermalConnection thermal_connection_ = hwlab::platform::ThermalConnection::kClosed;
  bool lens_shading_busy_ = false;  // 렌즈 셰이딩 보정 중. 렌즈를 가린 채일 수 있어 촬영 버튼을 막습니다
  qint64 last_poll_ms_ = 0;
  bool was_finished_ = false;
  std::mutex preview_mutex_;
  QImage preview_;
  bool remember_settings_ = false;      // 적용한 설정을 다음 실행에 남김(체크박스)
  bool started_with_remembered_ = false;  // 이번 실행을 기억한 설정으로 시작함
  std::string spec_file_;
  std::string spec_sha256_;
  int preview_revision_ = 0;
  // 마지막 멤버로 두어 소멸할 때 다른 멤버보다 먼저 남은 작업을 기다립니다.
  QThreadPool background_;
};

/** QML Image가 "image://preview/<번호>"로 미리보기 영상을 받아 가는 곳입니다. */
class PreviewProvider : public QQuickImageProvider {
 public:
  explicit PreviewProvider(CaptureController* controller)
      : QQuickImageProvider(QQuickImageProvider::Image), controller_(controller) {}
  QImage requestImage(const QString&, QSize* size, const QSize&) override {
    QImage image = controller_->LatestPreview();
    if (size != nullptr) *size = image.size();
    return image;
  }

 private:
  CaptureController* controller_;
};

#endif  // HWLAB_APP_CAPTURE_CONTROLLER_H_
