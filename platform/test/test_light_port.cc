#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "hwlab/platform/light.h"
#include "hwlab/platform/serial_port.h"

namespace {

using hwlab::platform::FindRememberedLightPort;
using hwlab::platform::IdentityOf;
using hwlab::platform::LightPortIdentity;
using hwlab::platform::SerialNumberFromInstanceId;
using hwlab::platform::SerialPortInfo;

// FTDI 포트는 두 번째 + 뒤부터 \ 앞까지가 일련번호(포트 문자 포함)입니다.
TEST(SerialNumber, ReadsFtdiBusInstanceId) {
  EXPECT_EQ(SerialNumberFromInstanceId("FTDIBUS\\VID_0403+PID_6001+A50285BIA\\0000"), "A50285BIA");
  EXPECT_EQ(SerialNumberFromInstanceId("FTDIBUS\\VID_0403+PID_6001+A50285BIA"), "A50285BIA");
}

// USB 장치는 마지막 \ 뒤가 일련번호입니다.
TEST(SerialNumber, ReadsUsbInstanceId) {
  EXPECT_EQ(SerialNumberFromInstanceId("USB\\VID_10C4&PID_EA60\\0001"), "0001");
}

// 일련번호가 없는 장치에 Windows가 위치로 만든 값(&가 들어 있음)은 일련번호로 쓰지 않습니다.
TEST(SerialNumber, IgnoresGeneratedInstanceId) {
  EXPECT_EQ(SerialNumberFromInstanceId("USB\\VID_1A86&PID_7523\\5&2C9A7A1&0&3"), "");
  EXPECT_EQ(SerialNumberFromInstanceId("USB\\VID_1A86&PID_7523"), "");
  EXPECT_EQ(SerialNumberFromInstanceId("ACPI\\PNP0501\\1"), "");
  EXPECT_EQ(SerialNumberFromInstanceId("FTDIBUS\\VID_0403+PID_6001"), "");
  EXPECT_EQ(SerialNumberFromInstanceId(""), "");
}

SerialPortInfo Port(const char* name, uint16_t vid, uint16_t pid, const char* serial) {
  return {name, "USB Serial Port", vid, pid, serial};
}

// 같은 FTDI 변환기(0403:6001)가 둘이면 일련번호가 같은 포트만 제안합니다. COM 번호는 보지 않습니다.
TEST(RememberedLightPort, MatchesSerialAmongSameVidPid) {
  const std::vector<SerialPortInfo> ports = {Port("COM6", 0x0403, 0x6001, "PPG0001A"),
                                             Port("COM9", 0x0403, 0x6001, "LED0001A")};

  EXPECT_EQ(FindRememberedLightPort(ports, {0x0403, 0x6001, "LED0001A"}), "COM9");
  EXPECT_EQ(FindRememberedLightPort(ports, {0x0403, 0x6001, "OTHER"}), std::nullopt);
}

// 일련번호가 없는 변환기는 같은 VID:PID가 하나뿐일 때만 제안합니다.
TEST(RememberedLightPort, WithoutSerialRequiresSingleVidPid) {
  const LightPortIdentity remembered{0x1A86, 0x7523, ""};
  const std::vector<SerialPortInfo> one = {Port("COM3", 0x1A86, 0x7523, ""), Port("COM6", 0x0403, 0x6001, "X")};
  const std::vector<SerialPortInfo> two = {Port("COM3", 0x1A86, 0x7523, ""), Port("COM4", 0x1A86, 0x7523, "")};

  EXPECT_EQ(FindRememberedLightPort(one, remembered), "COM3");
  EXPECT_EQ(FindRememberedLightPort(two, remembered), std::nullopt);
}

// USB가 아닌 포트는 기억할 식별값이 없고 제안하지 않습니다.
TEST(RememberedLightPort, SkipsNonUsbPorts) {
  const SerialPortInfo com1{"COM1", "Communications Port", std::nullopt, std::nullopt, ""};

  EXPECT_EQ(IdentityOf(com1), std::nullopt);
  EXPECT_EQ(FindRememberedLightPort({com1}, {0, 0, ""}), std::nullopt);
  EXPECT_EQ(IdentityOf(Port("COM9", 0x0403, 0x6001, "LED0001A")), (LightPortIdentity{0x0403, 0x6001, "LED0001A"}));
}

}  // namespace
