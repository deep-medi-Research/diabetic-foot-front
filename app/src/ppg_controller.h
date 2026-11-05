#ifndef HWLAB_APP_PPG_CONTROLLER_H_
#define HWLAB_APP_PPG_CONTROLLER_H_

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "hwlab/platform/ppg_sensor.h"
#include "hwlab/platform/serial_port.h"

class RememberedValues;

/** 촬영을 저장할 때 PPG 파일을 쓴 결과입니다. */
struct PpgSaveResult {
  QString line;         // 촬영 결과 줄에 붙일 문장
  bool failed = false;  // PPG 파일이나 사이드카를 쓰지 못했음(디스크 가득 참 포함)
};

/**
 * PPG 장치 화면의 ViewModel입니다.
 *
 * 사람이 고른 포트로 PPG 장치를 열고, 촬영 버튼을 누르면(BeginShot) 장치를 다시 시작해 파형 기록을 시작합니다.
 * 포트를 열면 장치 스레드가 정보 요청만 보내고 장치가 답한 뒤에 측정을 시작하므로, PPG 장치가 아닌 포트를 골라도 그 포트에
 * 측정 명령이 나가지 않습니다(PPG 장치 확인). 답을 기다리는 동안 "PPG 장치 확인 중", 1.5초가 지나면 "PPG 장치 준비 중"을
 * 보이고, 90초 동안 답이 없으면 포트를 닫고 "이 포트에서 PPG 장치를 찾지 못했습니다"를 보입니다.
 * 지난번에 측정 데이터를 받은 변환기(VID:PID, 일련번호)가 목록에 하나만 보이면 앱을 켤 때와 다시 꽂았을 때 그 포트에 자동으로
 * 연결합니다(MaybeAutoConnect). 사람이 연결을 끊으면 그 실행 동안은 자동으로 연결하지 않습니다.
 * 촬영이 저장되면(EndShot) 그때까지 받은 파형을 촬영 폴더의 `<촬영 이름>_ppg.csv`로, 시계 구간과 누락을
 * `<촬영 이름>_ppg.json`으로 씁니다. 결과 창은 RGB 사이드카의 공통 시각으로 녹화 구간만 잘라 그립니다.
 * 장치를 열지 않으면 PPG 없이 촬영합니다.
 *
 * 포트, 해석기, 상태는 앱이 도는 동안 하나 두는 장치 스레드(hwlab::platform::PpgSensor)가 가집니다. 화면 스레드는 명령을
 * 넣고, 장치 스레드가 상태가 바뀔 때만 보내는 스냅숏을 받아 속성을 바꾸고 그리기만 합니다. 촬영 기록(PpgShotRecord)은
 * 촬영이 가지므로, 녹화 중 연결이 끊겨도 그때까지 받은 표본은 저장하고 끊긴 시각을 촬영 결과 줄에 남깁니다.
 *
 * @note 장치 스레드가 직렬 드라이버 대기에서 kStallUs 넘게 돌아오지 않으면 화면에 알립니다. 기존 앱 실기(2026-10-20)에서
 *       FTDI 드라이버가 멈췄을 때는 USB를 다시 꽂아야 풀렸습니다.
 * @note 장치 시각은 표본 번호이고 PC 시계보다 약 0.5% 느려서(2026-10-19 실측) PC 수신 시각으로 맞춥니다(PpgLink).
 *       장치 스레드의 PC 시계(steady_clock)는 Windows에서 카메라 수신 시각(QueryPerformanceCounter)과 같은 카운터입니다.
 * @note 실험 노트북의 USB 포트에 바로 꽂으면 파형이 0.2~0.7초 뒤 멈춥니다. 허브를 거치면 정상입니다(이슈 #4).
 */
