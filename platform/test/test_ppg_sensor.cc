#include "hwlab/platform/ppg_sensor.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../src/json_reader.h"
#include "hwlab/platform/fake_ppg_device.h"
#include "hwlab/platform/fake_serial_port.h"
#include "hwlab/timing/clock_map.h"

using hwlab::Timestamp;
using hwlab::platform::BuildPpgCommand;
using hwlab::platform::FakePpgDevice;
using hwlab::platform::FakeSerialLine;
using hwlab::platform::FakeSerialPort;
using hwlab::platform::JsonReader;
using hwlab::platform::PpgCrc16;
using hwlab::platform::PpgLink;
using hwlab::platform::PpgReading;
using hwlab::platform::PpgSensor;
using hwlab::platform::PpgShotRecord;
using hwlab::platform::PpgSnapshot;
using hwlab::platform::PpgState;
using hwlab::platform::WritePpgSidecar;

namespace {

std::vector<uint8_t> FromDevice(uint8_t code, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> out = BuildPpgCommand(code, data);
  out[1] = hwlab::platform::kPpgFromDevice;
  const uint16_t crc = PpgCrc16(std::span<const uint8_t>(out.data(), out.size() - 2));
  out[out.size() - 2] = static_cast<uint8_t>(crc & 0xFF);
  out[out.size() - 1] = static_cast<uint8_t>(crc >> 8);
  return out;
}

std::vector<uint8_t> Measure(uint16_t first, int count, uint8_t hr, uint8_t error = 0) {
  std::vector<uint8_t> data;
  for (int i = 0; i < count; ++i) {
    const auto v = static_cast<uint16_t>(first + i);
    data.push_back(static_cast<uint8_t>(v >> 8));
    data.push_back(static_cast<uint8_t>(v & 0xFF));
  }
  data.push_back(hr);
  data.push_back(98);
  data.push_back(error);
  return FromDevice('m', data);
}

// 받은 바이트를 모두 해석할 때까지 읽습니다. 가짜 포트는 받은 바이트를 기다리지 않고 돌려줍니다.
template <typename Pred>
PpgReading PumpUntil(PpgLink& link, Pred pred) {
  for (int i = 0; i < 1000 && !pred(link.Reading()); ++i) {
    if (!link.Pump(0)) break;
  }
  return link.Reading();
}

// 다른 스레드가 바꾸는 값을 기다립니다. 3초 안에 맞지 않으면 false입니다.
template <typename Pred>
bool WaitUntil(Pred pred) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!pred()) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return true;
}

std::chrono::steady_clock::time_point Soon() { return std::chrono::steady_clock::now() + std::chrono::seconds(3); }

std::string ReadText(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// 시험용 PC 시계입니다. 테스트가 시각을 정합니다.
struct FakeClock {
  std::atomic<int64_t> now{1'000'000};
  std::function<int64_t()> Fn() {
    return [this] { return now.load(); };
  }
};

// 장치 스레드가 보낸 스냅숏과 파형을 모읍니다.
struct Received {
  std::mutex mutex;
  std::vector<PpgSnapshot> snapshots;
  int32_t waves = 0;

  PpgSensor::Callbacks Callbacks() {
    PpgSensor::Callbacks c;
    c.state = [this](const PpgSnapshot& s) {
      std::lock_guard<std::mutex> lock(mutex);
      snapshots.push_back(s);
    };
    c.wave = [this](std::vector<double>) {
      std::lock_guard<std::mutex> lock(mutex);
      ++waves;
    };
    return c;
  }
  PpgState Last() {
    std::lock_guard<std::mutex> lock(mutex);
    return snapshots.empty() ? PpgState::kOff : snapshots.back().state;
  }
  PpgSnapshot LastSnapshot() {
    std::lock_guard<std::mutex> lock(mutex);
    return snapshots.empty() ? PpgSnapshot{} : snapshots.back();
  }
};

// 포트에 쓴 명령이 하나 이상이고 모두 정보 요청('I')이면 true입니다.
bool OnlyInfoRequests(const std::vector<std::vector<uint8_t>>& writes) {
  for (const auto& w : writes) {
    if (w.size() < 5 || w[4] != 'I') return false;
  }
  return !writes.empty();
}

std::vector<uint8_t> Info(const std::string& text) { return FromDevice('i', std::vector<uint8_t>(text.begin(), text.end())); }

}  // namespace

// 열지 못하는 포트는 false와 이유를 돌려줍니다.
TEST(PpgLink, OpenFailsForUnknownPort) {
  auto line = std::make_shared<FakeSerialLine>();
  PpgLink link;
  std::string error;
  EXPECT_FALSE(link.Open(FakeSerialPort::Opener("COM6", line), "COM9", &error));
  EXPECT_FALSE(error.empty());
  EXPECT_EQ(link.Failure(), error);
  EXPECT_EQ(link.Snapshot().state, PpgState::kOff);
}

// 중지 뒤 시작 명령을 보내고, 상태와 측정 데이터를 받아 표본 수, 심박수, 측정 오류를 보입니다.
TEST(PpgLink, RestartsAndReadsMeasureData) {
  auto line = std::make_shared<FakeSerialLine>();
  FakeClock clock;
  PpgLink link(clock.Fn());
  std::string error;
  ASSERT_TRUE(link.Open(FakeSerialPort::Opener("COM6", line), "COM6", &error)) << error;
  EXPECT_EQ(link.Snapshot().state, PpgState::kChecking);
  ASSERT_TRUE(link.Restart(&error)) << error;
  ASSERT_EQ(line->Writes().size(), 2u);
  EXPECT_EQ(line->Writes()[0][4], 'P');
  EXPECT_EQ(line->Writes()[1][4], 'D');

  line->Feed(FromDevice('s', {'v'}));
  line->Feed(Measure(100, 24, 70));
  line->Feed(Measure(124, 24, 71, 'f'));
  const PpgReading r = PumpUntil(link, [](const PpgReading& x) { return x.samples == 48; });
  EXPECT_EQ(r.samples, 48);
  EXPECT_EQ(r.status, 'v');
  EXPECT_EQ(r.heart_rate, 71);
  EXPECT_EQ(r.measure_error, 'f');
  EXPECT_TRUE(r.open);
  EXPECT_TRUE(r.streaming);
  EXPECT_EQ(link.Snapshot().state, PpgState::kStreaming);

  // 마지막 측정 데이터 뒤 0.5초가 지나면 표본이 멈춘 것으로 봅니다.
  clock.now += PpgLink::kStaleUs + 1;
  EXPECT_FALSE(link.Reading().streaming);
  EXPECT_EQ(link.Snapshot().state, PpgState::kStalled);
}

