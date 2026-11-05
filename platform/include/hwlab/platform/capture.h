#ifndef HWLAB_PLATFORM_CAPTURE_H_
#define HWLAB_PLATFORM_CAPTURE_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/flow/capture_spec.h"
#include "hwlab/flow/flow.h"
#include "hwlab/pixel/brightness.h"
#include "hwlab/pixel/rect.h"
#include "hwlab/platform/light.h"
#include "hwlab/platform/rgb_camera.h"
#include "hwlab/platform/thermal_camera.h"
#include "hwlab/record/sidecar.h"

namespace hwlab::platform {

/** 열화상 스트림 상태의 사본입니다. CaptureStatus::thermal에 들어갑니다. */
struct ThermalStatus {
  bool attached = false;  // Capture에 열화상 카메라를 넘겼는지
  ThermalConnection connection = ThermalConnection::kClosed;
  int64_t received = 0;     // Start 뒤 받은 프레임 수
  int64_t written = 0;      // 이번 녹화에서 원시값 파일에 쓴 프레임 수
  int64_t dropped = 0;      // 이번 녹화의 저장 누락
  int64_t missing = 0;      // 이번 녹화의 열화상 누락(끊김 포함). 열화상 사이드카를 쓴 뒤 채워집니다
  int32_t reconnects = 0;   // Start 뒤 끊김을 판정하고 다시 연 횟수
  double measured_fps = 0;  // 최근 31장 수신 간격 중앙값으로 잰 fps. 두 장을 받기 전과 끊긴 동안은 0
  std::string error;        // 열화상 장치 열기와 파일 쓰기 오류. RGB 녹화는 이 오류로 멈추지 않습니다
  bool write_failed = false;  // 이번 녹화에서 열화상 파일을 열지, 쓰지, 닫지 못했음(디스크 가득 참 포함)
  // 지금 연결의 렌즈 셰이딩 보정 상태(ThermalCamera::LensShading). 열기 전, 끊긴 동안, 보정 기능이 없으면 비웁니다
  std::optional<LensShadingStatus> lens_shading;
  bool lens_shading_busy = false;  // 렌즈 셰이딩 보정 요청을 제어 스레드가 아직 끝내지 않음(Capture::CorrectThermalLensShading)
};

/** LED 조명 상태의 사본입니다. CaptureStatus::light에 들어갑니다. */
struct LightStatus {
  bool attached = false;              // Capture에 조명을 넘겼는지
  bool connected = false;             // 포트가 열려 있고 마지막 쓰기가 성공함(Light::IsConnected)
  std::string port;                   // 연 포트 이름. 연결되어 있지 않으면 빈 문자열
  std::optional<int32_t> percent;     // 조명 밝기(Light::LastSentPercent). 장치에서 읽은 값이 아님
  int32_t pending = 0;                // 제어 스레드가 아직 처리하지 않은 조명 요청 수
  pixel::LightResponse response = pixel::LightResponse::kUnknown;  // 마지막 LED 변경의 반영 판정
  bool judging = false;               // 반영 판정에 쓸 프레임을 모으는 중
  std::string error;                  // 마지막 조명 요청의 오류. 성공하면 지웁니다
};

/**
 * RGB 프레임에서 잘라 따로 녹화할 영역 하나와 그 파일입니다. 좌우 발을 따로 저장할 때 둘을 넘깁니다.
 *
 * @note 화면의 왼쪽과 오른쪽이 환자의 어느 발인지는 촬영 정보가 정하므로 부르는 쪽이 foot에 넣습니다.
 *       관류 분석은 세로로 긴 영상만 받으므로 부르는 쪽이 pixel::IsPortrait로 먼저 확인합니다.
 */
struct CropOutput {
  pixel::Rect rect;     // 프레임 안 영역. UYVY 색차 격자(x와 폭이 짝수)에 맞아야 합니다(pixel::IsAligned)
  std::string video;    // 영상 파일. 확장자는 flow::ExtensionOf(spec.codec)
  std::string sidecar;  // 이 영상의 사이드카. crop에 영역, foot에 발을 남깁니다
  std::optional<record::FootSide> foot;
};

/**
 * 녹화 한 번의 파일 경로입니다(UTF-8).
 *
 * @note video가 비어 있으면 전체 화면 영상과 그 사이드카를 쓰지 않습니다. 전체 화면과 crops 중 하나는 있어야 합니다.
 *       열화상 경로(thermal_raw, thermal_sidecar)가 비어 있거나 Capture에 열화상 카메라가 없으면 열화상은 쓰지 않습니다.
 *       thermal_preview만 비어 있으면 확인용 영상만 쓰지 않습니다.
 *       열화상 카메라가 카메라 온도(ThermalCamera::LastCameraTemperature)를 주면 thermal_raw의 .raw16을 _camera_temp.csv로
 *       바꾼 경로에 카메라 온도 파일도 씁니다.
 */
struct RecordingPaths {
  std::string video;            // 전체 화면 영상 파일. 확장자는 flow::ExtensionOf(spec.codec)
  std::string sidecar;          // 전체 화면 영상의 사이드카
  std::string thermal_raw;      // 열화상 원시값 파일(raw16)
  std::string thermal_sidecar;  // 열화상 사이드카
  std::string thermal_preview;  // 열화상 확인용 흑백 AVI
  std::vector<CropOutput> crops;  // 잘라 따로 쓸 영역. 쓰기 스레드가 프레임마다 잘라 영역마다 영상 파일 하나에 씁니다
  // RGB 사이드카의 light. 출처, 포트, 사람 확인은 부르는 쪽이 채웁니다. 출처가 kApp이고 Capture에 조명이 있으면 percent와
  // response는 녹화를 시작할 때 Capture가 채우고, flicker는 녹화가 끝난 뒤 영상 파일마다 채웁니다
  record::LightRecord light;
  // 촬영 버튼 뒤 PPG 안정 대기 시간. 0이면 기다리지 않습니다. PPG 장치를 다시 시작하는 촬영에 부르는 쪽이 정하고,
  // Capture가 선언의 SDK fps로 RGB 프레임 수로 바꿔 촬영 흐름에 넘깁니다(flow::Event::ppg_settle_frames)
  int64_t ppg_settle_us = 0;
};

/** 촬영 상태의 사본입니다. Capture::Status가 돌려주며, 화면이 0.2초마다 읽습니다. */
struct CaptureStatus {
  flow::Phase phase = flow::Phase::kPreview;
  int64_t received = 0;  // 스트림을 연 뒤 받은 프레임 수
  int64_t written = 0;   // 이번 녹화에서 파일에 쓴 프레임 수
  int64_t dropped = 0;   // 이번 녹화의 저장 누락
  int64_t missing = 0;   // 이번 녹화의 카메라 단 누락. 녹화가 끝난 뒤 채워집니다
  timing::FrameRateCheck frame_rate;  // 이번 녹화의 느린 간격과 1초 창 최저 fps. 녹화가 끝난 뒤 채워집니다
  int32_t frames_until_settled = 0;  // 안정화가 끝날 때까지 남은 프레임 수. 안정화 프레임 수와 PPG 안정 대기 중 큰 값
  int32_t reconnects = 0;            // 스트림을 연 뒤 다시 연결한 횟수
  CameraState readback;  // 마지막으로 다시 읽은 카메라 값
  std::vector<flow::SettingField> mismatches;
  bool finished = false;
  std::optional<flow::EndReason> end_reason;
  std::string error;
  // 이번 녹화에서 RGB 영상이나 사이드카를 열지, 쓰지, 닫지 못했음(디스크 가득 참 포함). 녹화는 끝까지 이어지고 앞서 쓴 파일은 지우지 않습니다
  bool write_failed = false;
  ThermalStatus thermal;  // 열화상 카메라가 없으면 attached가 false입니다
  LightStatus light;      // 조명이 없으면 attached가 false입니다
};

/**
 * 촬영 선언 하나를 실행합니다. 판단은 core(flow::Advance)가 하고, 이 클래스는 스레드 셋으로 실행만 합니다.
 *
 * - 수신 스레드: 프레임을 받아 촬영 흐름에 넣고 버퍼에 복사합니다. 카메라 제어는 하지 않습니다(설정 읽기가 345ms 동안 수신을 막은 실측 때문)
 * - 제어 스레드: 잠금과 재설정을 목표 상태 맞추기(Reconcile)로 처리하고, 바꾼 설정을 촬영 흐름에 사건으로 알립니다
 * - 쓰기 스레드: 영상 파일(FFV1 또는 무압축)과 사이드카를 씁니다
 *
 * 열화상 카메라를 넘기면 열화상 수신 스레드와 열화상 쓰기 스레드를 따로 둡니다. 열화상 SDK는 프레임을 보관하지 않아
 * 수신 스레드에서 디스크에 쓰면 프레임이 빠지기 때문입니다(기존 앱 실측 20~25fps). 열화상 프레임은 RGB 수신 스레드의
 * 촬영 흐름(flow::Advance)에 stream이 kThermal인 프레임 사건으로 들어가며, 녹화 중일 때만 파일에 씁니다. 녹화 구간의
 * 프레임은 선언의 thermal_record_period_us로 골라 기록하고(timing::AdvanceRecordRate), 열화상 미리보기에는 모두 보냅니다.
 * 녹화가 끝나면 스트림마다 장치 시각과 PC 수신 시각으로 timing::FitClockMap을 구해 사이드카의 clock에 남깁니다.
 * 열화상이 끊겨도 RGB 녹화는 계속하고, 열화상 누락은 열화상 사이드카의 gaps에 남깁니다.
 *
 * 사용 순서는 Start, StartRecording(필요한 만큼), Stop입니다. 연결이 끊기면 스스로 다시 열고 선언대로 다시 설정합니다.
 */
class Capture {
 public:
  /**
   * @param camera 실행할 카메라. Capture보다 먼저 소멸하면 안 됩니다
   * @param spec 촬영 선언
   * @param thermal 함께 녹화할 열화상 카메라. 없으면 nullptr. 열지 않은 채로 넘기며 Capture가 Start에서 열고 Stop에서 닫습니다.
   *                Capture보다 먼저 소멸하면 안 됩니다
   * @param light LED 조명. 없으면 nullptr. 포트는 OpenLight로 엽니다. Capture보다 먼저 소멸하면 안 됩니다
   * @note 열화상 카메라의 PC 수신 시각은 RGB 카메라의 host_time과 같은 PC 시계여야 두 스트림의 공통 시각을 비교할 수 있습니다.
   * @note LED 명령은 제어 스레드에서 보냅니다. 촬영 흐름에는 SettingKind::kLight 설정 변경 사건으로 들어가 안정화를 다시 셉니다.
   */
  Capture(RgbCamera& camera, flow::CaptureSpec spec, ThermalCamera* thermal = nullptr, Light* light = nullptr);
  ~Capture();
  Capture(const Capture&) = delete;
  Capture& operator=(const Capture&) = delete;

