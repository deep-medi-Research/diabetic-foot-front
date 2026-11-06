#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/pixel/convert.h"
#include "hwlab/platform/fake_thermal_camera.h"
#include "hwlab/platform/thermal_writer.h"

namespace {

using hwlab::FrameView;
using hwlab::PixelFormat;
using hwlab::StreamKind;
using hwlab::Timestamp;
using hwlab::pixel::Rect;
using hwlab::pixel::Y16Range;
using hwlab::platform::FakeThermalCamera;
using hwlab::platform::FakeThermalScript;
using hwlab::platform::JudgeThermalConnection;
using hwlab::platform::Raw16File;
using hwlab::platform::ReceivedFrame;
using hwlab::platform::ThermalConnection;
using hwlab::platform::ThermalLossRule;
using hwlab::platform::ThermalPreviewFile;
using hwlab::platform::WriteNpy;
using hwlab::platform::WriteRaw16Crop;

constexpr int32_t kWidth = 640;
constexpr int32_t kHeight = 480;
constexpr size_t kFrameBytes = static_cast<size_t>(kWidth) * kHeight * 2;
constexpr int64_t kSecondUs = 1'000'000;

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

uint32_t U32(const std::vector<uint8_t>& b, uint64_t at) {
  return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}

std::string Tag(const std::vector<uint8_t>& b, uint64_t at) {
  return std::string(reinterpret_cast<const char*>(&b[at]), 4);
}

// 묶음 안에서 tag 청크의 위치를 찾습니다. LIST는 안으로 들어가 찾습니다.
std::optional<uint64_t> FindChunk(const std::vector<uint8_t>& b, uint64_t begin, uint64_t end, const std::string& tag) {
  uint64_t at = begin;
  while (at + 8 <= end) {
    const uint32_t size = U32(b, at + 4);
    if (Tag(b, at) == tag) return at;
    if (Tag(b, at) == "LIST") {
      if (auto inner = FindChunk(b, at + 12, at + 8 + size, tag)) return inner;
    }
    at += 8 + size + (size % 2);
  }
  return std::nullopt;
}

uint16_t Y16At(const FrameView& frame, int32_t x, int32_t y) {
  const uint8_t* p = frame.plane[0] + static_cast<size_t>(y) * frame.row_stride[0] + static_cast<size_t>(x) * 2;
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

// pixel::Y16ToGray8과 같은 식을 따로 적어 변환 결과를 대조합니다.
uint8_t ExpectedGray(uint16_t v, Y16Range range) {
  const int32_t span = range.high - range.low;
  if (span <= 0) return 128;
  const int32_t c = std::clamp<int32_t>(v, range.low, range.high) - range.low;
  return static_cast<uint8_t>((c * 255 + span / 2) / span);
}

FakeThermalScript FastScript() {
  FakeThermalScript script;
  script.wall_period_us = 0;
  return script;
}

// 큰 시험 파일을 테스트마다 지웁니다.
class ThermalFiles : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("hwlab_test_thermal_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::filesystem::remove_all(dir_);
    std::filesystem::create_directories(dir_);
  }
  void TearDown() override { std::filesystem::remove_all(dir_); }
  std::string PathOf(const char* name) const { return (dir_ / name).string(); }

  // 가짜 열화상에서 frames장을 받아 원시값 파일로 쓰고, 받은 바이트를 이어 붙여 돌려줍니다.
  std::vector<uint8_t> RecordRaw(const std::string& path, int frames) {
    FakeThermalCamera camera(FastScript());
    EXPECT_TRUE(camera.Open(nullptr));
    Raw16File raw;
    EXPECT_TRUE(raw.Open(path, kWidth, kHeight));
    std::vector<uint8_t> received;
    for (int i = 0; i < frames; ++i) {
      const std::optional<ReceivedFrame> frame = camera.ReadFrame(kSecondUs);
      EXPECT_TRUE(frame);
      if (!frame) break;
      received.insert(received.end(), frame->view.plane[0], frame->view.plane[0] + kFrameBytes);
      EXPECT_TRUE(raw.WriteFrame(frame->view));
    }
    EXPECT_TRUE(raw.Close());
    return received;
  }

  std::filesystem::path dir_;
};

// 640x480 Y16을 원시값 파일로 쓰고 다시 읽으면 받은 바이트와 같습니다.
TEST_F(ThermalFiles, Raw16RoundTripMatchesReceivedBytes) {
  const std::string path = PathOf("thermal.raw16");
  const std::vector<uint8_t> received = RecordRaw(path, 5);
  ASSERT_EQ(received.size(), 5 * kFrameBytes);
  const std::vector<uint8_t> stored = ReadBytes(path);
  ASSERT_EQ(stored.size(), received.size());
  EXPECT_EQ(std::memcmp(stored.data(), received.data(), stored.size()), 0);
  // 리틀 엔디언으로 저장했는지 가짜 열화상의 화소 값으로 확인합니다.
  const uint16_t v = FakeThermalCamera::PixelValue(2, 7, 3);
  const size_t at = 2 * kFrameBytes + (3 * kWidth + 7) * 2;
  EXPECT_EQ(stored[at], v & 0xFF);
  EXPECT_EQ(stored[at + 1], v >> 8);
}

// 행 사이에 빈 바이트가 있는 프레임은 빈 바이트를 빼고 저장합니다. 크기가 다른 프레임은 거부합니다.
TEST_F(ThermalFiles, Raw16DropsRowPaddingAndRejectsOtherSize) {
  std::vector<uint8_t> padded(2 * 8, 0xEE);  // 폭 2, 높이 2, 행 간격 8
  const uint8_t rows[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
  std::memcpy(padded.data(), rows[0], 4);
  std::memcpy(padded.data() + 8, rows[1], 4);
  FrameView frame;
  frame.format = PixelFormat::kY16;
  frame.width = 2;
  frame.height = 2;
  frame.plane[0] = padded.data();
  frame.row_stride[0] = 8;
  frame.stream = StreamKind::kThermal;

  const std::string path = PathOf("padded.raw16");
  Raw16File raw;
  ASSERT_TRUE(raw.Open(path, 2, 2));
  EXPECT_TRUE(raw.WriteFrame(frame));
  FrameView wrong = frame;
  wrong.width = 4;
  EXPECT_FALSE(raw.WriteFrame(wrong));
  EXPECT_EQ(raw.FramesWritten(), 1);
  ASSERT_TRUE(raw.Close());
  EXPECT_EQ(ReadBytes(path), (std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}));
}

// 확인용 AVI는 쓴 프레임 수를 갖고, 화소는 첫 프레임에서 정한 범위로 편 회색입니다. 범위는 녹화 동안 바뀌지 않습니다.
TEST_F(ThermalFiles, PreviewAviKeepsFirstFrameRange) {
  constexpr int kFrames = 5;
  FakeThermalCamera camera(FastScript());
  ASSERT_TRUE(camera.Open(nullptr));
  const std::string path = PathOf("thermal.avi");
  ThermalPreviewFile preview;
  ASSERT_TRUE(preview.Open(path, kWidth, kHeight, 30, 100, std::nullopt));
  EXPECT_FALSE(preview.Range());
  std::vector<std::vector<uint16_t>> values;
  std::optional<Y16Range> first_range;
  for (int i = 0; i < kFrames; ++i) {
    const std::optional<ReceivedFrame> frame = camera.ReadFrame(kSecondUs);
    ASSERT_TRUE(frame);
    if (i == 0) first_range = hwlab::pixel::FindRange(frame->view, {0, 0, kWidth, kHeight});
    values.emplace_back();
    for (int32_t y = 0; y < kHeight; ++y) {
      for (int32_t x = 0; x < kWidth; ++x) values.back().push_back(Y16At(frame->view, x, y));
    }
    ASSERT_TRUE(preview.WriteFrame(frame->view));
  }
  ASSERT_TRUE(preview.Close());
  ASSERT_TRUE(preview.Range());
  EXPECT_EQ(preview.Range()->low, first_range->low);
  EXPECT_EQ(preview.Range()->high, first_range->high);
  EXPECT_EQ(preview.FramesWritten(), kFrames);

  const std::vector<uint8_t> avi = ReadBytes(path);
  ASSERT_GT(avi.size(), static_cast<size_t>(kFrames) * kFrameBytes);
  const auto avih = FindChunk(avi, 12, avi.size(), "avih");
  ASSERT_TRUE(avih);
  EXPECT_EQ(U32(avi, *avih + 8 + 16), static_cast<uint32_t>(kFrames));  // 프레임 수
  EXPECT_EQ(U32(avi, *avih + 8 + 32), static_cast<uint32_t>(kWidth));
  EXPECT_EQ(U32(avi, *avih + 8 + 36), static_cast<uint32_t>(kHeight));

  const auto hdrl = FindChunk(avi, 12, avi.size(), "LIST");
  const auto idx1 = FindChunk(avi, 12, avi.size(), "idx1");
  ASSERT_TRUE(hdrl && idx1);
  const uint64_t movi = *hdrl + 8 + U32(avi, *hdrl + 4);
  ASSERT_EQ(Tag(avi, movi + 8), "movi");
  ASSERT_EQ(U32(avi, *idx1 + 4), static_cast<uint32_t>(kFrames) * 16);
  bool saw_clipped = false;
  for (int i = 0; i < kFrames; ++i) {
    const uint64_t chunk = movi + 8 + U32(avi, *idx1 + 8 + i * 16 + 8);
    ASSERT_EQ(U32(avi, chunk + 4), kFrameBytes);  // UYVY도 화소당 2바이트
    const uint8_t* pixels = &avi[chunk + 8];
    for (size_t p = 0; p < values[i].size(); ++p) {
      ASSERT_EQ(pixels[p * 2], 128) << "frame " << i << " pixel " << p;
      ASSERT_EQ(pixels[p * 2 + 1], ExpectedGray(values[i][p], *first_range)) << "frame " << i << " pixel " << p;
      saw_clipped = saw_clipped || values[i][p] > first_range->high;
    }
  }
  // 뒤 프레임에 첫 범위를 넘는 값이 있어야 범위를 고정했는지 확인됩니다.
  EXPECT_TRUE(saw_clipped);
}

// 표시 범위를 주면 그 값을 그대로 씁니다. 폭이 홀수이면 열지 않습니다.
TEST_F(ThermalFiles, PreviewUsesGivenRangeAndRejectsOddWidth) {
  ThermalPreviewFile preview;
  ASSERT_TRUE(preview.Open(PathOf("given.avi"), kWidth, kHeight, 30, 10, Y16Range{7500, 8000}));
  ASSERT_TRUE(preview.Range());
  EXPECT_EQ(preview.Range()->low, 7500);
  EXPECT_EQ(preview.Range()->high, 8000);
  EXPECT_TRUE(preview.Close());
  ThermalPreviewFile odd;
  EXPECT_FALSE(odd.Open(PathOf("odd.avi"), 641, kHeight, 30, 10, std::nullopt));
}

// .npy는 NumPy 1.0 머리글(64바이트 정렬, '<u2', 모양) 뒤에 원시값 파일과 같은 바이트가 옵니다.
TEST_F(ThermalFiles, NpyHasHeaderAndRawBytes) {
  const std::string raw_path = PathOf("thermal.raw16");
  const std::vector<uint8_t> received = RecordRaw(raw_path, 3);
  std::string error;
  const std::optional<int64_t> frames =
      WriteNpy(raw_path, kWidth, kHeight, {0, 0, kWidth, kHeight}, PathOf("thermal.npy"), &error);
  ASSERT_TRUE(frames) << error;
  EXPECT_EQ(*frames, 3);

  const std::vector<uint8_t> npy = ReadBytes(PathOf("thermal.npy"));
  ASSERT_GT(npy.size(), 10u);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(npy.data()), 8), std::string("\x93NUMPY\x01\x00", 8));
  const size_t header_bytes = npy[8] | (npy[9] << 8);
  const size_t data_at = 10 + header_bytes;
  EXPECT_EQ(data_at % 64, 0u);
  const std::string dict(reinterpret_cast<const char*>(&npy[10]), header_bytes);
  EXPECT_EQ(dict.rfind("{'descr': '<u2', 'fortran_order': False, 'shape': (3, 480, 640), }", 0), 0u) << dict;
  EXPECT_EQ(dict.back(), '\n');
  ASSERT_EQ(npy.size() - data_at, received.size());
  EXPECT_EQ(std::memcmp(&npy[data_at], received.data(), received.size()), 0);
}

