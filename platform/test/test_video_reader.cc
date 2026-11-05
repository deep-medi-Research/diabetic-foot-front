#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hwlab/platform/fake_camera.h"
#include "hwlab/platform/video_reader.h"
#include "hwlab/platform/video_writer.h"

namespace {

using hwlab::platform::FakeCameraScript;
using hwlab::platform::FakeRgbCamera;
using hwlab::platform::MakeVideoReader;
using hwlab::platform::MakeVideoWriter;

constexpr int kFrames = 40;

// 가짜 카메라(화소마다 다른 값)의 프레임을 FFV1 MKV로 쓰고, 쓴 UYVY 바이트를 순서대로 돌려줍니다.
std::vector<std::vector<uint8_t>> WriteFakeMkv(const std::string& path) {
  FakeCameraScript script;
  script.width = 64;
  script.height = 32;
  script.initial = {false, -5, 4, false, 4600, 30, true};
  script.wall_period_us = 100;
  script.varied_pixels = true;
  FakeRgbCamera camera(script);
  std::string error;
  auto writer = MakeVideoWriter(hwlab::flow::VideoCodec::kFfv1, &error);
  EXPECT_NE(writer, nullptr) << error;
  if (writer == nullptr) return {};
  EXPECT_TRUE(writer->Open(path, {hwlab::PixelFormat::kUyvy, 64, 32, 30}, kFrames));
  std::vector<std::vector<uint8_t>> written;
  while (static_cast<int>(written.size()) < kFrames) {
    const auto frame = camera.ReadFrame();
    if (!frame) continue;
    std::vector<uint8_t> bytes(64 * 32 * 2);
    for (int r = 0; r < 32; ++r) {
      const uint8_t* row = frame->view.plane[0] + static_cast<ptrdiff_t>(r) * frame->view.row_stride[0];
      std::copy(row, row + 128, bytes.begin() + r * 128);
    }
    EXPECT_TRUE(writer->WriteFrame(bytes.data()));
    written.push_back(std::move(bytes));
  }
  EXPECT_TRUE(writer->Close());
  return written;
}

std::string TempMkv(const char* name) {
  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_video_reader";
  std::filesystem::create_directories(dir);
  return (dir / name).string();
}

// 번호로 읽은 프레임이 쓴 UYVY와 바이트 단위로 같습니다. 순서대로, 거꾸로, 건너뛰며 읽어도 같습니다.
TEST(VideoReader, ReadsFrameByNumberByteExact) {
  const std::string path = TempMkv("fake.mkv");
  const auto written = WriteFakeMkv(path);
  ASSERT_EQ(written.size(), static_cast<size_t>(kFrames));
  // 프레임마다 화소가 달라야 번호가 어긋난 것을 찾아냅니다.
  ASSERT_NE(written[0], written[1]);

  auto reader = MakeVideoReader();
  ASSERT_NE(reader, nullptr);
  std::string error;
  ASSERT_TRUE(reader->Open(path, &error)) << error;
  EXPECT_EQ(reader->Width(), 64);
  EXPECT_EQ(reader->Height(), 32);
  EXPECT_EQ(reader->FrameCount(), kFrames);

  std::vector<uint8_t> frame;
  std::vector<int64_t> order;
  for (int64_t i = 0; i < kFrames; ++i) order.push_back(i);
  for (int64_t i = kFrames - 1; i >= 0; --i) order.push_back(i);
  for (const int64_t i : {17, 3, 39, 0, 20, 21, 22, 5, 38}) order.push_back(i);
  for (const int64_t i : order) {
    ASSERT_TRUE(reader->ReadFrame(i, &frame)) << "frame " << i;
    ASSERT_EQ(frame, written[static_cast<size_t>(i)]) << "frame " << i;
  }
}

// 범위 밖 번호는 읽지 않고, 그 뒤에도 정상 번호를 읽습니다.
TEST(VideoReader, RejectsOutOfRangeIndex) {
  const std::string path = TempMkv("range.mkv");
  const auto written = WriteFakeMkv(path);
  ASSERT_EQ(written.size(), static_cast<size_t>(kFrames));
  auto reader = MakeVideoReader();
  ASSERT_NE(reader, nullptr);
  ASSERT_TRUE(reader->Open(path, nullptr));
  std::vector<uint8_t> frame;
  EXPECT_FALSE(reader->ReadFrame(-1, &frame));
  EXPECT_FALSE(reader->ReadFrame(kFrames, &frame));
  ASSERT_TRUE(reader->ReadFrame(kFrames - 1, &frame));
  EXPECT_EQ(frame, written.back());
}

// 없는 파일은 열지 못하고 이유를 남깁니다.
TEST(VideoReader, FailsOnMissingFile) {
  auto reader = MakeVideoReader();
  ASSERT_NE(reader, nullptr);
  std::string error;
  EXPECT_FALSE(reader->Open(TempMkv("missing.mkv"), &error));
  EXPECT_FALSE(error.empty());
  std::vector<uint8_t> frame;
  EXPECT_FALSE(reader->ReadFrame(0, &frame));
}

}  // namespace
