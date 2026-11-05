#ifndef HWLAB_RECORD_AVI_H_
#define HWLAB_RECORD_AVI_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "hwlab/frame.h"

namespace hwlab::record {

/** 영상 파일의 화소 형식, 크기, fps입니다. */
struct VideoFormat {
  PixelFormat format = PixelFormat::kUyvy;
  int32_t width = 0;
  int32_t height = 0;
  int32_t frame_rate = 0;
};

/** 이미 쓴 바이트를 고칠 자리입니다. offset은 파일 처음부터 센 바이트 수입니다. */
struct FilePatch {
  uint64_t offset = 0;
  std::vector<uint8_t> bytes;
};

/** AviBuilder가 돌려주는 쓰기 작업입니다. 파일 끝에 append를 덧붙인 뒤 patches를 순서대로 덮어씁니다. */
struct AviOutput {
  std::vector<uint8_t> append;
  std::vector<FilePatch> patches;
};

/**
 * 무압축 AVI를 OpenDML 형식으로 만듭니다.
 *
 * 1080p UYVY 900장(3.7GB)처럼 1GiB를 넘는 파일을 위해 RIFF를 나눕니다. 파일 입출력은 하지 않고 쓸 바이트와 위치만 돌려줍니다.
 *
 * 호출 순서는 Start, (BeginFrame 뒤 FrameBytes만큼 화소 덧붙이기) 반복, Finish입니다.
 * 일찍 멈춰도(중지, 연결 끊김) Finish를 부르면 그때까지 쓴 프레임 수가 맞게 들어간 파일이 됩니다.
 */
class AviBuilder {
 public:
  // 1GiB를 넘는 RIFF를 읽지 못하는 도구가 있어, RIFF 하나를 이 크기 이하로 나눕니다.
  static constexpr uint64_t kDefaultRiffLimitBytes = uint64_t{1} << 30;

  /**
   * @param format 화소 형식과 크기. UYVY와 NV12만 받습니다
   * @param max_frames 쓸 수 있는 최대 프레임 수. 헤더에 OpenDML 색인 자리를 잡는 데 씁니다
   * @param riff_limit_bytes RIFF 하나의 최대 크기
   */
  AviBuilder(const VideoFormat& format, int64_t max_frames,
             uint64_t riff_limit_bytes = kDefaultRiffLimitBytes);

  /** 형식이나 크기가 잘못되면 false이고, 이때 다른 함수는 빈 결과를 돌려줍니다. */
  bool IsValid() const { return frame_bytes_ > 0; }

  /** 프레임 하나의 화소 바이트 수입니다. BeginFrame 뒤에 이만큼 덧붙입니다. */
  size_t FrameBytes() const { return frame_bytes_; }

  /** 헤더와 첫 movi 목록의 머리를 만듭니다. 한 번만 부릅니다. */
  AviOutput Start();

  /**
   * 다음 프레임의 머리를 만듭니다. RIFF가 차면 색인을 닫고 다음 RIFF를 엽니다.
   *
   * @return 덧붙일 바이트. max_frames를 다 썼으면 nullopt
   */
  std::optional<AviOutput> BeginFrame();

  /** 색인을 닫고 헤더의 크기와 프레임 수를 고칩니다. */
  AviOutput Finish();

  /** BeginFrame으로 머리를 만든 프레임 수입니다. */
  int64_t FramesWritten() const { return frames_written_; }

 private:
  struct Segment {
    uint64_t riff_offset = 0;  // 'RIFF' 위치
    uint64_t movi_offset = 0;  // 'LIST' 위치
    int64_t first_frame = 0;
    int64_t frames = 0;
  };

  void CloseSegment(AviOutput& out);
  void OpenSegment(AviOutput& out);
  std::vector<uint8_t> BuildHeader(int64_t total_frames, int64_t first_riff_frames) const;
  int64_t CapacityOf(size_t segment) const;

  VideoFormat format_;
  size_t frame_bytes_ = 0;
  int64_t max_frames_ = 0;
  uint64_t riff_limit_bytes_ = 0;
  int32_t index_slots_ = 0;
  size_t header_bytes_ = 0;
  uint64_t position_ = 0;  // 지금까지 파일에 쓴 바이트 수
  int64_t frames_written_ = 0;
  std::vector<Segment> segments_;
  struct IndexEntry {
    uint64_t offset = 0;
    uint32_t bytes = 0;
    uint32_t frames = 0;
  };
  std::vector<IndexEntry> index_entries_;
  bool started_ = false;
  bool finished_ = false;
};

}  // namespace hwlab::record

#endif  // HWLAB_RECORD_AVI_H_
