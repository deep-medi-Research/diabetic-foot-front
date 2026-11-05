#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/record/avi.h"

namespace {

using hwlab::PixelFormat;
using hwlab::record::AviBuilder;
using hwlab::record::AviOutput;
using hwlab::record::VideoFormat;

constexpr VideoFormat kSmallUyvy{PixelFormat::kUyvy, 8, 4, 30};

// platform이 파일에 하는 일을 메모리에서 그대로 합니다.
struct MemoryFile {
  std::vector<uint8_t> bytes;

  void Apply(const AviOutput& out) {
    bytes.insert(bytes.end(), out.append.begin(), out.append.end());
    for (const auto& patch : out.patches) {
      ASSERT_LE(patch.offset + patch.bytes.size(), bytes.size());
      std::memcpy(bytes.data() + patch.offset, patch.bytes.data(), patch.bytes.size());
    }
  }

  // 프레임 i의 모든 바이트를 i로 채워, 색인이 가리키는 화소가 몇 번째 프레임인지 알 수 있게 합니다.
  void AppendFrame(size_t frame_bytes, int64_t i) {
    bytes.insert(bytes.end(), frame_bytes, static_cast<uint8_t>(i));
  }

  uint32_t U32(uint64_t at) const {
    return bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) |
           (static_cast<uint32_t>(bytes[at + 3]) << 24);
  }
  uint64_t U64(uint64_t at) const { return U32(at) | (static_cast<uint64_t>(U32(at + 4)) << 32); }
  std::string Tag(uint64_t at) const {
    return std::string(reinterpret_cast<const char*>(&bytes[at]), 4);
  }
};

MemoryFile Record(const VideoFormat& format, int64_t max_frames, int64_t frames,
                  uint64_t riff_limit) {
  AviBuilder builder(format, max_frames, riff_limit);
  MemoryFile file;
  file.Apply(builder.Start());
  for (int64_t i = 0; i < frames; ++i) {
    const std::optional<AviOutput> out = builder.BeginFrame();
    if (!out) {
      break;
    }
    file.Apply(*out);
    file.AppendFrame(builder.FrameBytes(), i);
  }
  file.Apply(builder.Finish());
  return file;
}

// 최상위 RIFF 묶음의 위치를 차례로 찾습니다.
std::vector<uint64_t> RiffOffsets(const MemoryFile& file) {
  std::vector<uint64_t> offsets;
  uint64_t at = 0;
  while (at + 8 <= file.bytes.size()) {
    EXPECT_EQ(file.Tag(at), "RIFF");
    offsets.push_back(at);
    at += 8 + file.U32(at + 4);
  }
  EXPECT_EQ(at, file.bytes.size());
  return offsets;
}

// 묶음 안에서 tag 청크의 위치를 찾습니다. LIST는 안으로 들어가 찾습니다.
std::optional<uint64_t> FindChunk(const MemoryFile& file, uint64_t begin, uint64_t end,
                                  const std::string& tag) {
  uint64_t at = begin;
  while (at + 8 <= end) {
    const std::string id = file.Tag(at);
    const uint32_t size = file.U32(at + 4);
    if (id == tag) {
      return at;
    }
    if (id == "LIST") {
      if (auto inner = FindChunk(file, at + 12, at + 8 + size, tag)) {
        return inner;
      }
    }
    at += 8 + size + (size % 2);
  }
  return std::nullopt;
}

// 헤더에 크기, 형식, 프레임 수가 맞게 들어갑니다.
TEST(Avi, WritesHeaderFields) {
  const MemoryFile file = Record(kSmallUyvy, 10, 3, AviBuilder::kDefaultRiffLimitBytes);

  ASSERT_EQ(RiffOffsets(file).size(), 1u);
  EXPECT_EQ(file.Tag(8), "AVI ");
  const auto avih = FindChunk(file, 12, file.bytes.size(), "avih");
  ASSERT_TRUE(avih);
  EXPECT_EQ(file.U32(*avih + 8), 33333u);       // 프레임 간격(us)
  EXPECT_EQ(file.U32(*avih + 8 + 16), 3u);      // 첫 RIFF 프레임 수
  EXPECT_EQ(file.U32(*avih + 8 + 32), 8u);      // 폭
  EXPECT_EQ(file.U32(*avih + 8 + 36), 4u);      // 높이
  const auto strf = FindChunk(file, 12, file.bytes.size(), "strf");
  ASSERT_TRUE(strf);
  EXPECT_EQ(file.Tag(*strf + 8 + 16), "UYVY");
  const auto dmlh = FindChunk(file, 12, file.bytes.size(), "dmlh");
  ASSERT_TRUE(dmlh);
  EXPECT_EQ(file.U32(*dmlh + 8), 3u);
}

