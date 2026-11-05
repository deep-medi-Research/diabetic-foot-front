#ifndef HWLAB_PLATFORM_VIDEO_READER_H_
#define HWLAB_PLATFORM_VIDEO_READER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hwlab::platform {

/**
 * 녹화한 영상 파일에서 번호로 프레임을 읽습니다. 구현은 FFV1(MKV)입니다. 한 스레드에서만 씁니다.
 *
 * @note FFV1은 GOP 1로 써서 모든 장이 독립 프레임이므로, 번호 하나를 읽을 때 그 장만 디코딩합니다.
 */
class VideoReader {
 public:
  virtual ~VideoReader() = default;

  /**
   * 파일을 엽니다.
   *
   * @param path 파일 경로(UTF-8)
   * @param error 실패하면 이유를 씁니다. nullptr이면 쓰지 않습니다
   * @return 열었으면 true. 영상 스트림이 없거나 화소 형식이 4:2:2 8비트가 아니면 false
   */
  virtual bool Open(const std::string& path, std::string* error) = 0;

  /** 프레임 폭입니다. 열기 전에는 0입니다. */
  virtual int32_t Width() const = 0;

  /** 프레임 높이입니다. 열기 전에는 0입니다. */
  virtual int32_t Height() const = 0;

  /**
   * 파일의 프레임 수입니다. 컨테이너 길이와 fps로 셉니다.
   *
   * @note 녹화 앱이 마지막 장까지 길이를 넣어 쓰므로(ffv1_file.cc) 쓴 장 수와 같습니다. 결과 창은 사이드카의 장 수를 함께 봅니다.
   */
  virtual int64_t FrameCount() const = 0;

  /**
   * 번호 index의 프레임을 UYVY로 읽습니다.
   *
   * @param index 프레임 번호(0부터, 파일 안 순서)
   * @param uyvy 결과. 행 사이 빈 바이트 없이 Width() * Height() * 2 바이트로 맞춥니다
   * @return 읽었으면 true. 번호가 범위 밖이거나 디코딩에 실패하면 false
   * @note 바로 앞에 읽은 번호의 다음 번호는 탐색하지 않고 이어서 읽습니다.
   */
  virtual bool ReadFrame(int64_t index, std::vector<uint8_t>* uyvy) = 0;
};

/**
 * 영상 읽기 객체를 만듭니다.
 *
 * @param error 실패하면 이유를 씁니다. nullptr이면 쓰지 않습니다
 * @return 읽기 객체. 이 빌드에 FFmpeg(libavcodec)가 없으면 nullptr
 */
std::unique_ptr<VideoReader> MakeVideoReader(std::string* error = nullptr);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_VIDEO_READER_H_