// 붙인 동안의 표본만 기록하고, 장치 시각은 표본 번호 / 500Hz이며 PC 수신 시각으로 공통 시각을 맞춥니다.
TEST(PpgLink, RecordsOnlyWhileAttached) {
  auto line = std::make_shared<FakeSerialLine>();
  FakeClock clock;
  PpgLink link(clock.Fn());
  std::string error;
  ASSERT_TRUE(link.Open(FakeSerialPort::Opener("COM6", line), "COM6", &error)) << error;
  ASSERT_TRUE(link.Restart(&error));
  line->Feed(Measure(0, 10, 60));
  PumpUntil(link, [](const PpgReading& x) { return x.samples == 10; });

  auto record = std::make_shared<PpgShotRecord>(Timestamp{clock.now.load()});
  link.Attach(record);
  // 메시지 3개: 장치 시각으로 표본 10~19, 20~29, 30~39. PC는 마지막 표본 뒤 3ms에 받습니다.
  for (int m = 0; m < 3; ++m) {
    const int64_t last_device_us = (10 + 10 * m + 9) * 2000;
    clock.now = 5'000'000 + last_device_us + 3000;
    line->Feed(Measure(static_cast<uint16_t>(10 + 10 * m), 10, 60));
    PumpUntil(link, [m](const PpgReading& x) { return x.samples == 20 + 10 * m; });
  }
  record->Close(Timestamp{clock.now.load()});
  link.Detach();
  const auto result = record->Finish();
  ASSERT_EQ(result.values.size(), 30u);
  EXPECT_EQ(result.values.front(), 10);
  EXPECT_EQ(result.values.back(), 39);
  EXPECT_EQ(result.device_time.front().us, 10 * 2000);
  EXPECT_EQ(result.anchors.size(), 3u);
  ASSERT_EQ(result.common_time.size(), 30u);
  EXPECT_NEAR(static_cast<double>(result.common_time.back().us), 5'000'000.0 + 39 * 2000 + 3000, 1.0);
  EXPECT_NEAR(static_cast<double>(result.common_time.front().us), 5'000'000.0 + 10 * 2000 + 3000, 1.0);
  EXPECT_FALSE(result.disconnected_at.has_value());

  // 뗀 뒤 받은 표본은 기록에 들어가지 않습니다.
  line->Feed(Measure(40, 10, 60));
  PumpUntil(link, [](const PpgReading& x) { return x.samples == 50; });
  EXPECT_EQ(record->Finish().values.size(), 30u);
}

