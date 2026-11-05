#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "hwlab/pixel/crop.h"
#include "hwlab/pixel/rect.h"
#include "hwlab/platform/capture.h"
#include "hwlab/platform/fake_camera.h"

#ifdef HWLAB_HAS_FFV1
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#endif

namespace {

using namespace std::chrono_literals;
using hwlab::CameraState;
using hwlab::PixelFormat;
using hwlab::SettingMode;
using hwlab::flow::CaptureSpec;
using hwlab::flow::EndReason;
using hwlab::pixel::Rect;
using hwlab::platform::Capture;
using hwlab::platform::CaptureStatus;
using hwlab::platform::CropOutput;
using hwlab::platform::FakeCameraScript;
using hwlab::platform::FakeRgbCamera;
using hwlab::platform::RecordingPaths;
using hwlab::record::FootSide;

constexpr int32_t kWidth = 64;
constexpr int32_t kHeight = 48;
const CameraState kInitial{false, -5, 4, false, 4600, 60, true};

CaptureSpec SplitSpec(hwlab::flow::VideoCodec codec, int64_t target_frames) {
  CaptureSpec spec;
  spec.device = "Fake";
  spec.width = kWidth;
  spec.height = kHeight;
  spec.settings = {SettingMode::kManual, -6, 8, 4600, 30};
  spec.settle = {4, 3, 3, 10};
  spec.target_frames = target_frames;
  spec.clip_level = 250;
  spec.buffer_frames = 64;
  spec.codec = codec;
  return spec;
}

FakeCameraScript VariedScript() {
  FakeCameraScript script{.initial = kInitial};
  script.width = kWidth;
  script.height = kHeight;
  script.varied_pixels = true;
  return script;
}

std::string TempPath(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_capture_split";
  std::filesystem::create_directories(dir);
  const std::string path = (dir / name).string();
  std::filesystem::remove(path);
  return path;
}

// 전체 프레임을 가운데에서 나눈 좌우 영역과 그 파일입니다. 화면 왼쪽을 왼발로 둡니다.
RecordingPaths SplitPaths(const std::string& name, const char* extension, bool whole) {
  const auto [left, right] = hwlab::pixel::SplitLeftRight({0, 0, kWidth, kHeight}, PixelFormat::kUyvy, 2);
  RecordingPaths paths;
  if (whole) {
    paths.video = TempPath(name + "." + extension);
    paths.sidecar = TempPath(name + ".json");
  }
  paths.crops.push_back({left, TempPath(name + "_left." + extension), TempPath(name + "_left.json"), FootSide::kLeft});
  paths.crops.push_back(
      {right, TempPath(name + "_right." + extension), TempPath(name + "_right.json"), FootSide::kRight});
  return paths;
}

std::string ReadText(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

std::vector<int64_t> IntArray(const std::string& json, const std::string& key) {
  std::vector<int64_t> values;
  const size_t at = json.find("\"" + key + "\": [");
  if (at == std::string::npos) return values;
  const size_t open = json.find('[', at);
  std::stringstream items(json.substr(open + 1, json.find(']', at) - open - 1));
  std::string item;
  while (std::getline(items, item, ',')) {
    if (item.find_first_not_of(' ') != std::string::npos) values.push_back(std::stoll(item));
  }
  return values;
}

CaptureStatus Record(Capture& capture, const RecordingPaths& paths) {
  while (capture.Status().received < 5) std::this_thread::sleep_for(1ms);
  capture.StartRecording(paths);
  EXPECT_TRUE(capture.WaitFinished(60s));
  return capture.Status();
}

// 좌우 영역은 세로로 긴 영상이고, 영역마다 사이드카에 영역과 발을 남깁니다.
TEST(CaptureSplit, WritesCropSidecarsWithFootAndRect) {
  FakeRgbCamera camera(VariedScript());
  const RecordingPaths paths = SplitPaths("sidecar", "avi", true);
  ASSERT_TRUE(hwlab::pixel::IsPortrait(paths.crops[0].rect));
  ASSERT_TRUE(hwlab::pixel::IsPortrait(paths.crops[1].rect));
  Capture capture(camera, SplitSpec(hwlab::flow::VideoCodec::kUncompressed, 60));
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;

  const CaptureStatus status = Record(capture, paths);
  capture.Stop();

  EXPECT_EQ(status.end_reason, EndReason::kCompleted);
  EXPECT_EQ(status.written, 60);
  EXPECT_TRUE(status.error.empty()) << status.error;
  const std::string left = ReadText(paths.crops[0].sidecar);
  const std::string right = ReadText(paths.crops[1].sidecar);
  EXPECT_NE(left.find("\"foot\": \"left\""), std::string::npos);
  EXPECT_NE(right.find("\"foot\": \"right\""), std::string::npos);
  EXPECT_NE(left.find("\"crop\": {\n    \"x\": 0,\n    \"y\": 0,\n    \"width\": 32,\n    \"height\": 48"),
            std::string::npos)
      << left;
  EXPECT_NE(right.find("\"crop\": {\n    \"x\": 32,"), std::string::npos) << right;
  EXPECT_NE(left.find("\"written\": 60"), std::string::npos);
  EXPECT_NE(left.find("\"brightness_pixels\": 1536"), std::string::npos);  // 32 * 48
  EXPECT_NE(ReadText(paths.sidecar).find("\"foot\": null"), std::string::npos);
  // 시각 기록은 세 파일이 같습니다.
  EXPECT_EQ(IntArray(left, "device_time_us"), IntArray(ReadText(paths.sidecar), "device_time_us"));
  EXPECT_EQ(IntArray(left, "device_time_us"), IntArray(right, "device_time_us"));
}

// 전체 화면 경로가 비어 있으면 좌우 영상만 씁니다.
TEST(CaptureSplit, SkipsWholeVideoWhenPathIsEmpty) {
  FakeRgbCamera camera(VariedScript());
  const RecordingPaths paths = SplitPaths("crops_only", "avi", false);
  Capture capture(camera, SplitSpec(hwlab::flow::VideoCodec::kUncompressed, 30));
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;

  const CaptureStatus status = Record(capture, paths);
  capture.Stop();

  EXPECT_EQ(status.end_reason, EndReason::kCompleted);
  EXPECT_TRUE(std::filesystem::exists(paths.crops[0].video));
  EXPECT_TRUE(std::filesystem::exists(paths.crops[1].sidecar));
  EXPECT_FALSE(std::filesystem::exists(TempPath("crops_only.avi")));
}

// 색차 격자에 맞지 않거나 프레임 밖인 영역은 녹화를 시작하지 않고 이유를 남깁니다.
TEST(CaptureSplit, RejectsMisalignedOrOutsideCrop) {
  for (const Rect rect : {Rect{1, 0, 32, 48}, Rect{40, 0, 32, 48}}) {
    FakeRgbCamera camera(VariedScript());
    RecordingPaths paths = SplitPaths("bad", "avi", false);
    paths.crops[1].rect = rect;
    Capture capture(camera, SplitSpec(hwlab::flow::VideoCodec::kUncompressed, 30));
    std::string error;
    ASSERT_TRUE(capture.Start(&error)) << error;

    const CaptureStatus status = Record(capture, paths);
    capture.Stop();

    EXPECT_FALSE(status.end_reason.has_value());
    EXPECT_NE(status.error.find("영역"), std::string::npos) << status.error;
    EXPECT_EQ(status.written, 0);
    EXPECT_FALSE(std::filesystem::exists(paths.crops[0].video));
  }
}

#ifdef HWLAB_HAS_FFV1
// MKV(FFV1)를 풀어 UYVY 바이트로 되돌린 프레임 수와 각 프레임이 원본 프레임의 영역 바이트와 같은지 셉니다.
struct Decoded {
  int frames = 0;
  int64_t mismatched_bytes = 0;
};

Decoded DecodeAndCompare(const std::string& path, const Rect& rect, const std::vector<int64_t>& index) {
  Decoded result;
  AVFormatContext* input = nullptr;
  if (avformat_open_input(&input, path.c_str(), nullptr, nullptr) != 0) return result;
  avformat_find_stream_info(input, nullptr);
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_FFV1);
  AVCodecContext* decoder = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(decoder, input->streams[0]->codecpar);
  avcodec_open2(decoder, codec, nullptr);
  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  const auto take = [&] {
    while (avcodec_receive_frame(decoder, frame) == 0) {
      const int64_t source = static_cast<size_t>(result.frames) < index.size() ? index[result.frames] : -1;
      for (int32_t r = 0; r < rect.height; ++r) {
        for (int32_t b = 0; b < rect.width * 2; ++b) {
          const int32_t pair = b / 4;
          uint8_t got = 0;
          switch (b % 4) {
            case 0: got = frame->data[1][r * frame->linesize[1] + pair]; break;
            case 1: got = frame->data[0][r * frame->linesize[0] + pair * 2]; break;
            case 2: got = frame->data[2][r * frame->linesize[2] + pair]; break;
            default: got = frame->data[0][r * frame->linesize[0] + pair * 2 + 1]; break;
          }
          const uint8_t want = FakeRgbCamera::PatternByte(source, rect.x * 2 + b, rect.y + r);
          result.mismatched_bytes += got != want ? 1 : 0;
        }
      }
      ++result.frames;
    }
  };
  while (av_read_frame(input, packet) >= 0) {
    avcodec_send_packet(decoder, packet);
    av_packet_unref(packet);
    take();
  }
  avcodec_send_packet(decoder, nullptr);
  take();
  av_frame_free(&frame);
  av_packet_free(&packet);
  avcodec_free_context(&decoder);
  avformat_close_input(&input);
  return result;
}

// 좌우 FFV1 MKV를 900장씩 쓰고, 풀어 낸 화소는 원본 프레임의 같은 영역과 바이트 단위로 같습니다.
TEST(CaptureSplit, Ffv1CropsHoldNineHundredFramesMatchingSource) {
  FakeRgbCamera camera(VariedScript());
  const RecordingPaths paths = SplitPaths("ffv1", "mkv", true);
  Capture capture(camera, SplitSpec(hwlab::flow::VideoCodec::kFfv1, 900));
  std::string error;
  ASSERT_TRUE(capture.Start(&error)) << error;

  const CaptureStatus status = Record(capture, paths);
  capture.Stop();

  ASSERT_EQ(status.end_reason, EndReason::kCompleted);
  ASSERT_TRUE(status.error.empty()) << status.error;
  EXPECT_EQ(status.dropped, 0);
  for (const CropOutput& crop : paths.crops) {
    const std::vector<int64_t> index = IntArray(ReadText(crop.sidecar), "index");
    ASSERT_EQ(index.size(), 900u);
    const Decoded decoded = DecodeAndCompare(crop.video, crop.rect, index);
    EXPECT_EQ(decoded.frames, 900) << crop.video;
    EXPECT_EQ(decoded.mismatched_bytes, 0) << crop.video;
  }
  const Decoded whole = DecodeAndCompare(paths.video, {0, 0, kWidth, kHeight}, IntArray(ReadText(paths.sidecar), "index"));
  EXPECT_EQ(whole.frames, 900);
  EXPECT_EQ(whole.mismatched_bytes, 0);
}
#endif

}  // namespace