  /**
   * 실행 전 카메라 상태를 저장값까지 읽어 두고(RgbCamera::ReadStoredState), 미리보기 목표로 맞춘 뒤 스레드를 시작합니다.
   *
   * @param error 실패하면 이유를 씁니다
   * @return 시작했으면 true. 선언이 검증을 통과하지 못하면 카메라를 건드리지 않고 false
   */
  bool Start(std::string* error);

  /**
   * 촬영 버튼입니다. 안정화를 거쳐 녹화를 시작합니다.
   *
   * @param video_path 이번 녹화의 영상 파일 경로(UTF-8). 확장자는 flow::ExtensionOf(spec.codec)
   * @param sidecar_path 이번 녹화의 사이드카 경로
   * @return 촬영 흐름에 넣었으면 true. 미리보기가 아니거나 앞 녹화의 파일을 아직 쓰는 중이거나 경로가 잘못되었으면 false
   * @note 녹화 시작 직전 실제 fps가 선언과 5% 넘게 다르면 녹화하지 않고 Status().error에 이유를 남깁니다.
   */
  bool StartRecording(const std::string& video_path, const std::string& sidecar_path);

  /**
   * 촬영 버튼입니다. 잘라 쓸 영역과 열화상 파일 경로까지 받습니다.
   *
   * @param paths 이번 녹화의 파일 경로
   * @note 동작은 두 인자 StartRecording과 같습니다. 열화상은 RGB 녹화의 첫 프레임부터 마지막 프레임까지 받은 프레임을 씁니다.
   *       paths.ppg_settle_us가 0보다 크면 그 시간을 선언의 SDK fps로 RGB 프레임 수로 바꿔 녹화 시작을 그만큼 미룹니다
   *       (flow::Event::ppg_settle_frames).
   *       WaitFinished는 모든 RGB 사이드카와 열화상 사이드카를 쓴 뒤 돌아옵니다.
   *       RGB 출력이 하나도 없거나 영역이 프레임 밖, 격자에 맞지 않거나 경로가 비었거나 보내지 않은 조명 요청이 있으면 녹화하지 않고
   *       Status().error에 이유를 남기며 finished를 true로 둡니다(end_reason은 비어 있음).
   *       앞 녹화의 사이드카를 다 쓰기 전(finished 전)에는 받지 않습니다. 다음 녹화가 앞 녹화의 누락 수와 설정값을 지우기 때문입니다.
   * @return 촬영 흐름에 넣었으면 true. 넣지 않았으면 false이고 finished를 바꾸지 않을 수 있으므로 부른 쪽이 기다리지 않습니다
   */
  bool StartRecording(const RecordingPaths& paths);