// 영역을 자른 .npy와 원시값 파일은 영역 크기의 모양이고 화소는 원본 영역과 같습니다.
TEST_F(ThermalFiles, CropWritesRegionToNpyAndRaw16) {
  const std::string raw_path = PathOf("thermal.raw16");
  RecordRaw(raw_path, 2);
  const Rect left{10, 20, 100, 50};
  std::string error;
  ASSERT_TRUE(WriteNpy(raw_path, kWidth, kHeight, left, PathOf("thermal_left.npy"), &error)) << error;
  ASSERT_TRUE(WriteRaw16Crop(raw_path, kWidth, kHeight, left, PathOf("thermal_left.raw16"), &error)) << error;

  const std::vector<uint8_t> npy = ReadBytes(PathOf("thermal_left.npy"));
  const size_t data_at = 10 + (npy[8] | (npy[9] << 8));
  const std::string dict(reinterpret_cast<const char*>(&npy[10]), data_at - 10);
  EXPECT_NE(dict.find("'shape': (2, 50, 100)"), std::string::npos) << dict;
  const std::vector<uint8_t> crop(npy.begin() + static_cast<std::ptrdiff_t>(data_at), npy.end());
  EXPECT_EQ(crop, ReadBytes(PathOf("thermal_left.raw16")));
  ASSERT_EQ(crop.size(), 2u * 100 * 50 * 2);
  // 가짜 열화상은 첫 Open 뒤 장치 프레임 0부터 냅니다.
  for (int f = 0; f < 2; ++f) {
    for (int32_t y = 0; y < left.height; ++y) {
      for (int32_t x = 0; x < left.width; ++x) {
        const size_t at = ((static_cast<size_t>(f) * left.height + y) * left.width + x) * 2;
        const uint16_t v = static_cast<uint16_t>(crop[at] | (crop[at + 1] << 8));
        ASSERT_EQ(v, FakeThermalCamera::PixelValue(f, left.x + x, left.y + y));
      }
    }
  }
}