// 기존 색인(idx1)의 각 항목이 해당 프레임의 화소를 가리킵니다.
TEST(Avi, Idx1PointsToEachFrame) {
  const MemoryFile file = Record(kSmallUyvy, 10, 3, AviBuilder::kDefaultRiffLimitBytes);
  const auto movi = FindChunk(file, 12, file.bytes.size(), "LIST");  // hdrl
  const auto idx1 = FindChunk(file, 12, file.bytes.size(), "idx1");
  ASSERT_TRUE(movi && idx1);
  // movi 리스트는 hdrl 다음 LIST입니다.
  const uint64_t movi_list = *movi + 8 + file.U32(*movi + 4);
  ASSERT_EQ(file.Tag(movi_list + 8), "movi");

  ASSERT_EQ(file.U32(*idx1 + 4), 3u * 16);
  for (uint32_t i = 0; i < 3; ++i) {
    const uint64_t entry = *idx1 + 8 + i * 16;
    EXPECT_EQ(file.Tag(entry), "00db");
    const uint64_t chunk = movi_list + 8 + file.U32(entry + 8);
    EXPECT_EQ(file.Tag(chunk), "00db");
    EXPECT_EQ(file.bytes[chunk + 8], i);
  }
}

// RIFF 크기 한도를 넘으면 AVIX로 나누고, 상위 색인이 모든 구간의 ix00을 가리킵니다.
TEST(Avi, SplitsIntoAvixAndIndexesEveryFrame) {
  // 프레임 하나가 64바이트라 한도 1000바이트에서 RIFF마다 몇 장만 들어갑니다.
  const MemoryFile file = Record(kSmallUyvy, 40, 40, 1000);

  const auto riffs = RiffOffsets(file);
  ASSERT_GT(riffs.size(), 2u);
  for (size_t i = 0; i < riffs.size(); ++i) {
    EXPECT_LE(file.U32(riffs[i] + 4) + 8u, 1000u);
    EXPECT_EQ(file.Tag(riffs[i] + 8), i == 0 ? "AVI " : "AVIX");
  }

  const auto indx = FindChunk(file, 12, file.U32(4) + 8, "indx");
  ASSERT_TRUE(indx);
  const uint32_t in_use = file.U32(*indx + 8 + 4);
  ASSERT_EQ(in_use, riffs.size());
  int64_t next_frame = 0;
  for (uint32_t s = 0; s < in_use; ++s) {
    const uint64_t entry = *indx + 8 + 24 + s * 16;
    const uint64_t ix = file.U64(entry);
    ASSERT_EQ(file.Tag(ix), "ix00");
    const uint32_t frames = file.U32(ix + 8 + 4);
    EXPECT_EQ(file.U32(entry + 12), frames);
    const uint64_t base = file.U64(ix + 8 + 12);
    for (uint32_t f = 0; f < frames; ++f) {
      const uint64_t data = base + file.U32(ix + 8 + 24 + f * 8);
      EXPECT_EQ(file.bytes[data], static_cast<uint8_t>(next_frame));
      EXPECT_EQ(file.Tag(data - 8), "00db");
      ++next_frame;
    }
  }
  EXPECT_EQ(next_frame, 40);
  const auto dmlh = FindChunk(file, 12, file.U32(4) + 8, "dmlh");
  EXPECT_EQ(file.U32(*dmlh + 8), 40u);
}

// max_frames를 다 쓰면 더 받지 않습니다.
TEST(Avi, RefusesFramesBeyondMax) {
  AviBuilder builder(kSmallUyvy, 2);
  builder.Start();

  EXPECT_TRUE(builder.BeginFrame());
  EXPECT_TRUE(builder.BeginFrame());
  EXPECT_FALSE(builder.BeginFrame());
  EXPECT_EQ(builder.FramesWritten(), 2);
}

// 목표보다 일찍 멈춰도(중지 버튼, 연결 끊김) 크기와 프레임 수가 맞는 파일이 됩니다.
TEST(Avi, StopsEarlyWithConsistentSizes) {
  const MemoryFile file = Record(kSmallUyvy, 900, 5, 5000);

  ASSERT_FALSE(file.bytes.empty());
  RiffOffsets(file);
  const auto dmlh = FindChunk(file, 12, file.U32(4) + 8, "dmlh");
  ASSERT_TRUE(dmlh);
  EXPECT_EQ(file.U32(*dmlh + 8), 5u);
}

// 1080p UYVY 900장은 1GiB 한도에서 RIFF 4개로 나뉩니다.
TEST(Avi, PlansFullHdNineHundredFrames) {
  AviBuilder builder({PixelFormat::kUyvy, 1920, 1080, 30}, 900);

  ASSERT_TRUE(builder.IsValid());
  EXPECT_EQ(builder.FrameBytes(), 1920u * 1080 * 2);
}

// 열화상 원시값과 잘못된 크기는 거부합니다.
TEST(Avi, RejectsUnsupportedFormats) {
  EXPECT_FALSE(AviBuilder({PixelFormat::kY16, 640, 480, 30}, 900).IsValid());
  EXPECT_FALSE(AviBuilder({PixelFormat::kNv12, 7, 4, 30}, 900).IsValid());
  EXPECT_FALSE(AviBuilder({PixelFormat::kUyvy, 8, 4, 0}, 900).IsValid());
  EXPECT_FALSE(AviBuilder(kSmallUyvy, 900, 100).IsValid());
  // 900장 분량의 색인 자리를 잡으면 헤더가 한도 1000바이트를 넘습니다.
  EXPECT_FALSE(AviBuilder(kSmallUyvy, 900, 1000).IsValid());
}

}  // namespace
