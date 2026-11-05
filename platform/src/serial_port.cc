#include <string>

#include "hwlab/platform/serial_port.h"

namespace hwlab::platform {

namespace {

bool StartsWith(const std::string& text, const char* prefix) { return text.rfind(prefix, 0) == 0; }

}  // namespace

std::string SerialNumberFromInstanceId(const std::string& instance_id) {
  if (StartsWith(instance_id, "FTDIBUS\\")) {
    // VID_xxxx+PID_xxxx+<일련번호><포트 문자>\0000 형식이라 두 번째 + 뒤를 씁니다.
    const size_t first = instance_id.find('+');
    if (first == std::string::npos) return {};
    const size_t second = instance_id.find('+', first + 1);
    if (second == std::string::npos) return {};
    const size_t end = instance_id.find('\\', second + 1);
    return instance_id.substr(second + 1, end == std::string::npos ? std::string::npos : end - second - 1);
  }
  if (StartsWith(instance_id, "USB\\")) {
    const size_t last = instance_id.find_last_of('\\');
    if (last == std::string::npos || last + 1 >= instance_id.size() || last < 4) return {};
    const std::string tail = instance_id.substr(last + 1);
    // 일련번호가 없는 장치는 Windows가 위치로 만든 값(예: 5&2C9A7A1&0&3)을 넣으므로 같은 장치를 가리키지 않습니다.
    if (tail.find('&') != std::string::npos) return {};
    return tail;
  }
  return {};
}

}  // namespace hwlab::platform
