// 직렬 포트 목록(이름, VID:PID, USB 일련번호, 장치 이름)을 보여 줍니다. 포트를 열지 않습니다. LED 보드와 PPG 장치가 어느 COM인지 고를 때 씁니다.
//
//   hwlab_serial_ports
#include <cstdio>

#include "hwlab/platform/serial_port.h"

int main() {
  const auto ports = hwlab::platform::ListSerialPorts();
  if (ports.empty()) {
    std::printf("직렬 포트가 없습니다\n");
    return 0;
  }
  for (const auto& port : ports) {
    std::printf("%-6s", port.name.c_str());
    if (port.usb_vendor_id && port.usb_product_id) {
      std::printf("  %04X:%04X", *port.usb_vendor_id, *port.usb_product_id);
    } else {
      std::printf("  %9s", "-");
    }
    std::printf("  %-14s", port.serial_number.empty() ? "-" : port.serial_number.c_str());
    std::printf("  %s\n", port.description.c_str());
  }
  return 0;
}
