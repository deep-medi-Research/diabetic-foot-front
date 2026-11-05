#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "hwlab/platform/fake_serial_port.h"
#include "hwlab/platform/light.h"
#include "hwlab/platform/serial_port.h"

namespace {

using namespace std::chrono_literals;
using hwlab::platform::FakeSerialLine;
using hwlab::platform::FakeSerialPort;
using hwlab::platform::Light;
using hwlab::platform::LightCommand;
using hwlab::platform::MakeLightCommand;

using Bytes = std::vector<uint8_t>;

struct Rig {
  std::shared_ptr<FakeSerialLine> line = std::make_shared<FakeSerialLine>();
  Light light{FakeSerialPort::Opener("COM7", line)};
};

// 100%는 FE 64, 0%는 FE 00입니다.
TEST(LightCommand, PercentIsSecondByte) {
  EXPECT_EQ(MakeLightCommand(100), (LightCommand{0xFE, 0x64}));
  EXPECT_EQ(MakeLightCommand(0), (LightCommand{0xFE, 0x00}));
  EXPECT_EQ(MakeLightCommand(37), (LightCommand{0xFE, 37}));
}

// 범위 밖의 값은 0과 100으로 자릅니다.
TEST(LightCommand, ClampsOutOfRange) {
  EXPECT_EQ(MakeLightCommand(150), (LightCommand{0xFE, 0x64}));
  EXPECT_EQ(MakeLightCommand(-5), (LightCommand{0xFE, 0x00}));
  EXPECT_EQ(MakeLightCommand(176), (LightCommand{0xFE, 0x64}));
}

// 0~100의 둘째 바이트에는 단계 명령(B0, B1)과 같은 값이 없습니다.
TEST(LightCommand, NeverProducesStepCommand) {
  for (int32_t percent = -300; percent <= 300; ++percent) {
    const LightCommand command = MakeLightCommand(percent);
    EXPECT_LE(command[1], 0x64) << percent;
  }
}

// 포트를 열면 그 자리에서 현재 밝기를 한 번 보냅니다.
TEST(Light, OpenSendsCurrentPercent) {
  Rig rig;
  std::string error;

  ASSERT_TRUE(rig.light.Open("COM7", 70, &error)) << error;

  EXPECT_EQ(rig.line->Writes(), (std::vector<Bytes>{{0xFE, 70}}));
  EXPECT_TRUE(rig.light.IsConnected());
  EXPECT_EQ(rig.light.PortName(), "COM7");
  EXPECT_EQ(rig.light.LastSentPercent(), 70);
}

// 열 때 넘긴 값도 자른 뒤에 보내고 기억합니다.
TEST(Light, OpenClampsPercent) {
  Rig rig;

  ASSERT_TRUE(rig.light.Open("COM7", 250, nullptr));

  EXPECT_EQ(rig.line->Written(), (Bytes{0xFE, 0x64}));
  EXPECT_EQ(rig.light.LastSentPercent(), 100);
}

// 밝기를 바꿀 때마다 명령 하나를 보내고 마지막으로 보낸 값을 기억합니다.
TEST(Light, SetPercentSendsAbsoluteCommand) {
  Rig rig;
  ASSERT_TRUE(rig.light.Open("COM7", 50, nullptr));

  ASSERT_TRUE(rig.light.SetPercent(20, nullptr));
  ASSERT_TRUE(rig.light.SetPercent(-1, nullptr));

  EXPECT_EQ(rig.line->Writes(), (std::vector<Bytes>{{0xFE, 50}, {0xFE, 20}, {0xFE, 0}}));
  EXPECT_EQ(rig.light.LastSentPercent(), 0);
}

// 없는 포트는 열지 못하고 아무것도 보내지 않습니다.
TEST(Light, OpenFailsForUnknownPort) {
  Rig rig;
  std::string error;

  EXPECT_FALSE(rig.light.Open("COM9", 50, &error));

  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(rig.light.IsConnected());
  EXPECT_EQ(rig.light.LastSentPercent(), std::nullopt);
  EXPECT_TRUE(rig.line->Written().empty());
}

// 연결 전에는 밝기를 보내지 못하고 마지막으로 보낸 값도 없습니다.
TEST(Light, SetPercentFailsWhenNotConnected) {
  Rig rig;
  std::string error;

  EXPECT_FALSE(rig.light.SetPercent(30, &error));

  EXPECT_FALSE(error.empty());
  EXPECT_EQ(rig.light.LastSentPercent(), std::nullopt);
}

// 쓰기가 실패하면(시간 초과 포함) 연결을 끊고, 마지막으로 보낸 값을 지웁니다.
TEST(Light, WriteFailureDisconnects) {
  Rig rig;
  ASSERT_TRUE(rig.light.Open("COM7", 50, nullptr));
  rig.line->FailWritesAfter(0, false);
  std::string error;

  EXPECT_FALSE(rig.light.SetPercent(60, &error));

  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(rig.light.IsConnected());
  EXPECT_EQ(rig.light.LastSentPercent(), std::nullopt);
  EXPECT_EQ(rig.light.PortName(), "");
  // 끊긴 뒤의 호출은 포트에 가지 않습니다.
  EXPECT_FALSE(rig.light.SetPercent(70, nullptr));
  EXPECT_EQ(rig.line->Writes(), (std::vector<Bytes>{{0xFE, 50}}));
}

// 장치를 빼면 다음 쓰기에서 연결 끊김이 되고, 다시 꽂고 열면 현재 값을 다시 보냅니다.
TEST(Light, ReopenAfterUnplugSendsAgain) {
  Rig rig;
  ASSERT_TRUE(rig.light.Open("COM7", 40, nullptr));
  rig.line->Unplug();

  EXPECT_FALSE(rig.light.SetPercent(45, nullptr));
  EXPECT_FALSE(rig.light.IsConnected());
  EXPECT_FALSE(rig.light.Open("COM7", 45, nullptr));

  rig.line->Plug();
  ASSERT_TRUE(rig.light.Open("COM7", 45, nullptr));
  EXPECT_EQ(rig.line->Writes(), (std::vector<Bytes>{{0xFE, 40}, {0xFE, 45}}));
  EXPECT_EQ(rig.line->OpenCount(), 2);
}

// 열 때의 첫 쓰기가 실패하면 열기 실패로 보고 포트를 닫습니다.
TEST(Light, OpenFailsWhenFirstWriteFails) {
  Rig rig;
  rig.line->FailWritesAfter(0, true);

  EXPECT_FALSE(rig.light.Open("COM7", 50, nullptr));

  EXPECT_FALSE(rig.light.IsConnected());
  EXPECT_EQ(rig.light.LastSentPercent(), std::nullopt);
}

// 닫으면 마지막으로 보낸 값이 없어집니다. 사이드카에 근거 없는 값을 남기지 않기 위함입니다.
TEST(Light, CloseForgetsLastSent) {
  Rig rig;
  ASSERT_TRUE(rig.light.Open("COM7", 80, nullptr));

  rig.light.Close();

  EXPECT_FALSE(rig.light.IsConnected());
  EXPECT_EQ(rig.light.LastSentPercent(), std::nullopt);
}

// 받은 바이트가 있으면 기다리지 않고 버퍼 크기까지 돌려주고, 나머지는 다음 읽기에 남깁니다.
TEST(FakeSerialPort, ReadReturnsAvailableBytesUpToBuffer) {
  auto line = std::make_shared<FakeSerialLine>();
  auto port = FakeSerialPort::Opener("COM3", line)("COM3", {}, nullptr);
  ASSERT_NE(port, nullptr);
  line->Feed({1, 2, 3, 4, 5});
  std::array<uint8_t, 3> buffer{};

  EXPECT_EQ(port->Read(buffer, 1000), 3u);
  EXPECT_EQ(buffer, (std::array<uint8_t, 3>{1, 2, 3}));
  EXPECT_EQ(port->Read(buffer, 1000), 2u);
  EXPECT_EQ(buffer[0], 4);
  EXPECT_EQ(buffer[1], 5);
}

// 받은 바이트가 없으면 시간 제한까지 기다린 뒤 0을 돌려줍니다.
TEST(FakeSerialPort, ReadTimesOutWithZero) {
  auto line = std::make_shared<FakeSerialLine>();
  auto port = FakeSerialPort::Opener("COM3", line)("COM3", {}, nullptr);
  std::array<uint8_t, 8> buffer{};

  EXPECT_EQ(port->Read(buffer, 0), 0u);
  EXPECT_EQ(port->Read(buffer, 20), 0u);
}

// 기다리는 동안 바이트가 오면 시간 제한 전에 돌려줍니다.
TEST(FakeSerialPort, ReadWakesWhenBytesArrive) {
  auto line = std::make_shared<FakeSerialLine>();
  auto port = FakeSerialPort::Opener("COM3", line)("COM3", {}, nullptr);
  std::array<uint8_t, 8> buffer{};
  std::thread device([&] {
    std::this_thread::sleep_for(30ms);
    line->Feed({9});
  });

  const auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(port->Read(buffer, 5000), 1u);
  EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);
  device.join();
}

// 장치가 빠지면 읽기는 nullopt를 돌려주고 포트는 닫힙니다.
TEST(FakeSerialPort, ReadReportsUnplug) {
  auto line = std::make_shared<FakeSerialLine>();
  auto port = FakeSerialPort::Opener("COM3", line)("COM3", {}, nullptr);
  std::array<uint8_t, 8> buffer{};
  line->Unplug();

  EXPECT_EQ(port->Read(buffer, 1000), std::nullopt);
  EXPECT_FALSE(port->IsOpen());
}

#ifdef _WIN32
// 실제 포트 목록을 조회합니다. 포트를 열지 않습니다. 목록이 비어 있어도 통과입니다.
TEST(SerialPortWindows, ListsPortsWithoutOpening) {
  const auto ports = hwlab::platform::ListSerialPorts();
  for (const auto& port : ports) {
    EXPECT_EQ(port.name.rfind("COM", 0), 0u) << port.name;
  }
}

#endif

}  // namespace
