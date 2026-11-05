#include "hwlab/platform/video_reader.h"

namespace hwlab::platform {

#ifdef HWLAB_HAS_FFV1
std::unique_ptr<VideoReader> MakeFfv1Reader();
#endif

std::unique_ptr<VideoReader> MakeVideoReader(std::string* error) {
#ifdef HWLAB_HAS_FFV1
  (void)error;
  return MakeFfv1Reader();
#else
  if (error != nullptr) *error = "이 빌드에는 FFV1 디코더(libavcodec)가 없습니다";
  return nullptr;
#endif
}

}  // namespace hwlab::platform
