#include "hwlab/record/avi.h"

#include <algorithm>
#include <cstring>

namespace hwlab::record {

namespace {

// OpenDML 1.02 규격의 값입니다.
constexpr uint32_t kAvifHasIndex = 0x10;
constexpr uint32_t kAviifKeyframe = 0x10;
constexpr uint8_t kIndexOfIndexes = 0x00;
constexpr uint8_t kIndexOfChunks = 0x01;
constexpr size_t kAvihBytes = 56;
constexpr size_t kStrhBytes = 56;
constexpr size_t kStrfBytes = 40;
constexpr size_t kDmlhBytes = 248;
constexpr size_t kSuperIndexHeaderBytes = 24;
constexpr size_t kSuperIndexEntryBytes = 16;
constexpr size_t kChunkIndexHeaderBytes = 24;
constexpr size_t kChunkIndexEntryBytes = 8;
constexpr size_t kIdx1EntryBytes = 16;

// AVI의 모든 수는 리틀 엔디언입니다. 호스트 바이트 순서와 관계없이 바이트 단위로 씁니다.
class ByteWriter {
 public:
  explicit ByteWriter(std::vector<uint8_t>& out) : out_(out) {}

  void U8(uint8_t v) { out_.push_back(v); }
  void U16(uint16_t v) {
    U8(static_cast<uint8_t>(v));
    U8(static_cast<uint8_t>(v >> 8));
  }
  void U32(uint32_t v) {
    U16(static_cast<uint16_t>(v));
    U16(static_cast<uint16_t>(v >> 16));
  }
  void U64(uint64_t v) {
    U32(static_cast<uint32_t>(v));
    U32(static_cast<uint32_t>(v >> 32));
  }
  void Tag(const char (&tag)[5]) { out_.insert(out_.end(), tag, tag + 4); }
  void Zeros(size_t n) { out_.insert(out_.end(), n, 0); }

