#ifndef HWLAB_PLATFORM_AVI_FILE_H_
#define HWLAB_PLATFORM_AVI_FILE_H_

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include "hwlab/platform/video_writer.h"
#include "hwlab/record/avi.h"

namespace hwlab::platform {

/** 무압축 AVI(OpenDML) 파일입니다. record::AviBuilder가 돌려준 바이트를 실제 파일에 씁니다. */
class AviFile final : public VideoWriter {
 public:
  AviFile() = default;
  ~AviFile() override;
  AviFile(const AviFile&) = delete;
  AviFile& operator=(const AviFile&) = delete;

  bool Open(const std::string& path, const record::VideoFormat& format, int64_t max_frames) override;

  /** pixels는 행 사이 빈 바이트가 없는 FrameBytes() 크기여야 합니다. */
  bool WriteFrame(const uint8_t* pixels) override;
  bool Close() override;

  int64_t FramesWritten() const override;

  /** 프레임 하나의 바이트 수입니다. 열기 전에는 0입니다. */
  size_t FrameBytes() const;

 private:
  bool Apply(const record::AviOutput& out);

  std::FILE* file_ = nullptr;
  std::optional<record::AviBuilder> builder_;
};

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_AVI_FILE_H_
