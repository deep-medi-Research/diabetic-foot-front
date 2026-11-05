#ifndef HWLAB_PLATFORM_FAKE_SERIAL_PORT_H_
#define HWLAB_PLATFORM_FAKE_SERIAL_PORT_H_

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "hwlab/platform/serial_port.h"

namespace hwlab::platform {

/**
 * 가짜 직렬 포트의 장치 쪽입니다. 테스트가 쥐고 있으면서 쓴 바이트를 보고, 받을 바이트를 넣고, 실패를 주입합니다.
 *
 * 포트를 닫거나 다시 열어도 같은 회선을 계속 씁니다. 여러 스레드에서 불러도 됩니다.
 */
class FakeSerialLine {
 public:
  /** 지금까지 포트에 쓴 모든 바이트입니다(열고 닫은 횟수와 무관하게 이어 붙임). */
  std::vector<uint8_t> Written();

  /** 성공한 Write 호출마다 나눈 바이트입니다. */
  std::vector<std::vector<uint8_t>> Writes();

  /** 장치가 보낸 바이트를 넣습니다. Read가 이 순서대로 돌려줍니다. */
  void Feed(const std::vector<uint8_t>& bytes);

  /**
   * 이 횟수만큼 Write가 더 성공한 뒤부터 쓰기를 실패시킵니다. -1이면 실패시키지 않습니다.
   *
   * @param writes 더 성공할 Write 횟수. 0이면 다음 Write부터 실패
   * @param disconnect true면 장치가 빠진 것처럼 포트를 닫고, false면 시간 초과처럼 false만 돌려줍니다
   */
  void FailWritesAfter(int32_t writes, bool disconnect);

  /** 장치를 뺍니다. 열린 포트는 다음 Write나 Read에서 닫히고, 이후 열기는 실패합니다. */
  void Unplug();

  /** 장치를 다시 꽂습니다. 실패 주입도 지웁니다. */
  void Plug();

  /** 이 회선으로 포트를 연 횟수입니다. */
  int32_t OpenCount();

  /**
   * 성공한 Write 수가 seen보다 많아지거나, until이 지나거나, StopWaiting을 부를 때까지 기다립니다.
   *
   * 가짜 장치가 명령을 반복 확인하지 않고 다음 메시지 시각까지 기다리게 하기 위함입니다.
   *
   * @param seen 부른 쪽이 이미 본 Write 수
   * @param until 기다리는 끝 시각
   * @return 지금까지 성공한 Write 수
   */
  size_t WaitForWrites(size_t seen, std::chrono::steady_clock::time_point until);

  /** WaitForWrites로 기다리는 쪽을 깨우고, 이후의 WaitForWrites는 기다리지 않습니다. 가짜 장치를 끝낼 때 씁니다. */
  void StopWaiting();

  /**
   * true면 Write가 ReleaseWrites를 부를 때까지 돌아오지 않습니다. 드라이버가 멈춰 쓰기가 끝나지 않는 경우를 시험합니다.
   *
   * @note 2026-10-20 기존 앱 실기에서 FTDI 드라이버가 멈춰 쓰기와 닫기가 돌아오지 않았습니다.
   */
  void HoldWrites();

  /** HoldWrites로 멈춘 Write를 모두 돌려보냅니다. 이후 Write는 멈추지 않습니다. */
  void ReleaseWrites();

 private:
  friend class FakeSerialPort;
  std::mutex mutex_;
  std::condition_variable arrived_;
  std::condition_variable written_;  // Write 성공, StopWaiting, ReleaseWrites
  std::vector<std::vector<uint8_t>> writes_;
  std::deque<uint8_t> inbound_;
  int32_t writes_before_failure_ = -1;
  bool failure_disconnects_ = false;
  bool plugged_ = true;
  bool stop_waiting_ = false;
  bool hold_writes_ = false;
  int32_t open_count_ = 0;
};

/** 장비 없이 직렬 포트 사용자를 시험하는 포트입니다. 장치 쪽 동작은 FakeSerialLine이 정합니다. */
class FakeSerialPort final : public SerialPort {
 public:
  FakeSerialPort(std::string name, std::shared_ptr<FakeSerialLine> line);

  std::string Name() const override { return name_; }
  bool IsOpen() const override;
  bool Write(std::span<const uint8_t> bytes, int32_t timeout_ms) override;
  std::optional<size_t> Read(std::span<uint8_t> buffer, int32_t timeout_ms) override;
  void WakeRead() override;
  void Close() override;

  /**
   * 이름이 name인 포트만 line으로 여는 함수를 만듭니다. 다른 이름이나 빠진 장치는 열기에 실패합니다.
   *
   * @param name 열 수 있는 포트 이름
   * @param line 장치 쪽
   * @return SerialPortOpener로 넘길 함수
   */
  static SerialPortOpener Opener(std::string name, std::shared_ptr<FakeSerialLine> line);

 private:
  std::string name_;
  std::shared_ptr<FakeSerialLine> line_;
  bool open_ = true;   // line_->mutex_ 아래에서 읽고 씁니다
  bool woken_ = false;  // WakeRead를 불렀고 아직 Read가 돌아오지 않음. line_->mutex_ 아래에서 읽고 씁니다
};

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_FAKE_SERIAL_PORT_H_
