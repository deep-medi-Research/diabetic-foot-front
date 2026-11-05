#include "hwlab/platform/ppg_wire.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using hwlab::platform::BuildPpgCommand;
using hwlab::platform::BuildPpgStart;
using hwlab::platform::BuildPpgStop;
using hwlab::platform::DecodePpgMeasure;
using hwlab::platform::PpgCrc16;
using hwlab::platform::PpgMessage;
using hwlab::platform::PpgParser;
using hwlab::platform::PpgStartRequest;

namespace {

// 장치가 보내는 메시지를 만듭니다. PC 명령과 발신지만 다릅니다.
std::vector<uint8_t> FromDevice(uint8_t code, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> out = BuildPpgCommand(code, data);
  out[1] = hwlab::platform::kPpgFromDevice;
  const uint16_t crc = PpgCrc16(std::span<const uint8_t>(out.data(), out.size() - 2));
  out[out.size() - 2] = static_cast<uint8_t>(crc & 0xFF);
  out[out.size() - 1] = static_cast<uint8_t>(crc >> 8);
  return out;
}

std::vector<uint8_t> MeasureData(const std::vector<uint16_t>& samples, uint8_t hr, uint8_t spo2, uint8_t error) {
  std::vector<uint8_t> data;
  for (const uint16_t s : samples) {
    data.push_back(static_cast<uint8_t>(s >> 8));
    data.push_back(static_cast<uint8_t>(s & 0xFF));
  }
  data.push_back(hr);
  data.push_back(spo2);
  data.push_back(error);
  return data;
}

}  // namespace

// 사양서 예제: 정보 요청 `FF 55 00 03 49`의 CRC 바이트는 `E0 DE`입니다.
TEST(PpgWire, InfoRequestMatchesSpecExample) {
  const std::vector<uint8_t> expected = {0xFF, 0x55, 0x00, 0x03, 0x49, 0xE0, 0xDE};
  EXPECT_EQ(hwlab::platform::BuildPpgInfoRequest(), expected);
}

// 시작 명령은 길이 0x0009(코드 1 + 데이터 6 + CRC 2)이고 데이터는 요청 순서대로입니다.
TEST(PpgWire, BuildsStartCommand) {
  PpgStartRequest request;
  request.preview_s = 30;
  request.measure_s = 60;
  request.sensor = 'F';
  request.gender = 'M';
  request.age = 40;
  request.reference = 'A';
  const auto start = BuildPpgStart(request);
  ASSERT_TRUE(start.has_value());
  ASSERT_EQ(start->size(), 13u);
  EXPECT_EQ((*start)[2], 0x00);
  EXPECT_EQ((*start)[3], 0x09);
  EXPECT_EQ((*start)[4], 'D');
  const std::vector<uint8_t> data(start->begin() + 5, start->begin() + 11);
  EXPECT_EQ(data, (std::vector<uint8_t>{30, 60, 'F', 'M', 40, 'A'}));
}

// 장치가 받지 않는 값은 명령을 만들지 않습니다.
TEST(PpgWire, RejectsOutOfRangeStart) {
  PpgStartRequest request;
  request.measure_s = 30;
  std::string error;
  EXPECT_FALSE(BuildPpgStart(request, &error).has_value());
  EXPECT_NE(error.find("60초 또는 150초"), std::string::npos);
  request = {};
  request.preview_s = 5;
  EXPECT_FALSE(BuildPpgStart(request).has_value());
}

TEST(PpgWire, StopCommandIsCodeP) {
  const auto stop = BuildPpgStop();
  ASSERT_EQ(stop.size(), 7u);
  EXPECT_EQ(stop[4], 'P');
}

// 여러 덩어리로 나뉘어 온 메시지를 이어서 합치고, 마지막 바이트가 든 덩어리의 시각을 메시지 시각으로 씁니다.
TEST(PpgWire, ParsesMessageSplitAcrossChunks) {
  const auto status = FromDevice('s', {'v'});
  const auto measure = FromDevice('m', MeasureData({100, 200, 65535}, 72, 98, 0));
  std::vector<uint8_t> all = status;
  all.insert(all.end(), measure.begin(), measure.end());

  PpgParser parser;
  std::vector<PpgMessage> got = parser.Feed(std::span<const uint8_t>(all.data(), 5), 10);
  EXPECT_TRUE(got.empty());
  got = parser.Feed(std::span<const uint8_t>(all.data() + 5, status.size() + 3), 20);
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0].code, 's');
  EXPECT_EQ(got[0].data, (std::vector<uint8_t>{'v'}));
  EXPECT_EQ(got[0].host_time_us, 20);
  const size_t used = 5 + status.size() + 3;
  got = parser.Feed(std::span<const uint8_t>(all.data() + used, all.size() - used), 30);
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0].code, 'm');
  EXPECT_EQ(got[0].host_time_us, 30);
  const auto decoded = DecodePpgMeasure(got[0].data);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->samples, (std::vector<uint16_t>{100, 200, 65535}));
  EXPECT_EQ(decoded->heart_rate, 72);
  EXPECT_EQ(decoded->spo2, 98);
  EXPECT_EQ(decoded->error, 0);
  EXPECT_EQ(parser.Counts().messages, 2);
  EXPECT_EQ(parser.Counts().skipped_bytes, 0);
}

// 앞의 잡음과 CRC가 틀린 메시지를 건너뛰고 다음 정상 메시지를 찾습니다.
TEST(PpgWire, ResyncsAfterNoiseAndBadCrc) {
  auto broken = FromDevice('s', {'s'});
  broken.back() ^= 0x01;
  const auto good = FromDevice('s', {'p'});
  std::vector<uint8_t> all = {0x00, 0x12, 0xFF, 0x00};
  all.insert(all.end(), broken.begin(), broken.end());
  all.insert(all.end(), good.begin(), good.end());

  PpgParser parser;
  const auto got = parser.Feed(all, 1);
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0].data, (std::vector<uint8_t>{'p'}));
  EXPECT_EQ(parser.Counts().crc_failures, 1);
  EXPECT_GT(parser.Counts().skipped_bytes, 0);
}

// 표본 값 안의 `FF 4D`를 상태 메시지 시작으로 잘못 잡아도 길이가 맞지 않으면 버리고 계속 찾습니다.
TEST(PpgWire, RejectsFixedLengthCodeWithWrongLength) {
  const std::vector<uint8_t> fake_start = {0xFF, 0x4D, 0x10, 0x00, 's'};
  const auto good = FromDevice('e', {'m'});
  std::vector<uint8_t> all = fake_start;
  all.insert(all.end(), good.begin(), good.end());
  PpgParser parser;
  const auto got = parser.Feed(all, 1);
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0].code, 'e');
  EXPECT_GE(parser.Counts().bad_lengths + parser.Counts().implausible, 1);
}

// 끝 3바이트를 뺀 길이가 홀수인 측정 데이터는 풀지 않습니다.
TEST(PpgWire, DecodeRejectsOddSampleBytes) {
  EXPECT_FALSE(DecodePpgMeasure(std::vector<uint8_t>{1, 2, 3, 4}).has_value());
  EXPECT_FALSE(DecodePpgMeasure(std::vector<uint8_t>{1, 2}).has_value());
  const auto empty = DecodePpgMeasure(std::vector<uint8_t>{0, 0, 'f'});
  ASSERT_TRUE(empty.has_value());
  EXPECT_TRUE(empty->samples.empty());
  EXPECT_EQ(hwlab::platform::PpgMeasureErrorText(empty->error), "손가락 빠짐");
}
