#include <cstdio>
#include <string>

#include "hwlab/platform/rgb_camera.h"

namespace hwlab::platform {

std::string LowFrameRateMessage(const std::string& format, double max_fps, int32_t frame_rate) {
  char fps[32];
  std::snprintf(fps, sizeof(fps), "%.0f", max_fps);
  return format + " 형식이 최대 " + fps + "fps라 " + std::to_string(frame_rate) +
         "fps로 찍을 수 없습니다. 카메라가 USB 2 속도로 연결된 것으로 보입니다. "
         "USB 3 케이블을 쓰고 허브 없이 PC에 바로 꽂으십시오. Type-C 단자라면 방향을 뒤집어 꽂으십시오";
}

}  // namespace hwlab::platform
