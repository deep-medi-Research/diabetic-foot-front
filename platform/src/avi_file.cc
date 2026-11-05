#include "hwlab/platform/avi_file.h"

#include "hwlab/platform/shot_folder.h"

namespace hwlab::platform {

namespace {

// 4GB를 넘는 위치로 옮기려면 64비트 위치 함수가 필요합니다.
int SeekTo(std::FILE* file, uint64_t offset) {
#ifdef _WIN32
  return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET);
#else
  return fseeko(file, static_cast<off_t>(offset), SEEK_SET);
#endif
}

int SeekToEnd(std::FILE* file) {
#ifdef _WIN32
  return _fseeki64(file, 0, SEEK_END);
#else
  return fseeko(file, 0, SEEK_END);
#endif
}

}  // namespace

AviFile::~AviFile() {
  if (file_ != nullptr) {
    Close();
  }
}

bool AviFile::Open(const std::string& path, const record::VideoFormat& format,
                         int64_t max_frames) {
  builder_.emplace(format, max_frames);
  if (!builder_->IsValid()) {
    builder_.reset();
    return false;
  }
  file_ = OpenFileUtf8(path, "w+b");
  if (file_ == nullptr) {
    builder_.reset();
    return false;
  }
  // 4MB 프레임을 그대로 넘기므로 C 라이브러리 버퍼를 거치지 않게 합니다.
  std::setvbuf(file_, nullptr, _IONBF, 0);
  return Apply(builder_->Start());
}

bool AviFile::Apply(const record::AviOutput& out) {
  if (!out.append.empty() &&
      std::fwrite(out.append.data(), 1, out.append.size(), file_) != out.append.size()) {
    return false;
  }
  if (out.patches.empty()) {
    return true;
  }
  for (const record::FilePatch& patch : out.patches) {
    if (SeekTo(file_, patch.offset) != 0 ||
        std::fwrite(patch.bytes.data(), 1, patch.bytes.size(), file_) != patch.bytes.size()) {
      return false;
    }
  }
  return SeekToEnd(file_) == 0;
}

bool AviFile::WriteFrame(const uint8_t* pixels) {
  if (file_ == nullptr) {
    return false;
  }
  const std::optional<record::AviOutput> out = builder_->BeginFrame();
  if (!out || !Apply(*out)) {
    return false;
  }
  return std::fwrite(pixels, 1, builder_->FrameBytes(), file_) == builder_->FrameBytes();
}

bool AviFile::Close() {
  if (file_ == nullptr) {
    return false;
  }
  const bool ok = Apply(builder_->Finish());
  const bool closed = std::fclose(file_) == 0;
  file_ = nullptr;
  return ok && closed;
}

int64_t AviFile::FramesWritten() const {
  return builder_ ? builder_->FramesWritten() : 0;
}

size_t AviFile::FrameBytes() const { return builder_ ? builder_->FrameBytes() : 0; }

}  // namespace hwlab::platform