// 원시값 파일 크기가 프레임의 배수가 아니거나 영역이 프레임 밖이면 쓰지 않습니다.
TEST_F(ThermalFiles, NpyRejectsBadSizeAndRegion) {
  const std::string raw_path = PathOf("short.raw16");
  {
    std::ofstream file(raw_path, std::ios::binary);
    const std::vector<char> bytes(kFrameBytes + 2, 0);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  std::string error;
  EXPECT_FALSE(WriteNpy(raw_path, kWidth, kHeight, {0, 0, kWidth, kHeight}, PathOf("a.npy"), &error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(WriteNpy(raw_path, kWidth, kHeight, {600, 0, 100, 10}, PathOf("b.npy"), &error));
  EXPECT_FALSE(WriteNpy(PathOf("missing.raw16"), kWidth, kHeight, {0, 0, kWidth, kHeight}, PathOf("c.npy"), &error));
}

// 누락 시나리오: 누락된 장만큼 수신 시각이 벌어지고 화소는 누락 뒤의 장치 프레임 것입니다. 받은 순서 번호에는 빠진 번호가 없습니다.
TEST(FakeThermal, SkipScriptWidensIntervalAndKeepsIndex) {
  FakeThermalScript script = FastScript();
  script.skips = {{3, 2}};
  FakeThermalCamera camera(script);
  ASSERT_TRUE(camera.Open(nullptr));
  EXPECT_EQ(camera.Width(), kWidth);
  EXPECT_EQ(camera.Height(), kHeight);
  std::vector<ReceivedFrame> frames;
  std::vector<int64_t> device_frames;
  for (int i = 0; i < 5; ++i) {
    const std::optional<ReceivedFrame> frame = camera.ReadFrame(kSecondUs);
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->view.format, PixelFormat::kY16);
    EXPECT_EQ(frame->view.stream, StreamKind::kThermal);
    EXPECT_EQ(frame->view.index, static_cast<hwlab::FrameIndex>(i));
    EXPECT_EQ(frame->view.time, frame->host_time);
    EXPECT_EQ(Y16At(frame->view, 5, 4), FakeThermalCamera::PixelValue(camera.LastDeviceFrame(), 5, 4));
    device_frames.push_back(camera.LastDeviceFrame());
    frames.push_back(*frame);
  }
  EXPECT_EQ(device_frames, (std::vector<int64_t>{0, 1, 2, 5, 6}));
  const int64_t period = script.device_period_us;
  EXPECT_EQ(hwlab::IntervalUs(frames[1].host_time, frames[2].host_time), period);
  EXPECT_EQ(hwlab::IntervalUs(frames[2].host_time, frames[3].host_time), 3 * period);
  EXPECT_EQ(hwlab::IntervalUs(frames[3].host_time, frames[4].host_time), period);
}

// 다음 프레임이 기다릴 시간 안에 오지 않으면 nullopt이고, 시간이 흘러 프레임이 오면 받습니다.
TEST(FakeThermal, ShortTimeoutReturnsNothingUntilFrameArrives) {
  FakeThermalCamera camera(FastScript());
  ASSERT_TRUE(camera.Open(nullptr));
  EXPECT_FALSE(camera.ReadFrame(20'000));
  EXPECT_EQ(camera.Connection(), ThermalConnection::kWaiting);
  EXPECT_TRUE(camera.ReadFrame(20'000));
  EXPECT_EQ(camera.Connection(), ThermalConnection::kReceiving);
}

// 끊김 시나리오: 장치는 열린 상태로 남고 프레임만 오지 않으며, 프레임 주기 30배가 지나면 끊김입니다. 실패 횟수만큼 다시 열기에 실패한 뒤 다시 받습니다.
TEST(FakeThermal, DisconnectScriptLostAfterStallThenReopens) {
  FakeThermalScript script = FastScript();
  script.disconnect_after = 4;
  script.reopen_failures = 2;
  FakeThermalCamera camera(script);
  EXPECT_EQ(camera.Connection(), ThermalConnection::kClosed);
  ASSERT_TRUE(camera.Open(nullptr));
  for (int i = 0; i < 4; ++i) ASSERT_TRUE(camera.ReadFrame(kSecondUs));
  const Timestamp last = camera.Now();
  // 한 프레임 주기씩 기다리며 끊김 판정 시간(30주기)이 지날 때까지 셉니다.
  const int64_t period = hwlab::platform::kThermalFramePeriodUs;
  int empty_reads = 0;
  while (camera.Connection() == ThermalConnection::kReceiving) {
    ASSERT_FALSE(camera.ReadFrame(period));
    ASSERT_LT(++empty_reads, 100);
  }
  EXPECT_EQ(camera.Connection(), ThermalConnection::kLost);
  EXPECT_EQ(hwlab::IntervalUs(last, camera.Now()), 30 * period);
  EXPECT_EQ(empty_reads, 30);

  camera.Close();
  EXPECT_EQ(camera.Connection(), ThermalConnection::kClosed);
  std::string error;
  EXPECT_FALSE(camera.Open(&error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(camera.Open(&error));
  ASSERT_TRUE(camera.Open(&error));
  EXPECT_EQ(camera.Connection(), ThermalConnection::kWaiting);
  const std::optional<ReceivedFrame> frame = camera.ReadFrame(kSecondUs);
  ASSERT_TRUE(frame);
  EXPECT_EQ(frame->view.index, 4u);
  EXPECT_GT(frame->host_time, last);
  EXPECT_EQ(camera.Connection(), ThermalConnection::kReceiving);
}

// 다시 연결되지 않는 시나리오에서는 Open이 계속 실패합니다.
TEST(FakeThermal, NeverReconnectsWhenScripted) {
  FakeThermalScript script = FastScript();
  script.disconnect_after = 1;
  FakeThermalCamera camera(script);
  ASSERT_TRUE(camera.Open(nullptr));
  ASSERT_TRUE(camera.ReadFrame(kSecondUs));
  EXPECT_FALSE(camera.ReadFrame(kSecondUs));
  camera.Close();
  for (int i = 0; i < 5; ++i) EXPECT_FALSE(camera.Open(nullptr));
  EXPECT_FALSE(camera.ReadFrame(kSecondUs));
}

// 수신 간격 변동: 수신 간격이 주기에서 지연 최대값 범위 안으로 달라지고, 시각은 늘 증가하며, 같은 시드면 같은 순서입니다.
TEST(FakeThermal, JitterStaysWithinBoundAndRepeats) {
  FakeThermalScript script = FastScript();
  script.width = 8;
  script.height = 4;
  script.jitter_us = 8'000;
  const auto arrivals = [&script]() {
    FakeThermalCamera camera(script);
    EXPECT_TRUE(camera.Open(nullptr));
    std::vector<int64_t> times;
    for (int i = 0; i < 200; ++i) times.push_back(camera.ReadFrame(kSecondUs)->host_time.us);
    return times;
  };
  const std::vector<int64_t> times = arrivals();
  bool varied = false;
  for (size_t i = 1; i < times.size(); ++i) {
    const int64_t gap = times[i] - times[i - 1];
    EXPECT_GE(gap, script.device_period_us - script.jitter_us);
    EXPECT_LE(gap, script.device_period_us + script.jitter_us);
    EXPECT_GT(gap, 0);
    varied = varied || gap != script.device_period_us;
    // 지연은 장치가 프레임을 낸 시각 뒤로만 붙습니다.
    const int64_t ideal = static_cast<int64_t>(i + 1) * script.device_period_us;
    EXPECT_GE(times[i], ideal);
    EXPECT_LE(times[i], ideal + script.jitter_us);
  }
  EXPECT_TRUE(varied);
  EXPECT_EQ(arrivals(), times);
}

// 연결 판정: 기본값은 첫 프레임 전과 받던 중 모두 프레임 주기의 30배(1초)이고, 그 안이면 대기와 수신입니다.
TEST(ThermalConnectionRule, LostAfterThirtyFramePeriods) {
  const ThermalLossRule rule;
  const int64_t limit = 30 * hwlab::platform::kThermalFramePeriodUs;
  EXPECT_EQ(rule.first_frame_us, limit);
  EXPECT_EQ(rule.stall_us, limit);
  const Timestamp opened{1'000};
  EXPECT_EQ(JudgeThermalConnection(false, opened, std::nullopt, Timestamp{2'000}, rule), ThermalConnection::kClosed);
  EXPECT_EQ(JudgeThermalConnection(true, opened, std::nullopt, Timestamp{opened.us + limit - 1}, rule),
            ThermalConnection::kWaiting);
  EXPECT_EQ(JudgeThermalConnection(true, opened, std::nullopt, Timestamp{opened.us + limit}, rule),
            ThermalConnection::kLost);
  const Timestamp last{opened.us + 3 * kSecondUs};
  EXPECT_EQ(JudgeThermalConnection(true, opened, last, Timestamp{last.us + limit - 1}, rule),
            ThermalConnection::kReceiving);
  EXPECT_EQ(JudgeThermalConnection(true, opened, last, Timestamp{last.us + limit}, rule), ThermalConnection::kLost);
}

// 셔터 교정 제어: 열기 전에는 실패하고, 연 뒤에는 교정 횟수와 멈춤 상태가 남으며, Close가 멈춤을 되돌립니다.
TEST(FakeThermal, CalibrationControlNeedsOpenAndCloseReleasesHold) {
  FakeThermalCamera camera(FakeThermalScript{});
  std::string error;
  EXPECT_FALSE(camera.CalibrateNow(&error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(camera.HoldAutoCalibration(true, &error));
  EXPECT_EQ(camera.AutoCalibrationSetting(), "");

  ASSERT_TRUE(camera.Open(&error));
  EXPECT_TRUE(camera.CalibrateNow(&error));
  EXPECT_TRUE(camera.CalibrateNow(&error));
  EXPECT_EQ(camera.Calibrations(), 2);
  EXPECT_TRUE(camera.HoldAutoCalibration(true, &error));
  EXPECT_TRUE(camera.AutoCalibrationHeld());
  EXPECT_EQ(camera.AutoCalibrationSetting(), "fake, held");
  EXPECT_TRUE(camera.HoldAutoCalibration(false, &error));
  EXPECT_FALSE(camera.AutoCalibrationHeld());

  ASSERT_TRUE(camera.HoldAutoCalibration(true, &error));
  camera.Close();
  EXPECT_FALSE(camera.AutoCalibrationHeld());
}

// 원래 셔터 설정을 모르는 장치: 교정과 멈춤 모두 실패하고 이유를 남기며, 상태를 바꾸지 않습니다.
TEST(FakeThermal, UnsupportedCalibrationLeavesStateUnchanged) {
  FakeThermalScript script;
  script.calibration_supported = false;
  FakeThermalCamera camera(script);
  std::string error;
  ASSERT_TRUE(camera.Open(&error));
  EXPECT_FALSE(camera.CalibrateNow(&error));
  EXPECT_FALSE(error.empty());
  error.clear();
  EXPECT_FALSE(camera.HoldAutoCalibration(true, &error));
  EXPECT_FALSE(error.empty());
  EXPECT_EQ(camera.Calibrations(), 0);
  EXPECT_FALSE(camera.AutoCalibrationHeld());
}

// 비어 있는 시험 폴더를 만듭니다. 렌즈 셰이딩 보정 파일을 둡니다.
std::string EmptyLensShadingFolder(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_thermal_lens_shading" / name;
  std::filesystem::remove_all(dir);
  return dir.string();
}

// 보정 파일 폴더를 정하지 않은 카메라는 보정 기능이 없고 보정 요청도 받지 않습니다.
TEST(FakeThermal, LensShadingNeedsFolder) {
  FakeThermalCamera camera(FakeThermalScript{});
  std::string error;
  ASSERT_TRUE(camera.Open(&error));
  EXPECT_EQ(camera.LensShading(), std::nullopt);
  EXPECT_FALSE(camera.CorrectLensShading(std::nullopt, &error));
  EXPECT_FALSE(error.empty());
}

TEST(FakeThermal, LensShadingSavesAndLoadsFile) {
  using hwlab::record::LensShadingSource;
  const std::string folder = EmptyLensShadingFolder("save_load");
  FakeThermalCamera camera(FakeThermalScript{});
  camera.SetLensShadingFolder(folder);
  const hwlab::timing::LensShadingConditions made{36.79, 97.234, hwlab::timing::WarmupPhase::kReady};
  std::string error;
  EXPECT_FALSE(camera.CorrectLensShading(made, &error));  // 열기 전
  ASSERT_TRUE(camera.Open(&error));
  ASSERT_TRUE(camera.LensShading());
  EXPECT_EQ(camera.LensShading()->record.source, LensShadingSource::kNone);
  EXPECT_EQ(camera.LensShading()->record.file_unix_s, std::nullopt);

  ASSERT_TRUE(camera.CorrectLensShading(made, &error)) << error;
  const auto corrected = camera.LensShading();
  ASSERT_TRUE(corrected && corrected->record.file_unix_s);
  EXPECT_EQ(corrected->record.source, LensShadingSource::kCorrected);
  EXPECT_EQ(corrected->made, made);
  EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(folder) / "lens_shading_FakeThermal.cal"));

  camera.Close();
  EXPECT_EQ(camera.LensShading(), std::nullopt);
  ASSERT_TRUE(camera.Open(&error));
  ASSERT_TRUE(camera.LensShading());
  EXPECT_EQ(camera.LensShading()->record.source, LensShadingSource::kLoaded);
  EXPECT_EQ(camera.LensShading()->record.file_unix_s, corrected->record.file_unix_s);
  EXPECT_EQ(camera.LensShading()->made, made);
}

TEST(FakeThermal, LensShadingRemadeWithoutConditionsHasNoRecord) {
  const std::string folder = EmptyLensShadingFolder("remade");
  const std::filesystem::path cal = std::filesystem::path(folder) / "lens_shading_FakeThermal.cal";
  std::filesystem::create_directories(folder);
  std::ofstream(cal) << 1762400000 << "\n";
  ASSERT_TRUE(hwlab::platform::WriteLensShadingConditions(
      cal, 1762300000, {38.23, 98.2, hwlab::timing::WarmupPhase::kReady}));
  FakeThermalCamera camera(FakeThermalScript{});
  camera.SetLensShadingFolder(folder);
  std::string error;
  ASSERT_TRUE(camera.Open(&error));
  ASSERT_TRUE(camera.LensShading());
  EXPECT_EQ(camera.LensShading()->record.file_unix_s, 1762400000);
  EXPECT_EQ(camera.LensShading()->made, std::nullopt);
}

// 보정이 실패하면 kFailed와 이유를 남기고 파일을 쓰지 않습니다.
TEST(FakeThermal, LensShadingFailureKeepsNoFile) {
  using hwlab::record::LensShadingSource;
  const std::string folder = EmptyLensShadingFolder("failure");
  FakeThermalScript script;
  script.lens_shading_fails = true;
  FakeThermalCamera camera(script);
  camera.SetLensShadingFolder(folder);
  std::string error;
  ASSERT_TRUE(camera.Open(&error));
  EXPECT_FALSE(camera.CorrectLensShading(std::nullopt, &error));
  EXPECT_FALSE(error.empty());
  ASSERT_TRUE(camera.LensShading());
  EXPECT_EQ(camera.LensShading()->record.source, LensShadingSource::kFailed);
  EXPECT_FALSE(camera.LensShading()->error.empty());
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(folder) / "lens_shading_FakeThermal.cal"));
}

// 보정 파일 이름에는 장치 이름(모델과 코어 번호)을 넣어 카메라마다 따로 둡니다.
TEST(LensShadingFile, NameHasDevice) {
  EXPECT_EQ(hwlab::platform::LensShadingFileName("TE-EV1-001234"), "lens_shading_TE-EV1-001234.cal");
}

TEST(LensShadingFile, ConditionsBelongToOneCalibrationFile) {
  using hwlab::timing::WarmupPhase;
  const std::filesystem::path cal = std::filesystem::path(EmptyLensShadingFolder("conditions")) / "lens_shading_X.cal";
  std::filesystem::create_directories(cal.parent_path());
  EXPECT_EQ(hwlab::platform::ReadLensShadingConditions(cal, 1762400000), std::nullopt);
  for (const WarmupPhase phase : {WarmupPhase::kMeasuring, WarmupPhase::kWarming, WarmupPhase::kSettling, WarmupPhase::kReady}) {
    const hwlab::timing::LensShadingConditions made{35.125, 96.5, phase};
    ASSERT_TRUE(hwlab::platform::WriteLensShadingConditions(cal, 1762400000, made));
    EXPECT_EQ(hwlab::platform::ReadLensShadingConditions(cal, 1762400000), made);
    EXPECT_EQ(hwlab::platform::ReadLensShadingConditions(cal, 1762400001), std::nullopt);
  }
  EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(cal.string() + ".json")));
  std::ofstream(cal.string() + ".json") << "{\"file_unix_s\": 1762400000}";
  EXPECT_EQ(hwlab::platform::ReadLensShadingConditions(cal, 1762400000), std::nullopt);
}

TEST(FakeThermal, CameraTemperatureFollowsWarmupCurve) {
  FakeThermalScript script;
  script.width = 8;
  script.height = 6;
  script.wall_period_us = 0;
  script.device_period_us = 1'000'000;
  script.fpa_tau_us = 60'000'000;
  FakeThermalCamera camera(script);
  std::string error;
  ASSERT_TRUE(camera.Open(&error));
  EXPECT_EQ(camera.LastCameraTemperature(), std::nullopt);
  std::vector<double> fpa;
  for (int i = 0; i < 120; ++i) {
    ASSERT_TRUE(camera.ReadFrame(2'000'000));
    const auto t = camera.LastCameraTemperature();
    ASSERT_TRUE(t);
    fpa.push_back(t->fpa);
    EXPECT_NEAR(std::fmod(t->fpa / 0.061 + 0.5, 1.0), 0.5, 1e-3);
    EXPECT_NEAR(t->shutter_c, 1.460 * t->fpa - 105.20, 0.01);
  }
  EXPECT_GE(fpa.front(), 88.6);
  EXPECT_LE(fpa.back(), 98.2);
  EXPECT_TRUE(std::is_sorted(fpa.begin(), fpa.end()));
  EXPECT_GT(fpa.back() - fpa.front(), 5.0);

  script.fpa_tau_us = 0;
  FakeThermalCamera silent(script);
  ASSERT_TRUE(silent.Open(&error));
  ASSERT_TRUE(silent.ReadFrame(2'000'000));
  EXPECT_EQ(silent.LastCameraTemperature(), std::nullopt);
}

}  // namespace