 private:
  std::vector<uint8_t>& out_;
};

std::vector<uint8_t> U32Bytes(uint32_t v) {
  std::vector<uint8_t> bytes;
  ByteWriter(bytes).U32(v);
  return bytes;
}

size_t PayloadBytes(const VideoFormat& f) {
  if (f.width <= 0 || f.height <= 0 || f.frame_rate <= 0) {
    return 0;
  }
  const size_t pixels = static_cast<size_t>(f.width) * static_cast<size_t>(f.height);
  switch (f.format) {
    case PixelFormat::kUyvy:
      return pixels * 2;
    case PixelFormat::kNv12:
      // 색차가 2x2 화소마다 한 쌍이라 폭과 높이가 짝수여야 합니다.
      return (f.width % 2 == 0 && f.height % 2 == 0) ? pixels + pixels / 2 : 0;
    case PixelFormat::kY16:
      // 열화상 원시값은 AVI가 아니라 raw16 파일로 저장합니다.
      return 0;
  }
  return 0;
}

uint32_t CompressionOf(PixelFormat format) {
  std::vector<uint8_t> tag;
  ByteWriter w(tag);
  if (format == PixelFormat::kNv12) {
    w.Tag("NV12");
  } else {
    w.Tag("UYVY");
  }
  return static_cast<uint32_t>(tag[0]) | (static_cast<uint32_t>(tag[1]) << 8) |
         (static_cast<uint32_t>(tag[2]) << 16) | (static_cast<uint32_t>(tag[3]) << 24);
}

uint16_t BitsPerPixel(PixelFormat format) { return format == PixelFormat::kNv12 ? 12 : 16; }

size_t ChunkIndexBytes(int64_t frames) {
  return 8 + kChunkIndexHeaderBytes + kChunkIndexEntryBytes * static_cast<size_t>(frames);
}

}  // namespace

AviBuilder::AviBuilder(const VideoFormat& format, int64_t max_frames, uint64_t riff_limit_bytes)
    : format_(format), max_frames_(max_frames), riff_limit_bytes_(riff_limit_bytes) {
  const size_t payload = PayloadBytes(format);
  if (payload == 0 || max_frames <= 0) {
    return;
  }
  frame_bytes_ = payload;
  // 헤더 크기가 색인 자리 수에 따라 달라지고, 자리 수는 RIFF마다 담을 프레임 수에 따라 달라져 고정점까지 반복합니다.
  index_slots_ = 1;
  for (int i = 0; i < 64; ++i) {
    header_bytes_ = BuildHeader(0, 0).size();
    int64_t remaining = max_frames_ - CapacityOf(0);
    int32_t slots = 1;
    while (remaining > 0) {
      const int64_t capacity = CapacityOf(1);
      if (capacity <= 0) {
        frame_bytes_ = 0;
        return;
      }
      remaining -= capacity;
      ++slots;
    }
    if (CapacityOf(0) <= 0) {
      frame_bytes_ = 0;
      return;
    }
    if (slots == index_slots_) {
      return;
    }
    index_slots_ = slots;
  }
  frame_bytes_ = 0;
}

int64_t AviBuilder::CapacityOf(size_t segment) const {
  const uint64_t chunk = 8 + frame_bytes_;
  uint64_t fixed = 12 + 8 + kChunkIndexHeaderBytes;  // movi LIST, ix00 머리
  uint64_t per_frame = chunk + kChunkIndexEntryBytes;
  if (segment == 0) {
    fixed += header_bytes_ + 8;  // 헤더(RIFF 머리 포함), idx1 머리
    per_frame += kIdx1EntryBytes;
  } else {
    fixed += 12;  // RIFF AVIX 머리
  }
  if (riff_limit_bytes_ <= fixed) {
    return 0;
  }
  return static_cast<int64_t>((riff_limit_bytes_ - fixed) / per_frame);
}

std::vector<uint8_t> AviBuilder::BuildHeader(int64_t total_frames,
                                             int64_t first_riff_frames) const {
  const size_t indx_bytes =
      kSuperIndexHeaderBytes + kSuperIndexEntryBytes * static_cast<size_t>(index_slots_);
  const size_t strl_bytes = 4 + (8 + kStrhBytes) + (8 + kStrfBytes) + (8 + indx_bytes);
  const size_t odml_bytes = 4 + 8 + kDmlhBytes;
  const size_t hdrl_bytes = 4 + (8 + kAvihBytes) + (8 + strl_bytes) + (8 + odml_bytes);
  const uint32_t suggested = static_cast<uint32_t>(frame_bytes_ + 8);
  const uint32_t us_per_frame = static_cast<uint32_t>((1000000 + format_.frame_rate / 2) /
                                                      format_.frame_rate);

  std::vector<uint8_t> out;
  ByteWriter w(out);
  w.Tag("RIFF");
  w.U32(0);  // Finish에서 채웁니다
  w.Tag("AVI ");
  w.Tag("LIST");
  w.U32(static_cast<uint32_t>(hdrl_bytes));
  w.Tag("hdrl");

  w.Tag("avih");
  w.U32(kAvihBytes);
  w.U32(us_per_frame);
  w.U32(static_cast<uint32_t>(std::min<uint64_t>(uint64_t{suggested} * format_.frame_rate,
                                                 UINT32_MAX)));
  w.U32(0);
  w.U32(kAvifHasIndex);
  w.U32(static_cast<uint32_t>(first_riff_frames));  // OpenDML 규격: 첫 RIFF의 프레임 수
  w.U32(0);
  w.U32(1);
  w.U32(suggested);
  w.U32(static_cast<uint32_t>(format_.width));
  w.U32(static_cast<uint32_t>(format_.height));
  w.Zeros(16);

  w.Tag("LIST");
  w.U32(static_cast<uint32_t>(strl_bytes));
  w.Tag("strl");
  w.Tag("strh");
  w.U32(kStrhBytes);
  w.Tag("vids");
  w.U32(CompressionOf(format_.format));
  w.U32(0);  // flags
  w.U16(0);  // priority
  w.U16(0);  // language
  w.U32(0);  // initial frames
  w.U32(1);  // scale
  w.U32(static_cast<uint32_t>(format_.frame_rate));
  w.U32(0);  // start
  w.U32(static_cast<uint32_t>(total_frames));
  w.U32(suggested);
  w.U32(UINT32_MAX);  // quality: 기본값
  w.U32(0);           // sample size: 프레임마다 크기가 같아도 0을 씁니다
  w.U16(0);
  w.U16(0);
  w.U16(static_cast<uint16_t>(format_.width));
  w.U16(static_cast<uint16_t>(format_.height));

  w.Tag("strf");
  w.U32(kStrfBytes);
  w.U32(kStrfBytes);
  w.U32(static_cast<uint32_t>(format_.width));
  // YUV 형식은 높이가 양수여도 위쪽 행부터 저장합니다.
  w.U32(static_cast<uint32_t>(format_.height));
  w.U16(1);
  w.U16(BitsPerPixel(format_.format));
  w.U32(CompressionOf(format_.format));
  w.U32(static_cast<uint32_t>(frame_bytes_));
  w.Zeros(16);

  w.Tag("indx");
  w.U32(static_cast<uint32_t>(indx_bytes));
  w.U16(4);
  w.U8(0);
  w.U8(kIndexOfIndexes);
  w.U32(static_cast<uint32_t>(index_entries_.size()));
  w.Tag("00db");
  w.Zeros(12);
  for (int32_t i = 0; i < index_slots_; ++i) {
    if (static_cast<size_t>(i) < index_entries_.size()) {
      w.U64(index_entries_[i].offset);
      w.U32(index_entries_[i].bytes);
      w.U32(index_entries_[i].frames);
    } else {
      w.Zeros(kSuperIndexEntryBytes);
    }
  }

  w.Tag("LIST");
  w.U32(static_cast<uint32_t>(odml_bytes));
  w.Tag("odml");
  w.Tag("dmlh");
  w.U32(kDmlhBytes);
  w.U32(static_cast<uint32_t>(total_frames));
  w.Zeros(kDmlhBytes - 4);
  return out;
}

void AviBuilder::OpenSegment(AviOutput& out) {
  ByteWriter w(out.append);
  Segment segment;
  segment.first_frame = frames_written_;
  if (!segments_.empty()) {
    segment.riff_offset = position_ + out.append.size();
    w.Tag("RIFF");
    w.U32(0);
    w.Tag("AVIX");
  }
  segment.movi_offset = position_ + out.append.size();
  w.Tag("LIST");
  w.U32(0);
  w.Tag("movi");
  segments_.push_back(segment);
}

void AviBuilder::CloseSegment(AviOutput& out) {
  Segment& segment = segments_.back();
  const uint64_t chunk = 8 + frame_bytes_;
  const uint64_t movi_data_offset = segment.movi_offset + 8;  // 'movi' 태그 위치
  ByteWriter w(out.append);

  // 표준 색인(ix00)은 movi 안 마지막에 둡니다. 화소 위치는 기준 위치에서 센 바이트 수입니다.
  const uint64_t ix_offset = position_ + out.append.size();
  const uint64_t base = segment.movi_offset;
  w.Tag("ix00");
  w.U32(static_cast<uint32_t>(ChunkIndexBytes(segment.frames) - 8));
  w.U16(2);
  w.U8(0);
  w.U8(kIndexOfChunks);
  w.U32(static_cast<uint32_t>(segment.frames));
  w.Tag("00db");
  w.U64(base);
  w.U32(0);
  for (int64_t i = 0; i < segment.frames; ++i) {
    const uint64_t data = movi_data_offset + 4 + static_cast<uint64_t>(i) * chunk + 8;
    w.U32(static_cast<uint32_t>(data - base));
    w.U32(static_cast<uint32_t>(frame_bytes_));  // 최상위 비트 0: 키 프레임
  }
  index_entries_.push_back({ix_offset, static_cast<uint32_t>(ChunkIndexBytes(segment.frames)),
                            static_cast<uint32_t>(segment.frames)});

  const uint64_t movi_end = position_ + out.append.size();
  out.patches.push_back(
      {segment.movi_offset + 4, U32Bytes(static_cast<uint32_t>(movi_end - segment.movi_offset - 8))});

  if (segments_.size() == 1) {
    // OpenDML을 모르는 도구를 위해 첫 RIFF에 기존 색인(idx1)도 둡니다. 위치는 'movi' 태그에서 센 값입니다.
    w.Tag("idx1");
    w.U32(static_cast<uint32_t>(kIdx1EntryBytes * static_cast<size_t>(segment.frames)));
    for (int64_t i = 0; i < segment.frames; ++i) {
      w.Tag("00db");
      w.U32(kAviifKeyframe);
      w.U32(static_cast<uint32_t>(4 + static_cast<uint64_t>(i) * chunk));
      w.U32(static_cast<uint32_t>(frame_bytes_));
    }
  }
  const uint64_t riff_end = position_ + out.append.size();
  out.patches.push_back(
      {segment.riff_offset + 4, U32Bytes(static_cast<uint32_t>(riff_end - segment.riff_offset - 8))});
}

AviOutput AviBuilder::Start() {
  AviOutput out;
  if (!IsValid() || started_) {
    return out;
  }
  started_ = true;
  out.append = BuildHeader(0, 0);
  OpenSegment(out);
  position_ += out.append.size();
  return out;
}

std::optional<AviOutput> AviBuilder::BeginFrame() {
  if (!IsValid() || !started_ || finished_ || frames_written_ >= max_frames_) {
    return std::nullopt;
  }
  AviOutput out;
  if (segments_.back().frames >= CapacityOf(segments_.size() - 1)) {
    CloseSegment(out);
    OpenSegment(out);
  }
  ByteWriter w(out.append);
  w.Tag("00db");
  w.U32(static_cast<uint32_t>(frame_bytes_));
  ++segments_.back().frames;
  ++frames_written_;
  position_ += out.append.size() + frame_bytes_;
  return out;
}

AviOutput AviBuilder::Finish() {
  AviOutput out;
  if (!IsValid() || !started_ || finished_) {
    return out;
  }
  finished_ = true;
  CloseSegment(out);
  position_ += out.append.size();
  // 헤더 크기는 Start 때와 같으므로 RIFF 크기 자리만 남기고 덮어씁니다.
  std::vector<uint8_t> header = BuildHeader(frames_written_, segments_.front().frames);
  out.patches.push_back({8, std::vector<uint8_t>(header.begin() + 8, header.end())});
  return out;
}

}  // namespace hwlab::record