// 다시 시작 직후 측정 데이터 하나가 먼저 오고 다음 데이터가 평소보다 128ms 늦게 와도(실측 모양) 빈틈 뒤 표본의 공통 시각은
// 수신 지연이 가장 짧은 경우의 PC 수신 시각과 1ms 안에서 맞고, 빈틈은 공통 시각에 그대로 남습니다.
TEST(PpgLink, RestartGapSplitsClockSegments) {
  auto line = std::make_shared<FakeSerialLine>();
  FakeClock clock;
  PpgLink link(clock.Fn());
  std::string error;
  ASSERT_TRUE(link.Open(FakeSerialPort::Opener("COM6", line), "COM6", &error)) << error;
  ASSERT_TRUE(link.Restart(&error)) << error;
  auto record = std::make_shared<PpgShotRecord>(Timestamp{clock.now.load()});
  link.Attach(record);

  // 메시지 m의 마지막 표본을 PC가 받는 시각입니다. 장치 시계는 0.5% 느리고 수신 지연은 3~7ms입니다.
  constexpr int kMessages = 120;
  constexpr int64_t kExtraUs = 128'000;
  const auto true_us = [](int64_t device_us, int64_t extra_us) {
    return 5'000'000 + extra_us + static_cast<int64_t>(static_cast<double>(device_us) * 1.005) + 3'000;
  };
  for (int m = 0; m < kMessages; ++m) {
    const int64_t last_device_us = (int64_t{24} * m + 23) * 2000;
    clock.now = true_us(last_device_us, m >= 1 ? kExtraUs : 0) + (m % 5) * 1'000;
    line->Feed(Measure(static_cast<uint16_t>(24 * m), 24, 70));
    PumpUntil(link, [m](const PpgReading& x) { return x.samples == 24 * (m + 1); });
  }
  record->Close(Timestamp{clock.now.load()});
  const auto result = record->Finish();

  ASSERT_EQ(result.values.size(), 24u * kMessages);
  ASSERT_EQ(result.clock.size(), 2u);
  EXPECT_EQ(result.clock[0].last_device.us, 23 * 2000);
  for (size_t i = 24; i < result.common_time.size(); ++i) {
    EXPECT_NEAR(static_cast<double>(result.common_time[i].us),
                static_cast<double>(true_us(result.device_time[i].us, kExtraUs)), 1'000.0)
        << i;
  }
  EXPECT_GT(result.common_time[24].us - result.common_time[23].us, 100'000);

  // 사이드카에는 시계 구간 둘과 빈틈 하나(23번 표본 뒤)가 남습니다.
  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_ppg";
  std::filesystem::create_directories(dir);
  const std::string path = (dir / "gap_ppg.json").string();
  ASSERT_TRUE(WritePpgSidecar(path, result, "gap_ppg.csv", "COM6", std::nullopt, &error)) << error;
  const std::string json = ReadText(path);
  EXPECT_NE(json.find("\"samples_file\": \"gap_ppg.csv\""), std::string::npos) << json;
  EXPECT_NE(json.find("\"last_device_us\": 46000"), std::string::npos) << json;
  EXPECT_NE(json.find("\"count\": 2880,\n    \"messages\": 120"), std::string::npos) << json;
  EXPECT_NE(json.find("\"gaps\": [\n      {\n        \"after_sample\": 23,"), std::string::npos) << json;
}

// 사이드카의 측정 데이터별 시각 쌍(anchor_device_us, anchor_host_us)을 FitClockSegments로 다시 맞추면 사이드카의 clock과
// 같습니다. 다시 시작 직후의 수신 빈틈으로 시계 구간이 둘인 기록이고, PC 시각은 부팅 뒤 수 일이 지난 크기입니다.
TEST(PpgShotRecord, SidecarAnchorsRefitToStoredClock) {
  constexpr int64_t kBootUs = 400'000'000'000;
  constexpr int kMessages = 60;
  PpgShotRecord record(Timestamp{kBootUs});
  hwlab::platform::PpgMeasure measure;
  measure.samples.assign(24, 30'000);
  for (int m = 0; m < kMessages; ++m) {
    const int64_t last_device_us = (int64_t{24} * m + 23) * 2000;
    const int64_t gap_us = m >= 1 ? 128'000 : 0;
    const int64_t jitter_us = (m * 7) % 5 * 1'000;
    record.AddMeasure(measure, int64_t{24} * m,
                      Timestamp{kBootUs + gap_us + last_device_us * 1005 / 1000 + 3'000 + jitter_us});
  }
  record.Close(Timestamp{kBootUs + 4'000'000});
  const auto result = record.Finish();
  ASSERT_EQ(result.clock.size(), 2u);

  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_ppg";
  std::filesystem::create_directories(dir);
  const std::string path = (dir / "anchors_ppg.json").string();
  std::string error;
  ASSERT_TRUE(WritePpgSidecar(path, result, "anchors_ppg.csv", "COM6", std::nullopt, &error)) << error;
  const std::string json = ReadText(path);

  const auto device = JsonReader(json).FindArray({"anchor_device_us"});
  const auto host = JsonReader(json).FindArray({"anchor_host_us"});
  ASSERT_TRUE(device.has_value() && host.has_value()) << json;
  ASSERT_EQ(device->size(), static_cast<size_t>(kMessages));
  ASSERT_EQ(host->size(), static_cast<size_t>(kMessages));
  // 첫 측정 데이터의 마지막 표본(23번)의 장치 시각과 그 데이터를 받은 PC 시각입니다.
  EXPECT_EQ(device->front().text, "46000");
  EXPECT_EQ(host->front().text, std::to_string(kBootUs + 46'230 + 3'000));
  const auto messages = JsonReader(json).FindScalar({"samples", "messages"});
  ASSERT_TRUE(messages.has_value()) << json;
  EXPECT_EQ(messages->text, std::to_string(kMessages));

  std::vector<hwlab::timing::ClockSample> pairs;
  for (size_t i = 0; i < device->size(); ++i) {
    pairs.push_back({Timestamp{std::stoll((*device)[i].text)}, Timestamp{std::stoll((*host)[i].text)}});
  }
  const auto refit = hwlab::timing::FitClockSegments(pairs, PpgLink::kReceiveGapUs);
  ASSERT_EQ(refit.size(), 2u);
  for (size_t k = 0; k < refit.size(); ++k) {
    const auto stored = [&](const char* name) {
      const auto value = JsonReader(json).FindScalar({"clock", std::to_string(k), name});
      return value ? value->text : std::string();
    };
    EXPECT_EQ(stored("last_device_us"), std::to_string(refit[k].last_device.us)) << k;
    EXPECT_EQ(stored("offset_us"), std::to_string(refit[k].map.offset_us)) << k;
    // rate는 소수점 아래 9자리로 씁니다.
    EXPECT_NEAR(std::stod(stored("rate")), refit[k].map.rate, 0.5e-9) << k;
    EXPECT_EQ(stored("max_residual_us"), std::to_string(refit[k].map.max_residual_us)) << k;
  }
}

// 가짜 장치는 시작에 상태 'v', 측정 중 다시 시작에 오류 's', 중지에 상태 'u', 정보 요청에 장치 정보로 답합니다.
TEST(FakePpgDevice, AnswersCommandsLikeDevice) {
  auto line = std::make_shared<FakeSerialLine>();
  FakePpgDevice device(line);
  auto port = FakeSerialPort::Opener("COM6", line)("COM6", {}, nullptr);
  ASSERT_NE(port, nullptr);
  const auto exchange = [&](const std::vector<uint8_t>& command) {
    EXPECT_TRUE(port->Write(command, 100));
    device.HandleCommands();
    std::array<uint8_t, 256> buffer{};
    const auto n = port->Read(buffer, 100);
    hwlab::platform::PpgParser parser;
    return parser.Feed(std::span<const uint8_t>(buffer.data(), n.value_or(0)), 0);
  };

  const auto started = exchange(*hwlab::platform::BuildPpgStart({}));
  ASSERT_EQ(started.size(), 1u);
  EXPECT_EQ(started[0].code, 's');
  EXPECT_EQ(started[0].data, std::vector<uint8_t>{'v'});
  EXPECT_TRUE(device.Running());
  EXPECT_EQ(device.StartCount(), 1);
  EXPECT_EQ(device.LastStartData()[0], 60);

  const auto again = exchange(*hwlab::platform::BuildPpgStart({}));
  ASSERT_EQ(again.size(), 1u);
  EXPECT_EQ(again[0].code, 'e');
  EXPECT_EQ(again[0].data, std::vector<uint8_t>{'s'});
  EXPECT_EQ(device.StartCount(), 1);

  const auto stopped = exchange(hwlab::platform::BuildPpgStop());
  ASSERT_EQ(stopped.size(), 1u);
  EXPECT_EQ(stopped[0].data, std::vector<uint8_t>{'u'});
  EXPECT_FALSE(device.Running());
  EXPECT_EQ(device.StopCount(), 1);

  const auto info = exchange(hwlab::platform::BuildPpgInfoRequest());
  ASSERT_EQ(info.size(), 1u);
  EXPECT_EQ(info[0].code, 'i');
  EXPECT_EQ(std::string(info[0].data.begin(), info[0].data.end()), "200000270270320");
}

// 가짜 장치의 맥파가 촬영 기록에 그대로 들어가고, 기록 중 받은 상태와 통신 집계가 기록과 사이드카에 남습니다.
TEST(FakePpgDevice, FeedsShotRecordAndSidecar) {
  auto line = std::make_shared<FakeSerialLine>();
  FakePpgDevice device(line);
  PpgLink link;
  std::string error;
  ASSERT_TRUE(link.Open(FakeSerialPort::Opener("COM6", line), "COM6", &error)) << error;
  ASSERT_TRUE(link.Restart(&error)) << error;
  auto record = std::make_shared<PpgShotRecord>(Timestamp{0});
  link.Attach(record);
  device.HandleCommands();
  device.EmitMeasurement(24, 0, 0);
  device.EmitMeasurement(24, 72, 98, 'f');
  device.EmitStatus(hwlab::platform::kPpgStatusMeasuring);
  PumpUntil(link, [](const PpgReading& x) { return x.samples == 48 && x.status == 's'; });
  record->Close(Timestamp{std::numeric_limits<int64_t>::max()});
  const auto result = record->Finish();

  ASSERT_EQ(result.values.size(), 48u);
  for (size_t i = 0; i < result.values.size(); ++i) {
    EXPECT_EQ(result.values[i], FakePpgDevice::WaveAt(static_cast<int64_t>(i))) << i;
  }
  ASSERT_EQ(result.events.size(), 2u);
  EXPECT_EQ(result.events[0].value, 'v');
  EXPECT_EQ(result.events[1].value, 's');
  EXPECT_EQ(result.counts.messages, 4);
  EXPECT_EQ(result.counts.skipped_bytes, 0);

  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_ppg";
  std::filesystem::create_directories(dir);
  const std::string path = (dir / "fake_ppg.json").string();
  // RGB 녹화 구간이 두 번째 측정 데이터의 표본 12개만 덮는 경우입니다.
  const auto window = hwlab::record::SummarizePpgErrors(result.common_time, result.measure_error,
                                                        result.common_time[30], result.common_time[41]);
  ASSERT_TRUE(WritePpgSidecar(path, result, "fake_ppg.csv", "COM6", window, &error)) << error;
  const std::string json = ReadText(path);
  EXPECT_NE(json.find("\"measure_error\": 24"), std::string::npos) << json;
  EXPECT_NE(json.find("\"code\": \"s\",\n      \"value\": \"s\""), std::string::npos) << json;
  // 측정 오류 구간은 기록 전체에서 찾으므로 두 번째 측정 데이터 24개입니다. 측정 데이터 둘은 한 식(속도 1)으로 맞춥니다.
  const std::string span = "\"code\": \"f\",\n        \"begin_us\": " + std::to_string(result.common_time[24].us) +
                           ",\n        \"end_us\": " + std::to_string(result.common_time[47].us) +
                           ",\n        \"samples\": 24,\n        \"rgb_begin_s\": -0.012,\n        \"rgb_end_s\": 0.034\n";
  EXPECT_NE(json.find(span), std::string::npos) << json;
  EXPECT_NE(json.find("\"samples\": 12,\n      \"codes\": [\n        {\n          \"code\": \"f\",\n"
                      "          \"samples\": 12,\n          \"longest_samples\": 12,\n          \"runs\": 1\n"),
            std::string::npos)
      << json;
}

// 측정 오류 값 하나의 양을 초와 비율로 씁니다. 1% 아래는 소수점 아래 한 자리, 여러 번 끊겨 나왔으면 횟수를 붙입니다.
TEST(PpgErrorShareText, WritesSecondsPercentAndRuns) {
  EXPECT_EQ(hwlab::platform::PpgErrorShareText({'s', 2150, 2150, 1}, 15'000), "신호 나쁨 4.3초(14%)");
  EXPECT_EQ(hwlab::platform::PpgErrorShareText({'f', 48, 24, 2}, 15'000), "손가락 빠짐 0.1초(0.3%, 2번)");
  EXPECT_EQ(hwlab::platform::PpgErrorShareText({'p', 15'000, 15'000, 1}, 15'000), "센서 연결 안 됨 30.0초(100%)");
}

// 장치는 박동을 잡은 측정 데이터에만 심박수를 채웁니다. 0이 온 측정 데이터 사이에도 마지막 심박수를 kHeartRateHoldUs 동안
// 보이고, 그 뒤와 다시 시작한 뒤에는 0입니다.
TEST(PpgLink, HoldsHeartRateBetweenBeats) {
  auto line = std::make_shared<FakeSerialLine>();
  FakeClock clock;
  PpgLink link(clock.Fn());
  std::string error;
  ASSERT_TRUE(link.Open(FakeSerialPort::Opener("COM6", line), "COM6", &error)) << error;
  ASSERT_TRUE(link.Restart(&error)) << error;
  line->Feed(Measure(0, 24, 0));
  line->Feed(Measure(24, 24, 84));
  line->Feed(Measure(48, 24, 0));
  PumpUntil(link, [](const PpgReading& x) { return x.samples == 72; });
  EXPECT_EQ(link.Reading().heart_rate, 84);
  EXPECT_EQ(link.Reading().spo2, 98);
  EXPECT_EQ(link.Snapshot().heart_rate, 84);

  clock.now += PpgLink::kHeartRateHoldUs + 1;
  EXPECT_EQ(link.Reading().heart_rate, 0);
  line->Feed(Measure(72, 24, 86));
  PumpUntil(link, [](const PpgReading& x) { return x.samples == 96; });
  EXPECT_EQ(link.Reading().heart_rate, 86);

  ASSERT_TRUE(link.Restart(&error)) << error;
  EXPECT_EQ(link.Reading().heart_rate, 0);
}

// 화면 파형용으로 최근 표본을 받은 순서대로 kRecentCapacity개까지 보관하고, 다시 시작하면 비웁니다.
TEST(PpgLink, KeepsRecentValuesForWaveform) {
  auto line = std::make_shared<FakeSerialLine>();
  PpgLink link;
  std::string error;
  ASSERT_TRUE(link.Open(FakeSerialPort::Opener("COM6", line), "COM6", &error)) << error;
  ASSERT_TRUE(link.Restart(&error)) << error;
  line->Feed(Measure(0, 24, 70));
  line->Feed(Measure(24, 24, 70));
  PumpUntil(link, [](const PpgReading& x) { return x.samples == 48; });
  const std::vector<uint16_t> last = link.RecentValues(30);
  ASSERT_EQ(last.size(), 30u);
  EXPECT_EQ(last.front(), 18);
  EXPECT_EQ(last.back(), 47);
  EXPECT_EQ(link.RecentValues(1000).size(), 48u);

  // 210개 메시지(5040표본)를 받으면 가장 오래된 40개를 버립니다.
  for (int m = 2; m < 210; ++m) line->Feed(Measure(static_cast<uint16_t>(24 * m), 24, 70));
  PumpUntil(link, [](const PpgReading& x) { return x.samples == 5040; });
  const std::vector<uint16_t> all = link.RecentValues(PpgLink::kRecentCapacity + 100);
  ASSERT_EQ(all.size(), PpgLink::kRecentCapacity);
  EXPECT_EQ(all.front(), 40);
  EXPECT_EQ(all.back(), 5039);

  ASSERT_TRUE(link.Restart(&error)) << error;
  EXPECT_TRUE(link.RecentValues(10).empty());
}

// 화면 파형은 5개씩 평균한 점이고, 튀는 값 몇 개가 높이를 정하지 않도록 1~99% 범위를 0~1로 옮깁니다.
TEST(PpgWavePoints, AveragesAndScalesWithoutOutliers) {
  std::vector<uint16_t> values;
  for (int i = 0; i < 1000; ++i) values.push_back(static_cast<uint16_t>(1000 + (i / 5) % 2 * 100));
  values[500] = 60'000;  // 손가락을 넣고 뺄 때처럼 튄 값
  const auto points = hwlab::platform::PpgWavePoints(values);
  ASSERT_EQ(points.size(), 200u);
  EXPECT_DOUBLE_EQ(points[0], 0.0);
  EXPECT_DOUBLE_EQ(points[1], 1.0);
  EXPECT_DOUBLE_EQ(points[100], 1.0);  // 튄 값이 든 점은 1에 머물고 나머지 높이를 바꾸지 않음
  EXPECT_TRUE(hwlab::platform::PpgWavePoints(std::vector<uint16_t>(9, 1000)).empty());
}

// 다른 연결이 장치를 측정 중으로 남겼어도, 다시 시작이 중지를 먼저 보내므로 장치가 새로 시작합니다.
// 중지를 보내지 않으면 시작 명령이 오류 's'로 거부됩니다(2026-10-20 실측).
TEST(PpgLink, RestartRestartsDeviceLeftRunning) {
  auto line = std::make_shared<FakeSerialLine>();
  FakePpgDevice device(line);
  {
    auto other = FakeSerialPort::Opener("COM6", line)("COM6", {}, nullptr);
    ASSERT_NE(other, nullptr);
    ASSERT_TRUE(other->Write(*hwlab::platform::BuildPpgStart({}), 100));
    device.HandleCommands();
    ASSERT_TRUE(device.Running());
  }
  PpgLink link;
  std::string error;
  ASSERT_TRUE(link.Open(FakeSerialPort::Opener("COM6", line), "COM6", &error)) << error;
  ASSERT_TRUE(link.Restart(&error)) << error;
  device.HandleCommands();

  EXPECT_EQ(device.StopCount(), 1);
  EXPECT_EQ(device.StartCount(), 2);
  // 앞 연결의 'v', 중지의 'u', 새 시작의 'v'까지 받았을 때 오류가 없습니다.
  const PpgReading r = PumpUntil(link, [](const PpgReading& x) { return x.counts.messages == 3; });
  EXPECT_EQ(r.status, 'v');
  EXPECT_EQ(r.device_error, 0);
}

// 시작 뒤 측정 데이터가 오지 않은 시간을 셉니다. 받은 적이 없으면 시작부터, 받은 뒤에는 마지막 측정 데이터부터입니다.
TEST(PpgLink, ReportsQuietTimeSinceLastData) {
  auto line = std::make_shared<FakeSerialLine>();
  FakeClock clock;
  PpgLink link(clock.Fn());
  std::string error;
  ASSERT_TRUE(link.Open(FakeSerialPort::Opener("COM6", line), "COM6", &error)) << error;
  EXPECT_EQ(link.Reading().quiet_us, 0);

  ASSERT_TRUE(link.Restart(&error)) << error;
  clock.now += 1'500'000;
  EXPECT_EQ(link.Reading().quiet_us, 1'500'000);

  line->Feed(Measure(0, 24, 70));
  PumpUntil(link, [](const PpgReading& x) { return x.samples == 24; });
  EXPECT_EQ(link.Reading().quiet_us, 0);
  clock.now += 2'100'000;
  EXPECT_EQ(link.Reading().quiet_us, 2'100'000);
}

// 측정 구간 진입('s'), 측정 끝('p'), 측정 데이터가 2초 넘게 없음이면 다시 시작하고, 다시 시작한 뒤 5초 안이면 하지 않습니다.
TEST(PpgNeedsRestart, Rules) {
  using hwlab::platform::kPpgRestartIntervalUs;
  using hwlab::platform::kPpgRestartQuietUs;
  using hwlab::platform::PpgNeedsRestart;
  PpgReading r;
  r.open = true;
  r.status = 'v';
  r.quiet_us = 100'000;
  EXPECT_FALSE(PpgNeedsRestart(r, kPpgRestartIntervalUs));
  r.status = 's';
  EXPECT_TRUE(PpgNeedsRestart(r, kPpgRestartIntervalUs));
  r.status = 'p';
  EXPECT_TRUE(PpgNeedsRestart(r, kPpgRestartIntervalUs));
  r.status = 'v';
  r.quiet_us = kPpgRestartQuietUs + 1;
  EXPECT_TRUE(PpgNeedsRestart(r, kPpgRestartIntervalUs));
  EXPECT_FALSE(PpgNeedsRestart(r, kPpgRestartIntervalUs - 1));
  r.open = false;
  EXPECT_FALSE(PpgNeedsRestart(r, kPpgRestartIntervalUs));
}

// 실측(2026-10-21): 손가락 없이 미리보기가 끝나면 장치가 오류 'f'를 보내고 측정 데이터를 멈춥니다. 기록 중이 아니면
// RestartIfStopped가 장치를 다시 시작하고, 장치가 미리보기로 돌아와 측정 데이터를 다시 보내며 장치 오류 표시도 지워집니다.
TEST(PpgLink, RecoversAfterDeviceStopsWithoutFinger) {
  auto line = std::make_shared<FakeSerialLine>();
  FakePpgDevice device(line);
  FakeClock clock;
  PpgLink link(clock.Fn());
  std::string error;
  ASSERT_TRUE(link.Open(FakeSerialPort::Opener("COM6", line), "COM6", &error)) << error;
  ASSERT_TRUE(link.Restart(&error)) << error;
  device.HandleCommands();
  device.EmitMeasurement(24, 0, 0, 'f');
  PumpUntil(link, [](const PpgReading& x) { return x.samples == 24; });
  EXPECT_FALSE(link.RestartIfStopped());

  device.EmitError('f');
  PumpUntil(link, [](const PpgReading& x) { return x.device_error == 'f'; });
  clock.now += hwlab::platform::kPpgRestartIntervalUs;
  EXPECT_FALSE(link.Reading().streaming);
  EXPECT_EQ(link.Snapshot().device_error, 'f');
  // 촬영 기록을 붙여 두었으면 다시 시작하지 않습니다.
  auto record = std::make_shared<PpgShotRecord>(Timestamp{clock.now.load()});
  link.Attach(record);
  EXPECT_FALSE(link.RestartIfStopped());
  link.Detach();
  EXPECT_TRUE(link.RestartIfStopped());

  device.HandleCommands();
  EXPECT_TRUE(device.Running());
  device.EmitMeasurement(24, 0, 0, 'f');
  const PpgReading r = PumpUntil(link, [](const PpgReading& x) { return x.samples == 24; });
  EXPECT_TRUE(r.streaming);
  EXPECT_EQ(r.device_error, 0);
  EXPECT_FALSE(link.RestartIfStopped());
}

// 닫기와 소멸 때 시작한 장치에 중지 명령을 보냅니다.
TEST(PpgLink, CloseSendsStopCommand) {
  auto line = std::make_shared<FakeSerialLine>();
  std::string error;
  {
    PpgLink link;
    ASSERT_TRUE(link.Open(FakeSerialPort::Opener("COM6", line), "COM6", &error));
    ASSERT_TRUE(link.Restart(&error));
    link.Close();
    EXPECT_FALSE(link.IsOpen());
  }
  const auto writes = line->Writes();
  ASSERT_EQ(writes.size(), 3u);
  EXPECT_EQ(writes[2][4], 'P');

  {
    PpgLink link;
    ASSERT_TRUE(link.Open(FakeSerialPort::Opener("COM6", line), "COM6", &error));
    ASSERT_TRUE(link.Restart(&error));
  }
  const auto after = line->Writes();
  ASSERT_EQ(after.size(), 6u);
  EXPECT_EQ(after[5][4], 'P');
}

// 녹화 중 장치가 빠지면 포트를 닫고, 그때까지 받은 표본은 촬영 기록에 남으며 끊긴 시각과 이유가 적힙니다.
TEST(PpgLink, UnplugDuringShotKeepsSamples) {
  auto line = std::make_shared<FakeSerialLine>();
  FakeClock clock;
  PpgLink link(clock.Fn());
  std::string error;
  ASSERT_TRUE(link.Open(FakeSerialPort::Opener("COM6", line), "COM6", &error)) << error;
  ASSERT_TRUE(link.Restart(&error));
  auto record = std::make_shared<PpgShotRecord>(Timestamp{clock.now.load()});
  link.Attach(record);
  line->Feed(Measure(0, 24, 70));
  PumpUntil(link, [](const PpgReading& x) { return x.samples == 24; });

  clock.now += 1'000'000;
  line->Unplug();
  EXPECT_FALSE(link.Pump(0));
  EXPECT_FALSE(link.IsOpen());
  EXPECT_FALSE(link.Failure().empty());

  clock.now += 2'000'000;
  record->Close(Timestamp{clock.now.load()});
  const auto result = record->Finish();
  EXPECT_EQ(result.values.size(), 24u);
  ASSERT_TRUE(result.disconnected_at.has_value());
  EXPECT_EQ(result.disconnected_at->us, 2'000'000);
  EXPECT_EQ(result.disconnect_reason, link.Failure());
  EXPECT_EQ(result.end_time.us, 4'000'000);
}

// 촬영을 끝낸 뒤에는 표본과 끊긴 시각을 더하지 않습니다. 끊긴 시각은 처음 한 번만 적습니다.
TEST(PpgShotRecord, IgnoresAdditionsAfterClose) {
  PpgShotRecord record(Timestamp{100});
  hwlab::platform::PpgMeasure measure;
  measure.samples = {1, 2, 3};
  measure.spo2 = 97;
  record.AddMeasure(measure, 0, Timestamp{200});
  record.MarkDisconnected(Timestamp{300}, "첫 이유");
  record.MarkDisconnected(Timestamp{400}, "둘째 이유");
  record.Close(Timestamp{500});
  EXPECT_TRUE(record.Closed());
  record.AddMeasure(measure, 3, Timestamp{600});
  record.Close(Timestamp{700});

  const auto result = record.Finish();
  EXPECT_EQ(result.values.size(), 3u);
  EXPECT_EQ(result.common_time.size(), 3u);
  ASSERT_TRUE(result.disconnected_at.has_value());
  EXPECT_EQ(result.disconnected_at->us, 300);
  EXPECT_EQ(result.disconnect_reason, "첫 이유");
  EXPECT_EQ(result.begin_time.us, 100);
  EXPECT_EQ(result.end_time.us, 500);
}

// 장치를 빼면 읽기가 실패해 포트를 닫고 이유를 남깁니다.
TEST(PpgLink, ReportsUnplug) {
  auto line = std::make_shared<FakeSerialLine>();
  PpgLink link;
  std::string error;
  ASSERT_TRUE(link.Open(FakeSerialPort::Opener("COM6", line), "COM6", &error));
  line->Unplug();
  EXPECT_FALSE(link.Pump(10));
  EXPECT_FALSE(link.IsOpen());
  EXPECT_FALSE(link.Failure().empty());
  EXPECT_EQ(link.PortName(), "COM6");
}

// WakeRead는 기다리는 Read를 바로 돌아오게 하고, 기다리는 Read가 없으면 다음 Read 한 번이 기다리지 않습니다.
TEST(FakeSerialPort, WakeReadReturnsWaitingRead) {
  auto line = std::make_shared<FakeSerialLine>();
  auto port = FakeSerialPort::Opener("COM6", line)("COM6", {}, nullptr);
  ASSERT_NE(port, nullptr);
  std::array<uint8_t, 16> buffer{};

  const auto begin = std::chrono::steady_clock::now();
  std::thread waker([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    port->WakeRead();
  });
  EXPECT_EQ(port->Read(buffer, 5000), 0u);
  waker.join();
  EXPECT_LT(std::chrono::steady_clock::now() - begin, std::chrono::seconds(2));
  EXPECT_TRUE(port->IsOpen());

  port->WakeRead();
  const auto again = std::chrono::steady_clock::now();
  EXPECT_EQ(port->Read(buffer, 5000), 0u);
  EXPECT_LT(std::chrono::steady_clock::now() - again, std::chrono::seconds(1));
}

// 가짜 장치는 쓰기 알림을 다음 메시지 시각까지 기다립니다. 쓰기가 오면 바로, StopWaiting 뒤에는 기다리지 않고 돌아옵니다.
TEST(FakeSerialLine, WaitForWritesWakesOnWrite) {
  auto line = std::make_shared<FakeSerialLine>();
  auto port = FakeSerialPort::Opener("COM6", line)("COM6", {}, nullptr);
  ASSERT_NE(port, nullptr);
  const auto far = std::chrono::steady_clock::now() + std::chrono::seconds(10);

  std::thread writer([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    port->Write(hwlab::platform::BuildPpgStop(), 100);
  });
  EXPECT_EQ(line->WaitForWrites(0, far), 1u);
  writer.join();
  EXPECT_EQ(line->WaitForWrites(0, far), 1u);  // 이미 본 수보다 많으면 기다리지 않음

  const auto soon = std::chrono::steady_clock::now() + std::chrono::milliseconds(30);
  EXPECT_EQ(line->WaitForWrites(1, soon), 1u);  // 시각이 지나면 돌아옴
  line->StopWaiting();
  const auto begin = std::chrono::steady_clock::now();
  EXPECT_EQ(line->WaitForWrites(1, far), 1u);
  EXPECT_LT(std::chrono::steady_clock::now() - begin, std::chrono::seconds(1));
}

// 장치 스레드는 연결, 장치 확인, 시작, 표본 수신을 거치며 상태가 바뀔 때만 스냅숏을 보내고, 표본이 오는 동안 파형을 보냅니다.
TEST(PpgSensor, SendsSnapshotsOnlyWhenStateChanges) {
  auto line = std::make_shared<FakeSerialLine>();
  FakePpgDevice device(line);
  Received received;
  PpgSensor sensor(FakeSerialPort::Opener("COM6", line), {}, received.Callbacks());
  sensor.Connect("COM6");
  // 포트를 열면 정보 요청만 보내고, 장치가 답한 뒤 중지와 시작을 보냅니다.
  ASSERT_GE(line->WaitForWrites(0, Soon()), 1u);
  EXPECT_TRUE(OnlyInfoRequests(line->Writes()));
  device.HandleCommands();
  ASSERT_GE(line->WaitForWrites(2, Soon()), 3u);
  ASSERT_TRUE(WaitUntil([&] { return received.Last() == PpgState::kWaiting; }));
  device.HandleCommands();
  for (int i = 0; i < 6; ++i) device.EmitMeasurement(24, 70, 98);
  ASSERT_TRUE(WaitUntil([&] { return received.Last() == PpgState::kStreaming; }));
  ASSERT_TRUE(WaitUntil([&] {
    std::lock_guard<std::mutex> lock(received.mutex);
    return received.waves > 0;
  }));

  std::lock_guard<std::mutex> lock(received.mutex);
  ASSERT_GE(received.snapshots.size(), 5u);
  EXPECT_EQ(received.snapshots[0].state, PpgState::kOff);
  EXPECT_EQ(received.snapshots[1].state, PpgState::kOpening);
  EXPECT_EQ(received.snapshots[1].port, "COM6");
  EXPECT_EQ(received.snapshots[2].state, PpgState::kChecking);
  EXPECT_EQ(received.snapshots[3].state, PpgState::kWaiting);
  EXPECT_EQ(received.snapshots.back().heart_rate, 70);
  for (size_t i = 1; i < received.snapshots.size(); ++i) {
    EXPECT_FALSE(received.snapshots[i] == received.snapshots[i - 1]) << i;
  }
}

// 장치 스레드가 촬영 기록에 표본을 더하고, 녹화 중 장치가 빠져도 기록은 남아 끊긴 시각이 적히며 상태는 kFailed가 됩니다.
TEST(PpgSensor, KeepsShotRecordWhenUnpluggedDuringShot) {
  auto line = std::make_shared<FakeSerialLine>();
  FakePpgDevice device(line);
  Received received;
  PpgSensor sensor(FakeSerialPort::Opener("COM6", line), {}, received.Callbacks());
  sensor.Connect("COM6");
  ASSERT_GE(line->WaitForWrites(0, Soon()), 1u);
  device.HandleCommands();
  ASSERT_GE(line->WaitForWrites(2, Soon()), 3u);
  device.HandleCommands();
  ASSERT_TRUE(WaitUntil([&] { return received.Last() == PpgState::kWaiting; }));

  auto record = std::make_shared<PpgShotRecord>(Timestamp{0});
  sensor.BeginShot(record);
  // 촬영 버튼은 장치를 다시 시작합니다(중지와 시작).
  ASSERT_GE(line->WaitForWrites(4, Soon()), 5u);
  device.HandleCommands();
  device.EmitMeasurement(24, 70, 98);
  device.EmitMeasurement(24, 70, 98);
  ASSERT_TRUE(WaitUntil([&] { return record->Finish().values.size() == 48; }));

  line->Unplug();
  ASSERT_TRUE(WaitUntil([&] { return received.Last() == PpgState::kFailed; }));
  record->Close(Timestamp{std::numeric_limits<int64_t>::max()});
  sensor.EndShot();
  const auto result = record->Finish();
  EXPECT_EQ(result.values.size(), 48u);
  ASSERT_TRUE(result.disconnected_at.has_value());
  EXPECT_FALSE(result.disconnect_reason.empty());
  EXPECT_EQ(result.device_info, "200000270270320");
}

// 직렬 드라이버가 멈춰 중지 명령 쓰기가 돌아오지 않아도, 소멸자는 kQuitWaitUs 뒤 장치 스레드를 놓아 두고 돌아옵니다.
TEST(PpgSensor, DestructorDoesNotWaitForStuckPort) {
  auto line = std::make_shared<FakeSerialLine>();
  FakePpgDevice device(line);
  auto sensor = std::make_unique<PpgSensor>(FakeSerialPort::Opener("COM6", line), nullptr, PpgSensor::Callbacks{});
  sensor->Connect("COM6");
  ASSERT_GE(line->WaitForWrites(0, Soon()), 1u);
  device.HandleCommands();
  ASSERT_GE(line->WaitForWrites(2, Soon()), 3u);
  device.HandleCommands();

  line->HoldWrites();
  const auto begin = std::chrono::steady_clock::now();
  sensor.reset();
  const auto waited = std::chrono::steady_clock::now() - begin;
  EXPECT_GE(waited, std::chrono::microseconds(PpgSensor::kQuitWaitUs - 50'000));
  EXPECT_LT(waited, std::chrono::microseconds(PpgSensor::kQuitWaitUs + 1'500'000));

  // 놓아 둔 장치 스레드가 끝나도록 풀어 줍니다. 장치 스레드는 자기 상태만 참조합니다.
  line->ReleaseWrites();
  ASSERT_TRUE(WaitUntil([&] { return line->Writes().size() == 4u; }));
  EXPECT_EQ(line->Writes()[3][4], 'P');
}

// 장치를 확인하기 전에는 정보 요청을 kPpgInfoIntervalUs마다 보내고, 연 뒤 kPpgPreparingAfterUs가 지나면 준비 중입니다.
// 정보 응답을 받으면 장치를 확인하고 장치 정보를 남기며, 그 뒤로는 정보 요청을 보내지 않습니다.
TEST(PpgLink, AsksDeviceInfoUntilDeviceAnswers) {
  using hwlab::platform::kPpgInfoIntervalUs;
  using hwlab::platform::kPpgPreparingAfterUs;
  auto line = std::make_shared<FakeSerialLine>();
  FakeClock clock;
  PpgLink link(clock.Fn());
  std::string error;
  ASSERT_TRUE(link.Open(FakeSerialPort::Opener("COM6", line), "COM6", &error)) << error;
  EXPECT_FALSE(link.Found());
  EXPECT_EQ(link.Snapshot().state, PpgState::kChecking);
  link.AskDeviceInfo();
  link.AskDeviceInfo();
  ASSERT_EQ(line->Writes().size(), 1u);
  EXPECT_TRUE(OnlyInfoRequests(line->Writes()));

  clock.now += kPpgPreparingAfterUs;
  EXPECT_EQ(link.Snapshot().state, PpgState::kPreparing);
  link.AskDeviceInfo();
  EXPECT_EQ(line->Writes().size(), 2u);

  line->Feed(Info("100002280280320"));
  PumpUntil(link, [&](const PpgReading&) { return link.Found(); });
  EXPECT_TRUE(link.Found());
  EXPECT_EQ(link.DeviceInfo(), "100002280280320");
  EXPECT_EQ(link.Snapshot().state, PpgState::kWaiting);
  clock.now += kPpgInfoIntervalUs;
  link.AskDeviceInfo();
  EXPECT_EQ(line->Writes().size(), 2u);

  // 촬영 기록을 붙이면 받아 둔 장치 정보가 기록에 남습니다.
  auto record = std::make_shared<PpgShotRecord>(Timestamp{0});
  link.Attach(record);
  record->Close(Timestamp{clock.now.load()});
  EXPECT_EQ(record->Finish().device_info, "100002280280320");

  // 다시 열면 장치 확인을 처음부터 합니다.
  link.Detach();
  ASSERT_TRUE(link.Open(FakeSerialPort::Opener("COM6", line), "COM6", &error)) << error;
  EXPECT_FALSE(link.Found());
  EXPECT_TRUE(link.DeviceInfo().empty());
}

// 앞 연결이 장치를 측정 중으로 남기면 측정 데이터로 장치를 확인합니다. 장치 정보를 받지 못했으므로 정보 요청을 한 번만 더 보냅니다.
TEST(PpgLink, AsksDeviceInfoOnceWhenFoundByMeasureData) {
  auto line = std::make_shared<FakeSerialLine>();
  FakeClock clock;
  PpgLink link(clock.Fn());
  std::string error;
  ASSERT_TRUE(link.Open(FakeSerialPort::Opener("COM6", line), "COM6", &error)) << error;
  line->Feed(Measure(0, 24, 70));
  PumpUntil(link, [&](const PpgReading&) { return link.Found(); });
  ASSERT_TRUE(link.Found());
  EXPECT_EQ(link.Reading().samples, 0);  // 시작하기 전에 받은 표본은 세지 않음
  link.AskDeviceInfo();
  link.AskDeviceInfo();
  clock.now += hwlab::platform::kPpgInfoIntervalUs;
  link.AskDeviceInfo();
  EXPECT_EQ(line->Writes().size(), 1u);
  EXPECT_TRUE(OnlyInfoRequests(line->Writes()));
}

// 전원을 켠 직후처럼 장치가 답하지 않는 동안에는 정보 요청만 보내고 준비 중을 보입니다. 장치가 초기화를 끝내고 장치 정보와
// 상태 'o'를 보내면 그때 중지와 시작을 보내 파형을 받습니다.
TEST(PpgSensor, StartsAfterDeviceFinishesBooting) {
  using hwlab::platform::kPpgInfoIntervalUs;
  using hwlab::platform::kPpgPreparingAfterUs;
  auto line = std::make_shared<FakeSerialLine>();
  FakePpgDevice device(line);
  device.StartBooting();
  FakeClock clock;
  Received received;
  PpgSensor sensor(FakeSerialPort::Opener("COM6", line), {}, received.Callbacks(), clock.Fn());
  sensor.Connect("COM6");
  ASSERT_GE(line->WaitForWrites(0, Soon()), 1u);
  ASSERT_TRUE(WaitUntil([&] { return received.Last() == PpgState::kChecking; }));

  clock.now += kPpgPreparingAfterUs;
  ASSERT_TRUE(WaitUntil([&] { return received.Last() == PpgState::kPreparing; }));
  ASSERT_GE(line->WaitForWrites(1, Soon()), 2u);
  clock.now += kPpgInfoIntervalUs;
  ASSERT_GE(line->WaitForWrites(2, Soon()), 3u);
  device.HandleCommands();
  EXPECT_TRUE(OnlyInfoRequests(line->Writes()));
  EXPECT_EQ(device.StartCount(), 0);
  EXPECT_EQ(device.StopCount(), 0);

  const size_t asked = line->Writes().size();
  device.FinishBooting();
  ASSERT_GE(line->WaitForWrites(asked + 1, Soon()), asked + 2);
  device.HandleCommands();
  EXPECT_EQ(device.StartCount(), 1);
  for (int i = 0; i < 6; ++i) device.EmitMeasurement(24, 70, 98);
  ASSERT_TRUE(WaitUntil([&] { return received.Last() == PpgState::kStreaming; }));
}

// 조명 보드처럼 PPG 장치가 아닌 포트에는 정보 요청만 보냅니다. kPpgFindTimeoutUs 동안 답이 없으면 중지와 시작 없이 포트를
// 닫고 찾지 못했다고 알립니다.
TEST(PpgSensor, ClosesPortWithoutStartWhenNoDeviceAnswers) {
  auto line = std::make_shared<FakeSerialLine>();
  FakeClock clock;
  Received received;
  PpgSensor sensor(FakeSerialPort::Opener("COM5", line), {}, received.Callbacks(), clock.Fn());
  sensor.Connect("COM5");
  ASSERT_GE(line->WaitForWrites(0, Soon()), 1u);
  ASSERT_TRUE(WaitUntil([&] { return received.Last() == PpgState::kChecking; }));

  clock.now += hwlab::platform::kPpgFindTimeoutUs;
  ASSERT_TRUE(WaitUntil([&] { return received.Last() == PpgState::kFailed; }));
  const PpgSnapshot last = received.LastSnapshot();
  EXPECT_EQ(last.port, "COM5");
  EXPECT_EQ(last.problem, "이 포트에서 PPG 장치를 찾지 못했습니다");
  EXPECT_TRUE(OnlyInfoRequests(line->Writes()));
}

// 장치를 확인하기 전에 촬영 버튼을 누르고 녹화 중에 장치가 답하면, 녹화 중인 파형을 바꾸지 않도록 시작하지 않고 장치 정보만
// 기록에 남깁니다. 촬영을 끝내면 시작합니다.
TEST(PpgSensor, StartsAfterShotWhenDeviceAnswersDuringShot) {
  auto line = std::make_shared<FakeSerialLine>();
  FakePpgDevice device(line);
  device.StartBooting();
  Received received;
  PpgSensor sensor(FakeSerialPort::Opener("COM6", line), {}, received.Callbacks());
  sensor.Connect("COM6");
  ASSERT_GE(line->WaitForWrites(0, Soon()), 1u);

  auto record = std::make_shared<PpgShotRecord>(Timestamp{0});
  sensor.BeginShot(record);
  ASSERT_TRUE(WaitUntil([&] { return sensor.PendingSinceUs() == 0; }));
  device.FinishBooting();
  ASSERT_TRUE(WaitUntil([&] { return record->Finish().device_info == "200000270270320"; }));
  ASSERT_TRUE(WaitUntil([&] { return received.Last() == PpgState::kWaiting; }));
  EXPECT_TRUE(OnlyInfoRequests(line->Writes()));

  const size_t before = line->Writes().size();
  record->Close(Timestamp{std::numeric_limits<int64_t>::max()});
  sensor.EndShot();
  ASSERT_GE(line->WaitForWrites(before + 1, Soon()), before + 2);
  device.HandleCommands();
  EXPECT_EQ(device.StartCount(), 1);
  EXPECT_TRUE(record->Finish().values.empty());
}

// 기록을 CSV로 쓰고 다시 읽으면 같은 값이고, 열 이름이 다른 파일은 읽지 않습니다.
TEST(PpgSensor, CsvRoundTrip) {
  hwlab::platform::PpgRecord record;
  for (int i = 0; i < 5; ++i) {
    record.values.push_back(static_cast<uint16_t>(1000 + i));
    record.device_time.push_back(Timestamp{i * 2000});
    record.common_time.push_back(Timestamp{7'000'000 + i * 2000});
    record.measure_error.push_back(i == 4 ? 'f' : 0);
    record.heart_rate.push_back(72);
    record.spo2.push_back(98);
  }
  const auto dir = std::filesystem::temp_directory_path() / "hwlab_test_ppg";
  std::filesystem::create_directories(dir);
  const std::string path = (dir / "a_ppg.csv").string();
  std::string error;
  ASSERT_TRUE(hwlab::platform::WritePpgCsv(path, record, &error)) << error;
  const auto csv = hwlab::platform::ReadPpgCsv(path, &error);
  ASSERT_TRUE(csv.has_value()) << error;
  ASSERT_EQ(csv->value.size(), 5u);
  EXPECT_EQ(csv->value[2], 1002);
  EXPECT_EQ(csv->common_time_us[4], 7'008'000);
  EXPECT_EQ(csv->measure_error[4], 'f');
  EXPECT_EQ(csv->heart_rate[0], 72);
  EXPECT_EQ(csv->spo2[3], 98);

  const std::string bad = (dir / "bad_ppg.csv").string();
  std::FILE* f = std::fopen(bad.c_str(), "wb");
  ASSERT_NE(f, nullptr);
  std::fputs("t,v\n1,2\n", f);
  std::fclose(f);
  EXPECT_FALSE(hwlab::platform::ReadPpgCsv(bad, &error).has_value());
  EXPECT_FALSE(hwlab::platform::ReadPpgCsv((dir / "none.csv").string()).has_value());
}
