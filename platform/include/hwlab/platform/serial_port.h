#ifndef HWLAB_PLATFORM_SERIAL_PORT_H_
#define HWLAB_PLATFORM_SERIAL_PORT_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hwlab::platform {

/** 시스템에 있는 직렬 포트 하나입니다. 사람이 포트를 고를 때 보여 줍니다. */
struct SerialPortInfo {
  std::string name;         // 열 때 쓰는 이름(예: "COM3")
  std::string description;  // 장치 관리자에 보이는 이름. 어느 변환기인지 사람이 구분하는 데 씁니다
  std::optional<uint16_t> usb_vendor_id;   // USB 변환기면 VID(예: 0x10C4)
  std::optional<uint16_t> usb_product_id;  // USB 변환기면 PID(예: 0xEA60)
  // USB 일련번호(SerialNumberFromInstanceId). 장치가 일련번호를 내지 않거나 USB가 아니면 빈 문자열.
  // COM 번호는 꽂는 단자에 따라 바뀌므로 같은 변환기를 다시 찾을 때 VID:PID와 이 값을 씁니다
  std::string serial_number;
};

/**
 * Windows 장치 인스턴스 ID에서 USB 일련번호를 꺼냅니다. 포트를 열지 않는 순수 함수라 모든 OS에서 빌드합니다.
 *
 * - `FTDIBUS\VID_0403+PID_6001+A50285BIA\0000`: 두 번째 `+` 뒤부터 `\` 앞까지(A50285BIA)
 * - `USB\VID_10C4&PID_EA60\0001`: 마지막 `\` 뒤(0001)
 * - `USB\VID_1A86&PID_7523\5&2C9A7A1&0&3`: 마지막 부분에 `&`가 있으면 Windows가 만든 값이라 빈 문자열
 *
 * @param instance_id SetupDiGetDeviceInstanceId나 CM_Get_Device_ID가 돌려준 문자열(UTF-8)
 * @return 일련번호. 꺼낼 수 없으면 빈 문자열
 * @note FTDI 드라이버는 장치 일련번호 뒤에 포트 문자(A, B 등)를 더한 값을 인스턴스 ID에 넣습니다. 이 함수는 그 값을 그대로
 *       돌려주며, 같은 변환기를 다시 찾는 비교에만 쓰므로 포트 문자를 떼지 않습니다.
 */
std::string SerialNumberFromInstanceId(const std::string& instance_id);

/**
 * 직렬 포트를 여는 조건입니다. 데이터 8비트, 패리티 없음, 정지 1비트, 흐름 제어 없음으로 고정합니다.
 *
 * @note LED 보드와 PPG 장치가 모두 115200bps 8N1이라 다른 형식은 두지 않습니다.
 */
struct SerialConfig {
  int32_t baud_rate = 115200;
};

/**
 * 열린 직렬 포트 하나입니다.
 *
 * 쓰기와 읽기는 각자 시간 제한을 받습니다. 한 스레드가 Read로 기다리는 동안 다른 스레드가 Write를 할 수 있습니다.
 * 쓰기나 읽기가 장치 오류로 실패하면 포트는 닫힌 상태가 되고 다시 열리지 않습니다. 다시 쓰려면 새로 엽니다.
 */
class SerialPort {
 public:
  virtual ~SerialPort() = default;

  /** 연 포트 이름입니다(예: "COM3"). 닫힌 뒤에도 같은 값을 돌려줍니다. */
  virtual std::string Name() const = 0;

  /** 포트가 열려 있으면 true입니다. Close를 불렀거나 장치 오류가 난 뒤에는 false입니다. */
  virtual bool IsOpen() const = 0;

  /**
   * 바이트를 모두 내보냅니다.
   *
   * @param bytes 보낼 바이트
   * @param timeout_ms 모두 내보낼 때까지 기다리는 최대 시간
   * @return 시간 안에 모두 내보냈으면 true. 일부만 나갔거나 시간이 지났거나 장치 오류면 false
   * @note 시간 초과는 포트를 닫지 않습니다. 장치가 빠졌다고 볼지는 부른 쪽이 정합니다.
   */
  virtual bool Write(std::span<const uint8_t> bytes, int32_t timeout_ms) = 0;

  /**
   * 받은 바이트를 읽습니다.
   *
   * 이미 받은 바이트가 있으면 기다리지 않고 buffer 크기까지 돌려줍니다. 없으면 첫 바이트가 올 때까지 최대 timeout_ms 기다립니다.
   * 정해진 길이가 다 찰 때까지 기다리지 않으므로, 패킷을 맞추는 일은 부른 쪽이 합니다.
   *
   * @param buffer 받은 바이트를 넣을 곳
   * @param timeout_ms 첫 바이트를 기다리는 최대 시간. 0이면 기다리지 않습니다
   * @return 읽은 바이트 수(시간 초과면 0). 포트가 닫혔거나 장치 오류면 nullopt
   */
  virtual std::optional<size_t> Read(std::span<uint8_t> buffer, int32_t timeout_ms) = 0;

  /**
   * Read로 기다리는 쪽을 바로 돌아오게 합니다. 다른 스레드에서 부릅니다.
   *
   * 기다리는 Read가 없으면 다음 Read 한 번이 기다리지 않고 돌아옵니다. 깨운 Read는 그때까지 받은 바이트 수(없으면 0)를
   * 돌려주고 포트는 닫지 않습니다. 장치 스레드가 Read로 기다리는 동안 명령이 오면 바로 처리하게 하기 위함입니다.
   */
  virtual void WakeRead() = 0;

  /** 포트를 닫습니다. 여러 번 불러도 됩니다. */
  virtual void Close() = 0;
};

/**
 * 이름으로 직렬 포트를 엽니다. 테스트에서는 가짜 포트를 여는 함수로 바꿔 넣습니다.
 *
 * @return 연 포트. 열지 못하면 nullptr이고 error에 이유를 씁니다
 */
using SerialPortOpener =
    std::function<std::unique_ptr<SerialPort>(const std::string& name, const SerialConfig& config, std::string* error)>;

/**
 * 시스템의 직렬 포트 목록을 돌려줍니다. 포트를 열지 않습니다. Windows 빌드에만 있습니다.
 *
 * @return 이름 순서의 포트 목록
 * @note LED 보드와 PPG 장치가 같은 종류의 USB 직렬 변환기를 쓸 수 있어, 목록에서 자동으로 고르지 않고 사람이 고릅니다.
 */
std::vector<SerialPortInfo> ListSerialPorts();

/**
 * 직렬 포트를 엽니다. Windows 빌드에만 있습니다. SerialPortOpener로 넘길 수 있습니다.
 *
 * @param name ListSerialPorts가 돌려준 이름(예: "COM3")
 * @param config 속도
 * @param error 실패하면 이유를 씁니다
 * @return 연 포트. 없는 포트이거나 다른 프로그램이 쓰고 있으면 nullptr
 */
std::unique_ptr<SerialPort> OpenSerialPort(const std::string& name, const SerialConfig& config, std::string* error);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_SERIAL_PORT_H_