  /** 중지 버튼입니다. 녹화 중이면 파일과 사이드카를 닫고, 안정화 중이면 미리보기로 돌아가기만 합니다. */
  void StopRecording();

  /**
   * 미리보기 중 설정 선언을 바꿉니다. 제어 스레드가 새 목표로 맞춥니다.
   *
   * @return 받아들였으면 true. 미리보기가 아니면(안정화, 녹화 중) false
   */
  bool UpdateSettings(const flow::SettingsSpec& settings);

  /**
   * 미리보기 프레임을 받을 함수를 정합니다. Start 전에 부릅니다.
   *
   * @param every_frames 몇 장마다 부를지
   * @param sink 수신 스레드에서 불립니다. 프레임은 호출이 끝나면 무효이므로 필요한 만큼 복사하고 바로 반환해야 합니다
   */
  void SetPreviewSink(int32_t every_frames, std::function<void(const FrameView&)> sink);

  /**
   * 열화상 미리보기 프레임을 받을 함수를 정합니다. Start 전에 부릅니다. 열화상 카메라가 없으면 불리지 않습니다.
   *
   * @param every_frames 몇 장마다 부를지(프레임 번호 view.index가 이 값의 배수인 프레임)
   * @param sink 열화상 수신 스레드에서 불립니다. 프레임(Y16)은 호출이 끝나면 무효이므로 필요한 만큼 복사하고 바로 반환해야 합니다
   */
  void SetThermalPreviewSink(int32_t every_frames, std::function<void(const FrameView&)> sink);

