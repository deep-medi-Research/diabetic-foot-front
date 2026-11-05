#ifndef HWLAB_PLATFORM_LIGHT_H_
#define HWLAB_PLATFORM_LIGHT_H_

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/platform/serial_port.h"

namespace hwlab::platform {

/** LED 보드의 직렬 조건입니다. 115200bps 8N1, 흐름 제어 없음(기존 앱과 같음). */
inline constexpr SerialConfig kLightSerial{.baud_rate = 115200};

/** 밝기 명령을 다 내보낼 때까지 기다리는 시간입니다. 115200bps에서 2바이트는 0.2ms 안에 나가므로 넘으면 장치가 빠진 것으로 봅니다. */
inline constexpr int32_t kLightWriteTimeoutMs = 150;

inline constexpr int32_t kLightMinPercent = 0;
inline constexpr int32_t kLightMaxPercent = 100;

/** LED 보드 명령 한 개입니다. 첫 바이트는 항상 0xFE입니다. */
using LightCommand = std::array<uint8_t, 2>;

/**
 * 밝기를 절대값으로 정하는 명령을 만듭니다.
 *
 * @param percent 밝기(%). 0보다 작으면 0, 100보다 크면 100으로 자릅니다
 * @return FE 00(0%)부터 FE 64(100%)까지
 * @note 보드에는 10%씩 올리고 내리는 명령(FE B1, FE B0)도 있지만 쓰지 않습니다. 현재 값을 읽는 명령이 없어
 *       명령 하나가 빠지면 보낸 값과 실제 밝기가 달라지고 되돌릴 방법이 없기 때문입니다.
 */
LightCommand MakeLightCommand(int32_t percent);

/**
 * 자른 밝기입니다. MakeLightCommand가 보내는 값과 같습니다.
 *
 * @param percent 밝기(%)
 * @return 0~100으로 자른 값
 */
int32_t ClampLightPercent(int32_t percent);

/** 사람이 LED 보드로 확인한 직렬 변환기입니다. 다음 실행에서 같은 변환기를 찾을 때 씁니다. */
struct LightPortIdentity {
  uint16_t usb_vendor_id = 0;
  uint16_t usb_product_id = 0;
  std::string serial_number;  // 비어 있으면 일련번호를 내지 않는 변환기

  friend bool operator==(const LightPortIdentity&, const LightPortIdentity&) = default;
};

/**
 * 포트 목록에서 기억한 LED 보드 변환기를 찾아 제안합니다. 포트를 열지 않습니다.
 *
 * @param ports ListSerialPorts의 결과
 * @param remembered 지난번에 사람이 확인한 변환기
 * @return 맞는 포트가 하나뿐이면 그 이름. 없거나 둘 이상이면 nullopt
 * @note 일련번호를 기억했으면 VID:PID와 일련번호가 모두 같아야 합니다. 일련번호가 없는 변환기는 같은 VID:PID가 하나뿐일 때만
 *       제안합니다. PPG 장치도 같은 FTDI 변환기(0403:6001)를 쓸 수 있어 VID:PID만으로는 구분하지 못하기 때문입니다.
 *       제안한 포트도 사람이 연결하고 LED 밝기가 바뀌는 것을 확인합니다.
 */
std::optional<std::string> FindRememberedLightPort(const std::vector<SerialPortInfo>& ports,
                                                   const LightPortIdentity& remembered);

/**
 * 포트 정보에서 기억할 변환기 식별값을 만듭니다.
 *
 * @param port 사람이 LED 보드로 확인한 포트
 * @return USB 변환기가 아니면(VID나 PID가 없으면) nullopt
 */
std::optional<LightPortIdentity> IdentityOf(const SerialPortInfo& port);

/**
 * 직렬 포트로 연결한 LED 조명 하나입니다.
 *
 * 보드는 현재 밝기를 읽는 명령이 없고 명령에 응답하지 않습니다. 그래서 이 클래스가 아는 밝기는 장치에서 읽은 값이 아니라
 * 마지막으로 쓰기에 성공한 명령의 값입니다(LastSentPercent). 포트를 열면 그 자리에서 밝기를 한 번 보내 이 값을 장치와 맞춥니다.
 * 쓰기가 실패하거나 kLightWriteTimeoutMs를 넘으면 포트를 닫고 연결 끊김으로 둡니다.
 *
 * 여러 스레드에서 불러도 됩니다.
 *
 * @note 녹화 중에는 밝기를 바꾸지 않습니다. 이 클래스는 막지 않으므로 부른 쪽(촬영 흐름)이 지킵니다.
 * @note 포트는 사람이 고릅니다. PPG 장치도 같은 종류의 직렬 변환기를 쓰므로 목록의 첫 포트를 자동으로 열지 않습니다.
 */
class Light {
 public:
  /**
   * @param opener 포트를 여는 함수. 제품은 OpenSerialPort, 테스트는 FakeSerialPort::Opener
   */
  explicit Light(SerialPortOpener opener);
  ~Light();

  Light(const Light&) = delete;
  Light& operator=(const Light&) = delete;

  /**
   * 포트를 열고 밝기를 보냅니다. 이미 열려 있으면 먼저 닫습니다.
   *
   * @param port_name 사람이 고른 포트 이름(예: "COM3")
   * @param percent 열자마자 보낼 밝기(%). 0~100으로 자릅니다
   * @param error 실패하면 이유를 씁니다
   * @return 열고 밝기를 보냈으면 true. 열었지만 보내지 못했으면 포트를 닫고 false
   */
  bool Open(const std::string& port_name, int32_t percent, std::string* error);

  /**
   * 밝기를 보냅니다.
   *
   * @param percent 밝기(%). 0~100으로 자릅니다
   * @param error 실패하면 이유를 씁니다
   * @return 보냈으면 true. 연결되어 있지 않거나 쓰기가 실패하면 false이고, 쓰기 실패면 연결을 끊습니다
   */
  bool SetPercent(int32_t percent, std::string* error);

  /**
   * 마지막으로 보내는 데 성공한 밝기(%)입니다. 장치에서 읽은 값이 아닙니다.
   *
   * @return 자른 뒤의 값. 연결되어 있지 않으면 nullopt
   * @note 사이드카에는 이 값을 남깁니다. 연결이 없을 때 화면에 보이는 값은 장치에 보내지 않은 값이라 남기지 않습니다.
   */
  std::optional<int32_t> LastSentPercent() const;

  /** 포트가 열려 있고 마지막 쓰기가 성공했으면 true입니다. */
  bool IsConnected() const;

  /** 연 포트 이름입니다. 연결되어 있지 않으면 빈 문자열입니다. */
  std::string PortName() const;

  /** 포트를 닫습니다. 밝기는 바꾸지 않으므로 LED는 마지막 밝기로 남습니다. */
  void Close();

 private:
  bool SendLocked(int32_t percent, std::string* error);
  void CloseLocked();

  SerialPortOpener opener_;
  mutable std::mutex mutex_;
  std::unique_ptr<SerialPort> port_;
  std::optional<int32_t> last_sent_percent_;
};

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_LIGHT_H_
