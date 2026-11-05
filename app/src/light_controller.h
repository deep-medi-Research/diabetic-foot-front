#ifndef HWLAB_APP_LIGHT_CONTROLLER_H_
#define HWLAB_APP_LIGHT_CONTROLLER_H_

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/platform/capture.h"
#include "hwlab/platform/light.h"
#include "hwlab/platform/serial_port.h"
#include "hwlab/record/shot.h"
#include "hwlab/record/sidecar.h"

class RememberedValues;

/**
 * LED 조명 화면의 ViewModel입니다.
 *
 * 포트 목록과 기억한 변환기에 맞는 포트 제안을 보여 주고, 사람이 고른 포트를 Capture에 열게 합니다. 밝기 명령은 Capture의
 * 제어 스레드가 보내며(Capture::SetLight), 이 클래스는 Capture 상태(CaptureStatus::light)를 받아 화면에 알리기만 합니다.
 *
 * @note 포트를 열었다고 LED 보드라고 보지 않습니다. PPG 장치도 같은 종류의 변환기를 쓸 수 있어, 사람이 LED 밝기가 바뀌는 것을
 *       보고 확인(confirm)해야 확인한 변환기(VID:PID, 일련번호)로 다음 실행에 남깁니다.
 * @note VID:PID로 포트를 막지 않습니다. PPG 칸이 연 포트는 열지 않고, 지난번 PPG 장치로 연결한 포트는 열기 전에 사람의
 *       확인을 받습니다(SetPpgPorts, openPending).
 * @note 매니페스트 lighting과 사이드카 light는 이 클래스가 만든 같은 출처와 밝기를 씁니다(Lighting, SidecarLight).
 */