  /**
   * 조명 포트를 열고 밝기를 보냅니다(Light::Open). 제어 스레드가 실행하고 결과는 Status().light에 남깁니다.
   *
   * @param port_name 사람이 고른 포트 이름
   * @param percent 열자마자 보낼 밝기(%)
   * @return 요청을 받았으면 true. 조명이 없거나 미리보기가 아니면(flow::AllowsLightChange) false이고 아무것도 보내지 않습니다
   */
  bool OpenLight(const std::string& port_name, int32_t percent);

  /**
   * 조명 밝기를 보냅니다(Light::SetPercent). 제어 스레드가 실행하고 결과는 Status().light에 남깁니다.
   *
   * @param percent 밝기(%). 0~100으로 자릅니다
   * @return 요청을 받았으면 true. 조명이 없거나 미리보기가 아니면 false이고 아무것도 보내지 않습니다
   * @note 제어 스레드는 대기 중인 조명 요청을 잠금과 재설정보다 먼저 실행합니다. 자동 맞춤은 LED가 바뀐 뒤 잠급니다.
   *       실행할 때 녹화 중이면 보내지 않고 Status().light.error에 이유를 남깁니다.
   */
  bool SetLight(int32_t percent);

  /** 조명 포트를 닫습니다. LED는 마지막 밝기로 남으므로 설정 변경 사건을 만들지 않습니다. */
  void CloseLight();

