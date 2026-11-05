#ifndef HWLAB_PLATFORM_THERMAL_WRITER_H_
#define HWLAB_PLATFORM_THERMAL_WRITER_H_

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/frame.h"
#include "hwlab/pixel/convert.h"
#include "hwlab/pixel/rect.h"
#include "hwlab/platform/avi_file.h"

namespace hwlab::platform {

/**
 * 열화상 원시값 파일(raw16)을 씁니다. 분석에 쓰는 원본입니다.
 *
 * 머리글 없이 프레임마다 width * height개의 uint16을 리틀 엔디언, 행 순서로 이어 붙입니다(기존 앱과 같은 형식).
 * 크기와 프레임 수는 파일에 없으므로 사이드카에 남깁니다. 프레임 수는 파일 크기를 프레임 바이트 수로 나눈 값과 같습니다.
 *
 * @note 머리글이 없어 녹화 중 프로그램이 멈춰도 그때까지 쓴 프레임을 그대로 읽을 수 있습니다.
 *       쓰기 스레드 한 곳에서만 씁니다.
 */
class Raw16File {
 public:
  Raw16File() = default;
  Raw16File(const Raw16File&) = delete;
  Raw16File& operator=(const Raw16File&) = delete;

  /**
   * 파일을 만듭니다. 같은 이름의 파일이 있으면 덮어씁니다.
   *
   * @param path 파일 경로(UTF-8)
   * @param width 프레임 폭
   * @param height 프레임 높이
   * @return 만들었으면 true
   */
  bool Open(const std::string& path, int32_t width, int32_t height);

  /**
   * 프레임 하나를 씁니다.
   *
   * @param y16 Y16 형식 프레임. 크기가 Open과 같아야 합니다. 행 간격(row_stride)이 폭보다 커도 됩니다
   * @return 썼으면 true. 형식이나 크기가 다르면 쓰지 않고 false
   */
  bool WriteFrame(const FrameView& y16);

  /** 파일을 닫습니다. 열려 있지 않았거나 쓰기에 실패했으면 false입니다. 프레임이 0장이어도 파일은 남깁니다. */
  bool Close();

  /** 쓴 프레임 수입니다. */
  int64_t FramesWritten() const { return frames_; }

  /** 프레임 하나의 바이트 수(width * height * 2)입니다. 열기 전에는 0입니다. */
  size_t FrameBytes() const;

 private:
  std::ofstream file_;
  int32_t width_ = 0;
  int32_t height_ = 0;
  int64_t frames_ = 0;
};

/**
 * 열화상을 사람이 보는 확인용 흑백 영상(무압축 AVI)으로 씁니다.
 *
 * 원시값을 표시 범위로 8비트 회색으로 펴고(pixel::Y16ToGray8) UYVY로 바꿔(pixel::Gray8ToUyvy) AviFile로 씁니다.
 * 표시 범위는 녹화 동안 바꾸지 않습니다. 프레임마다 범위를 다시 잡으면 같은 원시값이 프레임마다 다른 회색이 되어
 * 영상의 밝기 변화를 온도 변화로 읽을 수 없기 때문입니다.
 *
 * @note 분석에는 쓰지 않습니다. 원본은 Raw16File입니다. 폭은 짝수여야 합니다(UYVY).
 */
class ThermalPreviewFile {
 public:
  ThermalPreviewFile() = default;
  ThermalPreviewFile(const ThermalPreviewFile&) = delete;
  ThermalPreviewFile& operator=(const ThermalPreviewFile&) = delete;

