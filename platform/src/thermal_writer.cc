#include "hwlab/platform/thermal_writer.h"

#include <bit>
#include <filesystem>
#include <system_error>

#include "hwlab/pixel/crop.h"

namespace hwlab::platform {

namespace {

// Windows에서 좁은 문자열 경로는 ANSI 코드 페이지로 읽혀 한글 환자 이름 폴더를 열지 못하므로 UTF-8로 해석합니다.
std::filesystem::path PathOf(const std::string& utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

bool IsFrameOf(const FrameView& y16, int32_t width, int32_t height) {
  return y16.format == PixelFormat::kY16 && y16.width == width && y16.height == height && y16.plane[0] != nullptr &&
         y16.row_stride[0] >= width * 2;
}

// NumPy format 1.0 머리글입니다. 읽는 쪽이 자료 시작을 64바이트 정렬로 가정하므로 공백으로 채우고 줄바꿈으로 끝냅니다.
std::string NpyHeader(int64_t frames, int32_t height, int32_t width) {
  std::string dict = "{'descr': '<u2', 'fortran_order': False, 'shape': (" + std::to_string(frames) + ", " +
                     std::to_string(height) + ", " + std::to_string(width) + "), }";
  constexpr size_t kPrefixBytes = 10;  // 매직 6, 판 2, 머리글 길이 2
  const size_t unpadded = kPrefixBytes + dict.size() + 1;
  dict.append((unpadded + 63) / 64 * 64 - unpadded, ' ');
  dict.push_back('\n');
  std::string header("\x93NUMPY\x01\x00", 8);
  header.push_back(static_cast<char>(dict.size() & 0xFF));
  header.push_back(static_cast<char>((dict.size() >> 8) & 0xFF));
  return header + dict;
}

std::optional<int64_t> CopyRegion(const std::string& raw16_path, int32_t width, int32_t height,
                                  const pixel::Rect& rect, const std::string& out_path, bool npy, std::string* error) {
  const auto fail = [error](const std::string& why) -> std::optional<int64_t> {
    if (error != nullptr) *error = why;
    return std::nullopt;
  };
  if (width <= 0 || height <= 0 || !pixel::IsAligned(rect, PixelFormat::kY16, width, height)) {
    return fail("영역이 열화상 프레임 밖입니다");
  }
  std::error_code code;
  const uintmax_t bytes = std::filesystem::file_size(PathOf(raw16_path), code);
  if (code) return fail("원시값 파일을 열지 못했습니다: " + raw16_path);
  const uintmax_t frame_bytes = static_cast<uintmax_t>(width) * static_cast<uintmax_t>(height) * 2;
  // 크기가 맞지 않으면 모양을 잘못 알고 있는 것이고, 그대로 옮기면 행이 밀린 영상이 됩니다.
  if (bytes == 0 || bytes % frame_bytes != 0) {
    return fail("원시값 파일 크기가 " + std::to_string(width) + "x" + std::to_string(height) + " 프레임의 배수가 아닙니다");
  }
  const auto frames = static_cast<int64_t>(bytes / frame_bytes);

  std::ifstream in(PathOf(raw16_path), std::ios::binary);
  std::ofstream out(PathOf(out_path), std::ios::binary | std::ios::trunc);
  if (!in || !out) return fail("파일을 열지 못했습니다: " + out_path);
  if (npy) {
    const std::string header = NpyHeader(frames, rect.height, rect.width);
    out.write(header.data(), static_cast<std::streamsize>(header.size()));
  }
  std::vector<uint8_t> frame(static_cast<size_t>(frame_bytes));
  std::vector<uint8_t> region(pixel::PackedBytes(PixelFormat::kY16, rect));
  FrameView view;
  view.format = PixelFormat::kY16;
  view.width = width;
  view.height = height;
  view.plane[0] = frame.data();
  view.row_stride[0] = width * 2;
  view.stream = StreamKind::kThermal;
  for (int64_t i = 0; i < frames; ++i) {
    in.read(reinterpret_cast<char*>(frame.data()), static_cast<std::streamsize>(frame.size()));
    if (in.gcount() != static_cast<std::streamsize>(frame.size())) return fail("원시값을 끝까지 읽지 못했습니다");
    if (!pixel::CopyRect(view, rect, region)) return fail("영역을 자르지 못했습니다");
    out.write(reinterpret_cast<const char*>(region.data()), static_cast<std::streamsize>(region.size()));
  }
  out.close();
  if (!out) return fail("파일을 끝까지 쓰지 못했습니다: " + out_path);
  return frames;
}

}  // namespace

bool Raw16File::Open(const std::string& path, int32_t width, int32_t height) {
  if (file_.is_open() || width <= 0 || height <= 0) return false;
  file_.open(PathOf(path), std::ios::binary | std::ios::trunc);
  if (!file_) return false;
  width_ = width;
  height_ = height;
  frames_ = 0;
  return true;
}

bool Raw16File::WriteFrame(const FrameView& y16) {
  if (!file_.is_open() || !IsFrameOf(y16, width_, height_)) return false;
  const size_t row_bytes = static_cast<size_t>(width_) * 2;
  // 파일은 리틀 엔디언으로 정해져 있으므로 화소를 바이트로 받은 그대로 씁니다. 빅 엔디언 PC는 대상이 아닙니다.
  static_assert(std::endian::native == std::endian::little);
  if (static_cast<size_t>(y16.row_stride[0]) == row_bytes) {
    file_.write(reinterpret_cast<const char*>(y16.plane[0]), static_cast<std::streamsize>(row_bytes * height_));
  } else {
    for (int32_t y = 0; y < height_; ++y) {
      file_.write(reinterpret_cast<const char*>(y16.plane[0] + static_cast<size_t>(y) * y16.row_stride[0]),
                  static_cast<std::streamsize>(row_bytes));
    }
  }
  if (!file_) return false;
  ++frames_;
  return true;
}

bool Raw16File::Close() {
  if (!file_.is_open()) return false;
  file_.close();
  return !file_.fail();
}

size_t Raw16File::FrameBytes() const {
  return static_cast<size_t>(width_) * static_cast<size_t>(height_) * 2;
}

bool ThermalPreviewFile::Open(const std::string& path, int32_t width, int32_t height, int32_t frame_rate,
                              int64_t max_frames, std::optional<pixel::Y16Range> range) {
  if (width <= 0 || height <= 0 || width % 2 != 0) return false;
  if (!avi_.Open(path, {PixelFormat::kUyvy, width, height, frame_rate}, max_frames)) return false;
  width_ = width;
  height_ = height;
  range_ = range;
  gray_.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
  uyvy_.resize(gray_.size() * 2);
  return true;
}

bool ThermalPreviewFile::WriteFrame(const FrameView& y16) {
  if (!IsFrameOf(y16, width_, height_)) return false;
  if (!range_) range_ = pixel::FindRange(y16, {0, 0, width_, height_});
  return pixel::Y16ToGray8(y16, *range_, gray_) && pixel::Gray8ToUyvy(gray_, width_, height_, uyvy_) &&
         avi_.WriteFrame(uyvy_.data());
}

bool ThermalPreviewFile::Close() { return avi_.Close(); }

std::optional<int64_t> WriteRaw16Crop(const std::string& raw16_path, int32_t width, int32_t height,
                                      const pixel::Rect& rect, const std::string& out_path, std::string* error) {
  return CopyRegion(raw16_path, width, height, rect, out_path, false, error);
}

std::optional<int64_t> WriteNpy(const std::string& raw16_path, int32_t width, int32_t height, const pixel::Rect& rect,
                                const std::string& npy_path, std::string* error) {
  return CopyRegion(raw16_path, width, height, rect, npy_path, true, error);
}

}  // namespace hwlab::platform