  /**
   * 열화상 렌즈 셰이딩 보정을 하고 보정 파일로 저장합니다(ThermalCamera::CorrectLensShading). 제어 스레드가 실행하고 결과는
   * Status().thermal.lens_shading에 남깁니다.
   *
   * @return 요청을 받았으면 true. 열화상 카메라가 없거나, 열화상을 받고 있지 않거나(kReceiving 아님), 미리보기가 아니거나,
   *         앞 요청이 아직 끝나지 않았으면 false. 요청을 받은 뒤 실행할 때 받고 있지 않아도 보정하지 않습니다
   * @note 사람이 렌즈를 실온의 고른 면으로 가린 뒤에 부릅니다. 보정하는 동안(1.5~1.8초) 열화상 프레임이 오지 않습니다.
   *       요청부터 끝날 때까지 Status().thermal.lens_shading_busy가 true입니다. 제어 스레드가 보정을 시작하기 전에
   *       미리보기가 끝나면(그 사이 촬영 버튼을 누름) 보정하지 않습니다. 보정을 시작한 뒤 촬영 버튼을 누르면 잠금과
   *       열화상 준비가 같은 제어 스레드에서 보정 뒤에 실행되므로 녹화는 보정이 끝난 뒤 시작합니다.
   */
  bool CorrectThermalLensShading();

  /**
   * LED 변경의 반영 판정 기준을 정합니다. Start 전에 부릅니다.
   *
   * @param rule 판정 기준. 정하지 않으면 판정하지 않고(kUnknown) 미리보기 프레임의 밝기도 재지 않습니다
   * @note 판정 기준이 있으면 미리보기와 안정화 중 RGB 프레임마다 전체 화면 밝기를 수신 스레드에서 잽니다.
   */
  void SetLightResponseRule(const pixel::LightResponseRule& rule);

  /** 지금 상태의 사본입니다. 어느 스레드에서 불러도 됩니다. */
  CaptureStatus Status() const;

  /**
   * 녹화가 끝날 때까지 기다립니다(완료, 중지, 연결 끊김, fps 불일치로 거부).
   *
   * @return 제한 시간 안에 끝났으면 true
   */
  bool WaitFinished(std::chrono::milliseconds timeout);

  /**
   * 스레드를 멈추고 카메라를 실행 전 상태로 되돌립니다. 같은 카메라를 다른 사람도 쓰기 때문입니다.
   *
   * @param on_step 단계를 시작할 때마다 단계 이름(한국어)으로 부릅니다. 종료가 멈췄을 때 어느 단계인지 남기는 데 씁니다
   * @return 되돌린 결과(Restore). mismatches가 비어 있으면 실행 전과 모드와 저장값이 모두 같습니다
   * @note 녹화 중이면 중지 버튼과 같게 파일과 사이드카를 닫은 뒤 멈춥니다.
   */
  ReconcileResult Stop(const std::function<void(const char*)>& on_step = nullptr);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/**
 * 느린 간격을 사람이 읽는 문장으로 바꿉니다. 촬영 결과 줄과 결과 창이 같은 문장을 씁니다.
 *
 * @param check timing::CheckFrameRate의 결과
 * @return 느린 간격이 없으면 빈 문자열. 있으면 "fps 저하 12.3~14.1초, 최저 25.0fps"처럼 첫 구간과 최저 fps,
 *         구간이 여럿이면 "외 N곳"을 붙입니다. 시각은 녹화 첫 프레임 기준 초입니다
 */
std::string DescribeSlowFrames(const timing::FrameRateCheck& check);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_CAPTURE_H_