  /**
   * 파일을 만듭니다.
   *
   * @param path 파일 경로(UTF-8)
   * @param width 프레임 폭. 짝수
   * @param height 프레임 높이
   * @param frame_rate AVI 머리글에 적을 fps. 재생 속도에만 쓰이고 프레임의 실제 시각은 사이드카에 있습니다
   * @param max_frames 쓸 최대 프레임 수
   * @param range 표시 범위. nullopt이면 처음 쓰는 프레임 전체의 최솟값과 최댓값(pixel::FindRange)으로 정합니다
   * @return 만들었으면 true. 폭이 홀수이거나 파일을 만들지 못하면 false
   */
  bool Open(const std::string& path, int32_t width, int32_t height, int32_t frame_rate, int64_t max_frames,
            std::optional<pixel::Y16Range> range);

  /**
   * 프레임 하나를 씁니다.
   *
   * @param y16 Y16 형식 프레임. 크기가 Open과 같아야 합니다
   * @return 썼으면 true. 형식이나 크기가 다르거나 max_frames를 넘으면 false
   */
  bool WriteFrame(const FrameView& y16);

  /** 남은 색인을 쓰고 닫습니다. 일찍 멈춰도 그때까지 쓴 프레임으로 읽을 수 있는 파일이 됩니다. */
  bool Close();

  /** 쓴 프레임 수입니다. */
  int64_t FramesWritten() const { return avi_.FramesWritten(); }

  /**
   * 녹화에 쓴 표시 범위입니다. 사이드카에 남깁니다.
   *
   * @return 표시 범위. Open에 범위를 주지 않았고 아직 프레임을 쓰지 않았으면 nullopt
   */
  std::optional<pixel::Y16Range> Range() const { return range_; }

 private:
  AviFile avi_;
  int32_t width_ = 0;
  int32_t height_ = 0;
  std::optional<pixel::Y16Range> range_;
  std::vector<uint8_t> gray_;
  std::vector<uint8_t> uyvy_;
};

/**
 * 원시값 파일에서 영역을 잘라 다른 원시값 파일로 씁니다. 녹화 뒤 좌우 발을 따로 저장할 때 씁니다.
 *
 * @param raw16_path 원본 원시값 파일
 * @param width 원본 프레임 폭
 * @param height 원본 프레임 높이
 * @param rect 자를 영역. 원본 프레임 안이어야 합니다
 * @param out_path 결과 원시값 파일. 영역 크기의 프레임이 같은 형식으로 이어집니다
 * @param error 실패하면 이유를 씁니다
 * @return 옮긴 프레임 수. 원본 크기가 프레임 바이트 수의 배수가 아니거나 영역이 프레임 밖이면 nullopt
 */
std::optional<int64_t> WriteRaw16Crop(const std::string& raw16_path, int32_t width, int32_t height,
                                      const pixel::Rect& rect, const std::string& out_path, std::string* error);

/**
 * 원시값 파일에서 영역을 잘라 NumPy .npy 파일로 씁니다. 관류 분석이 thermal_left.npy와 thermal_right.npy를 읽습니다.
 *
 * NumPy format 1.0입니다. 머리글의 dtype은 '<u2', 모양은 (프레임 수, 영역 높이, 영역 폭)이고, 머리글 뒤의 자료는
 * 원시값 파일과 같은 리틀 엔디언 uint16 행 순서입니다. 자료 시작 위치는 64바이트의 배수입니다.
 *
 * @param raw16_path 원본 원시값 파일
 * @param width 원본 프레임 폭
 * @param height 원본 프레임 높이
 * @param rect 자를 영역. 전체를 쓰려면 {0, 0, width, height}
 * @param npy_path 결과 .npy 파일
 * @param error 실패하면 이유를 씁니다
 * @return 옮긴 프레임 수. 원본 크기가 프레임 바이트 수의 배수가 아니거나 영역이 프레임 밖이면 nullopt
 * @note 원시값 파일에서 언제든 다시 만들 수 있는 사본입니다. 머리글에 프레임 수가 들어가므로 녹화가 끝난 뒤 만듭니다.
 */
std::optional<int64_t> WriteNpy(const std::string& raw16_path, int32_t width, int32_t height, const pixel::Rect& rect,
                                const std::string& npy_path, std::string* error);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_THERMAL_WRITER_H_
