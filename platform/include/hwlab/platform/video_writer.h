#ifndef HWLAB_PLATFORM_VIDEO_WRITER_H_
#define HWLAB_PLATFORM_VIDEO_WRITER_H_

#include <cstdint>
#include <memory>
#include <string>

#include "hwlab/flow/capture_spec.h"
#include "hwlab/record/avi.h"

namespace hwlab::platform {

/** 영상 파일 하나를 씁니다. 구현은 무압축 AVI와 FFV1(MKV)입니다. 쓰기 스레드 한 곳에서만 씁니다. */
class VideoWriter {
 public:
  virtual ~VideoWriter() = default;

  /**
   * 파일을 만듭니다.
   *
   * @param path 파일 경로(UTF-8)
   * @param format 화소 형식과 크기와 fps. UYVY만 받습니다
   * @param max_frames 쓸 최대 프레임 수
   * @return 만들었으면 true
   */
  virtual bool Open(const std::string& path, const record::VideoFormat& format, int64_t max_frames) = 0;

  /**
   * 프레임 하나를 씁니다.
   *
   * @param uyvy 행 사이 빈 바이트가 없는 UYVY 화소
   * @return 썼으면 true
   */
  virtual bool WriteFrame(const uint8_t* uyvy) = 0;

  /** 남은 데이터를 쓰고 파일을 닫습니다. 일찍 멈춰도 그때까지 쓴 프레임으로 읽을 수 있는 파일이 됩니다. */
  virtual bool Close() = 0;

  /** 쓴 프레임 수입니다. */
  virtual int64_t FramesWritten() const = 0;
};

/**
 * 코덱에 맞는 영상 쓰기 객체를 만듭니다.
 *
 * @param codec 코덱
 * @param error 실패하면 이유를 씁니다
 * @return 쓰기 객체. 이 빌드에서 쓸 수 없는 코덱이면(FFmpeg 없이 빌드) nullptr
 */
std::unique_ptr<VideoWriter> MakeVideoWriter(flow::VideoCodec codec, std::string* error);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_VIDEO_WRITER_H_