class PpgController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("main에서 만듭니다")

  /** 포트 목록입니다. 항목마다 {name, description, vidPid, serial, suggested, lightOpen}입니다. */
  Q_PROPERTY(QVariantList ports READ ports NOTIFY portsChanged)
  /** 포트를 열었거나 여는 중인지입니다. */
  Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
  Q_PROPERTY(QString portName READ portName NOTIFY stateChanged)
  /** 파형 표본이 오는 중인지입니다. */
  Q_PROPERTY(bool streaming READ streaming NOTIFY stateChanged)
  /** 촬영 중이라 파형을 기록하고 있는지입니다. 녹화 중 연결이 끊겨도 촬영이 끝날 때까지 true입니다. */
  Q_PROPERTY(bool recording READ recording NOTIFY stateChanged)
  Q_PROPERTY(int heartRate READ heartRate NOTIFY stateChanged)
  Q_PROPERTY(int spo2 READ spo2 NOTIFY stateChanged)
  /**
   * 상태 한 줄입니다. 예: "수신 중  72 bpm  SpO2 98%", "신호 나쁨  72 bpm  SpO2 98%", "손가락 빠짐", "연결 중",
   * "PPG 장치 확인 중", "PPG 장치 준비 중", "이 포트에서 PPG 장치를 찾지 못했습니다", "연결 안 됨",
   * "PPG 장치가 응답하지 않습니다".
   */
  Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
  /** 상태 색 이름입니다. "ok", "warn", "error", "off" 중 하나입니다. */
  Q_PROPERTY(QString tone READ tone NOTIFY stateChanged)
  /**
   * 최근 맥파입니다. 손가락 센서 원시값을 100Hz로 평균하고, 1~99% 범위를 0~1로 옮긴 값을 오래된 것부터 담습니다.
   * 표본이 오지 않으면 비어 있습니다. 많아야 wavePoints개(5초)입니다. 장치 스레드가 만듭니다(PpgWavePoints).
   */
  Q_PROPERTY(QList<qreal> wave READ wave NOTIFY waveChanged)
  /** wave가 가득 찼을 때의 점 수입니다. 화면은 마지막 점을 오른쪽 끝에 두고 이 수로 가로 간격을 정합니다. */
  Q_PROPERTY(int wavePoints READ wavePoints CONSTANT)

 public:
  /** 장치 스레드가 이 시간 넘게 돌지 않거나 명령을 이 시간 넘게 처리하지 않으면 멈춘 것으로 봅니다. */
  static constexpr int64_t kStallUs = 2'000'000;

  /**
   * @param opener 포트를 여는 함수(OpenSerialPort, 가짜 포트). 장치 스레드에서 부릅니다
   * @param list_ports 포트 목록을 읽는 함수. 장치 스레드에서 부릅니다
   * @param remembered_values 측정 데이터를 받은 변환기(VID:PID, 일련번호)를 기억하고 목록에서 제안합니다. 이 객체보다 오래 있어야 합니다
   * @param remembered 시험용으로 기억한 변환기("VID:PID:일련번호")를 정합니다. 주면 기억한 값 대신 씁니다
   */
  PpgController(hwlab::platform::SerialPortOpener opener,
                std::function<std::vector<hwlab::platform::SerialPortInfo>()> list_ports,
                RememberedValues* remembered_values, QString remembered = QString(), QObject* parent = nullptr);

  /** 장치 스레드를 끝냅니다. 직렬 드라이버가 멈춰도 1초 넘게 기다리지 않습니다(PpgSensor::kQuitWaitUs). */
  ~PpgController() override;

  QVariantList ports() const { return ports_; }
  bool connected() const;
  QString portName() const { return QString::fromStdString(snapshot_.port); }
  bool streaming() const { return snapshot_.state == hwlab::platform::PpgState::kStreaming; }
  bool recording() const { return record_ != nullptr; }
  int heartRate() const { return snapshot_.heart_rate; }
  int spo2() const { return snapshot_.spo2; }
  QString statusText() const;
  QString tone() const;
  QList<qreal> wave() const { return wave_; }
  int wavePoints() const;

  /**
   * 기억한 변환기가 꽂힌 포트입니다. 조명 칸이 이 포트를 열기 전에 확인을 받는 데 씁니다.
   *
   * @return 마지막으로 읽은 목록에서 기억한 변환기와 맞는 포트가 하나뿐이면 그 이름, 아니면 빈 문자열
   */
  QString LastPpgPort() const;

  /**
   * 조명 칸이 연 포트를 받아 목록에 표시합니다. 그 포트는 이 칸에서 열지 않습니다.
   *
   * @param name 조명 칸이 연 포트. 없으면 빈 문자열
   */
  void SetLightPort(const QString& name);

  /** 포트 목록을 다시 읽습니다. 포트를 열지 않습니다. 목록은 다 읽은 뒤 portsChanged로 알립니다. */
  Q_INVOKABLE void refreshPorts();

  /**
   * 포트를 열고, 장치가 답하면 미리보기 측정을 시작합니다. 화면에서 파형이 오는지 바로 보이게 하기 위함입니다. 조명 칸이 연
   * 포트는 열지 않습니다.
   *
   * @note 사람이 끊어 꺼 둔 자동 연결을 다시 켭니다.
   */
  Q_INVOKABLE void connectPort(const QString& name);

  /** 측정을 멈추고 포트를 닫습니다. 이 실행 동안 자동 연결과 다시 연결을 하지 않습니다. */
  Q_INVOKABLE void disconnectPort();

  /** 촬영 버튼을 눌렀습니다. 장치를 열었으면 촬영 기록을 만들어 장치 스레드에 넘기고 장치를 다시 시작합니다. */
  void BeginShot();

  /**
   * 촬영이 저장되었습니다. 촬영 기록을 닫고, 기록을 파일로 쓰는 함수를 돌려줍니다. 장치 스레드를 기다리지 않습니다.
   *
   * @param rgb_sidecars 이 촬영의 RGB 사이드카 경로들(빈 문자열은 건너뜀). 돌려준 함수가 앞에서부터 처음 열리는 파일의
   *        첫 장부터 마지막 장까지(RGB 녹화 구간) 측정 오류를 셉니다. 하나도 읽지 못하면 세지 않고 사이드카의 rgb_window를
   *        null로 씁니다. 파일을 여는 일은 화면 스레드에서 하지 않습니다
   * @return `<folder>/<stem>_ppg.csv`와 사이드카 `<folder>/<stem>_ppg.json`을 쓰고 촬영 결과 줄에 붙일 문장(표본 수,
   *         RGB 녹화 구간 안 측정 오류 값마다의 시간과 비율, 녹화 중 연결이 끊겼으면 끊긴 시각)을 돌려주는 함수.
   *         화면 스레드가 아닌 곳에서 부릅니다. 촬영 기록이 없었으면 빈 함수입니다
   * @note 측정 오류가 있어도 촬영을 막거나 다시 찍으라고 하지 않습니다. 기준을 정하지 않았습니다(표 10).
   */
  std::function<PpgSaveResult()> EndShot(const std::string& folder, const std::string& stem,
                                   std::vector<std::string> rgb_sidecars);

  /** 촬영을 저장하지 않고 끝냈습니다. 기록을 버립니다. */
  void CancelShot();

 signals:
  void portsChanged();
  void stateChanged();
  void waveChanged();

 private:
  void ApplySnapshot(const hwlab::platform::PpgSnapshot& snapshot);
  void ApplyWave(const QList<qreal>& wave);
  void SetPorts(std::vector<hwlab::platform::SerialPortInfo> infos);
  void UpdatePortRows();
  void RememberPort(const QString& name);
  void CheckStall();
  /**
   * 지난번 PPG 변환기가 꽂힌 포트가 목록에 하나만 있으면 엽니다. 사람이 끊었거나, 촬영 중(촬영 버튼부터 저장까지)이거나,
   * 이미 열었거나, 조명 칸이 연 포트이면 열지 않습니다. 시도는 kAutoRetryMs 간격, 같은 장치가 보이는 동안 kMaxAutoAttempts번까지입니다.
   */
  void MaybeAutoConnect();
  /** 자동 연결을 기다리는 동안 포트 목록을 다시 읽어, 다시 꽂은 장치를 찾습니다. */
  void PollForReconnect();

  RememberedValues* const remembered_values_;
  QString remembered_;       // 기억한 변환기(VID:PID:일련번호). 없으면 빈 문자열
  QString remembered_port_;  // 이번 연결에서 기억한 포트. 연결이 바뀌면 비웁니다
  QString light_open_;       // 조명 칸이 연 포트
  std::vector<hwlab::platform::SerialPortInfo> infos_;  // 마지막으로 읽은 포트 목록
  QVariantList ports_;
  hwlab::platform::PpgSnapshot snapshot_;
  QList<qreal> wave_;
  std::shared_ptr<hwlab::platform::PpgShotRecord> record_;  // 촬영 중인 기록. 촬영 중이 아니면 nullptr
  bool stalled_ = false;
  QTimer stall_timer_;
  bool auto_connect_ = true;   // 사람이 연결을 끊으면 false, 사람이 포트를 고르면 다시 true
  int auto_attempts_ = 0;      // 지난번 PPG 장치가 목록에 보이는 동안 자동으로 연 횟수. 측정 데이터를 받거나 장치가 빠지면 0
  int64_t last_auto_us_ = 0;   // 마지막으로 자동으로 연 시각(steady_clock)
  QTimer reconnect_timer_;
  std::unique_ptr<hwlab::platform::PpgSensor> sensor_;  // 장치 스레드. 소멸자에서 가장 먼저 끝냅니다
};

#endif  // HWLAB_APP_PPG_CONTROLLER_H_