class LightController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("main에서 만듭니다")

  /** 포트 목록입니다. 항목마다 {name, description, vidPid, serial, suggested, ppgOpen, lastPpg}입니다. */
  Q_PROPERTY(QVariantList ports READ ports NOTIFY portsChanged)
  Q_PROPERTY(QString suggestedPort READ suggestedPort NOTIFY portsChanged)
  /** 지난번 PPG 장치로 연결한 포트라 열기 전에 확인을 기다리는 포트입니다. 없으면 빈 문자열입니다. */
  Q_PROPERTY(QString pendingPort READ pendingPort NOTIFY stateChanged)
  Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
  Q_PROPERTY(QString portName READ portName NOTIFY stateChanged)
  Q_PROPERTY(int percent READ percent NOTIFY stateChanged)
  Q_PROPERTY(bool confirmed READ confirmed NOTIFY stateChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
  Q_PROPERTY(bool canChange READ canChange NOTIFY stateChanged)
  Q_PROPERTY(QString responseState READ responseState NOTIFY stateChanged)
  Q_PROPERTY(QString responseText READ responseText NOTIFY stateChanged)
  Q_PROPERTY(QString error READ error NOTIFY stateChanged)
  Q_PROPERTY(QString summary READ summary NOTIFY stateChanged)

 public:
  /**
   * @param light 조명. Capture에 넘기므로 이 객체가 Capture보다 오래 있어야 합니다
   * @param list_ports 포트 목록을 읽는 함수. 제품은 ListSerialPorts, --fake는 가짜 목록
   * @param product 매니페스트에 남길 조명 제품 이름
   * @param remembered_values 지난 변환기를 읽고 사람이 확인한 변환기를 기억합니다. 이 객체보다 오래 있어야 합니다
   * @param remembered 시험용으로 기억한 변환기를 정합니다. 주면 기억한 값 대신 씁니다
   */
  LightController(std::unique_ptr<hwlab::platform::Light> light,
                  std::function<std::vector<hwlab::platform::SerialPortInfo>()> list_ports, std::string product,
                  RememberedValues* remembered_values,
                  std::optional<hwlab::platform::LightPortIdentity> remembered = std::nullopt);

  /** Capture에 넘길 조명입니다. */
  hwlab::platform::Light* Device() { return light_.get(); }

  /** 명령을 보낼 Capture를 정합니다. Capture::Start 뒤에 부릅니다. */
  void Bind(hwlab::platform::Capture* capture) { capture_ = capture; }

  /** Capture 상태를 받아 바뀐 속성만 알립니다. CaptureController가 0.2초마다 부릅니다. */
  void Update(const hwlab::platform::CaptureStatus& status);

  /**
   * PPG 칸의 포트를 받아 목록에 표시합니다. 포트를 열거나 닫지 않습니다.
   *
   * @param open PPG 칸이 열었거나 여는 포트. 없으면 빈 문자열
   * @param last_ppg 지난번 PPG 장치로 연결한 변환기의 포트. 없으면 빈 문자열
   * @note 포트는 공유 없이 열리므로 PPG 칸이 연 포트는 이 칸에서 열 수 없습니다(CreateFileW 공유 모드 0).
   */
  void SetPpgPorts(const QString& open, const QString& last_ppg);

  /** 매니페스트의 lighting입니다. 포트가 연결되어 있으면 앱 제어와 보낸 밝기, 아니면 앱 밖에서 켬입니다. */
  hwlab::record::LightingInfo Lighting() const;

  /** 사이드카 light의 출처, 포트, 사람 확인입니다. 밝기와 반영 판정은 Capture가 녹화를 시작할 때 채웁니다. */
  hwlab::record::LightRecord SidecarLight() const;

  QVariantList ports() const { return ports_; }
  QString suggestedPort() const { return suggested_; }
  QString pendingPort() const { return pending_port_; }
  bool connected() const { return connected_; }
  QString portName() const { return port_name_; }
  int percent() const { return percent_.value_or(-1); }
  bool confirmed() const { return confirmed_; }
  bool busy() const { return busy_; }
  bool canChange() const { return can_change_; }
  QString responseState() const { return response_state_; }
  QString responseText() const;
  QString error() const { return error_; }
  QString summary() const;

  /** 포트 목록을 다시 읽고 기억한 변환기에 맞는 포트를 찾습니다. 포트를 열지 않습니다. */
  Q_INVOKABLE void refreshPorts();

  /**
   * 사람이 고른 포트를 열고 밝기를 보냅니다. 미리보기가 아니면 아무것도 하지 않습니다.
   *
   * @param name 포트 이름
   * @param percent 열자마자 보낼 밝기(%)
   * @note PPG 칸이 연 포트는 열지 않습니다. 지난번 PPG 장치로 연결한 포트는 열지 않고 pendingPort로 두어 확인을 기다립니다.
   */
  Q_INVOKABLE void connectPort(const QString& name, int percent);

  /**
   * 확인을 기다리던 포트(pendingPort)를 열고 밝기를 보냅니다. 사람이 PPG 장치가 아니라 LED 보드라고 판단했을 때 누릅니다.
   *
   * @param percent 열자마자 보낼 밝기(%)
   */
  Q_INVOKABLE void openPending(int percent);

  /** 확인을 기다리던 포트를 열지 않습니다. */
  Q_INVOKABLE void cancelPending();

  /** 포트를 닫습니다. LED는 마지막 밝기로 남고, 이후 촬영은 앱 밖에서 켠 조명으로 기록합니다. */
  Q_INVOKABLE void disconnectPort();

  /**
   * 밝기를 보냅니다('적용').
   *
   * @return 받아들였으면 true. 연결되어 있지 않거나 미리보기가 아니면 false
   */
  Q_INVOKABLE bool apply(int percent);

  /** 사람이 LED 밝기가 바뀌는 것을 보고 이 포트가 LED 보드라고 확인했습니다. 변환기를 다음 실행에 남깁니다. */
  Q_INVOKABLE void confirm();

 signals:
  void portsChanged();
  void stateChanged();

 private:
  void UpdatePortRows();
  void Open(const QString& name, int percent);

  std::unique_ptr<hwlab::platform::Light> light_;
  std::function<std::vector<hwlab::platform::SerialPortInfo>()> list_ports_;
  const std::string product_;
  RememberedValues* const remembered_values_;
  std::optional<hwlab::platform::LightPortIdentity> remembered_;
  hwlab::platform::Capture* capture_ = nullptr;
  std::vector<hwlab::platform::SerialPortInfo> infos_;
  std::optional<hwlab::platform::SerialPortInfo> requested_port_;  // 마지막으로 열기를 요청한 포트
  QVariantList ports_;
  QString suggested_;
  QString ppg_open_;      // PPG 칸이 연 포트
  QString last_ppg_;      // 지난번 PPG 장치로 연결한 변환기의 포트
  QString pending_port_;  // 열기 전에 확인을 기다리는 포트
  bool connected_ = false;
  QString port_name_;
  std::optional<int32_t> percent_;
  bool confirmed_ = false;
  bool busy_ = false;
  bool can_change_ = false;
  QString response_state_ = QStringLiteral("unknown");  // "unknown", "judging", "applied", "not_seen"
  QString error_;
};

#endif  // HWLAB_APP_LIGHT_CONTROLLER_H_
