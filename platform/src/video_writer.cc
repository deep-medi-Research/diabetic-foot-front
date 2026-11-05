#include "hwlab/platform/video_writer.h"

#include "hwlab/platform/avi_file.h"

namespace hwlab::platform {

#ifdef HWLAB_HAS_FFV1
std::unique_ptr<VideoWriter> MakeFfv1Writer();
#endif

std::unique_ptr<VideoWriter> MakeVideoWriter(flow::VideoCodec codec, std::string* error) {
  switch (codec) {
    case flow::VideoCodec::kUncompressed:
      return std::make_unique<AviFile>();
    case flow::VideoCodec::kFfv1:
#ifdef HWLAB_HAS_FFV1
      return MakeFfv1Writer();
#else
      *error = "이 빌드에는 FFV1 인코더(libavcodec)가 없습니다";
      return nullptr;
#endif
  }
  *error = "알 수 없는 코덱";
  return nullptr;
}

}  // namespace hwlab::platform
