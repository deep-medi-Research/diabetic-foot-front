// PPG 장치를 열어 정해진 시간 동안 측정하고 받은 표본 수, 표본 간격, 심박수, 멈춤 여부를 출력합니다(실기 확인용).
//
//   hwlab_ppg <포트> [초]
//
// 앱의 장치 스레드와 같은 PpgLink를 씁니다. 실험 노트북의 USB 포트에 바로 꽂으면 파형이 0.2~0.7초 뒤 멈추고, 허브를 거치면 정상입니다(이슈 #4).
// 앱과 같이 정보 요청으로 장치를 확인한 뒤 시작하고, 장치가 답할 때까지 걸린 시간과 장치 정보를 먼저 출력합니다. 장치 전원을
// 켜기 전에 실행하면 전원을 켠 뒤 답할 때까지의 시간이 나옵니다. kPpgFindTimeoutUs 동안 답이 없으면 시작 명령 없이 끝냅니다.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "hwlab/platform/ppg_sensor.h"
#include "hwlab/platform/serial_port.h"

namespace {

int64_t NowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "사용법: hwlab_ppg <포트> [초]\n");
    return 2;
  }
  const std::string port = argv[1];
  const int seconds = argc > 2 ? std::atoi(argv[2]) : 10;
  hwlab::platform::PpgLink link;
  std::string error;
  if (!link.Open(hwlab::platform::OpenSerialPort, port, &error)) {
    std::fprintf(stderr, "열지 못했습니다: %s\n", error.c_str());
    return 1;
  }
  while (!link.Found()) {
    if (link.OpenUs() >= hwlab::platform::kPpgFindTimeoutUs) {
      std::fprintf(stderr, "%.0f초 동안 PPG 장치가 답하지 않았습니다\n", hwlab::platform::kPpgFindTimeoutUs / 1e6);
      return 1;
    }
    link.AskDeviceInfo();
    if (!link.Pump(100)) {
      std::fprintf(stderr, "포트가 닫혔습니다: %s\n", link.Failure().c_str());
      return 1;
    }
  }
  std::printf("장치 확인 %.2f초  장치 정보 %s\n", link.OpenUs() / 1e6,
              link.DeviceInfo().empty() ? "-" : link.DeviceInfo().c_str());
  std::fflush(stdout);
  if (!link.Restart(&error)) {
    std::fprintf(stderr, "시작 명령 실패: %s\n", error.c_str());
    return 1;
  }
  auto record = std::make_shared<hwlab::platform::PpgShotRecord>(hwlab::Timestamp{NowUs()});
  link.Attach(record);
  const int64_t begin = NowUs();
  for (int i = 0; i < seconds; ++i) {
    while (NowUs() - begin < (i + 1) * 1'000'000LL) {
      if (!link.Pump(100)) {
        std::fprintf(stderr, "포트가 닫혔습니다: %s\n", link.Failure().c_str());
        break;
      }
    }
    const auto r = link.Reading();
    std::printf("%2d초  표본 %lld  수신 %s  상태 %c  심박 %d  SpO2 %d  측정 오류 %d  버린 바이트 %lld\n", i + 1,
                static_cast<long long>(r.samples), r.streaming ? "중" : "멈춤", r.status ? r.status : '-', r.heart_rate,
                r.spo2, r.measure_error, static_cast<long long>(r.counts.skipped_bytes));
    std::fflush(stdout);
    if (!link.IsOpen()) break;
  }
  record->Close(hwlab::Timestamp{NowUs()});
  link.Detach();
  link.Close();
  const auto result = record->Finish();
  const double span = result.common_time.size() > 1
                          ? (result.common_time.back().us - result.common_time.front().us) / 1e6
                          : 0;
  std::printf("기록 %zu표본, 공통 시각 구간 %.2f초, 실효 %.1f Hz, 시계 맞춤 rate %.6f, 시계 구간 %zu\n", result.values.size(),
              span, span > 0 ? (result.values.size() - 1) / span : 0.0,
              result.clock.empty() ? 1.0 : result.clock.front().map.rate, result.clock.size());
  return 0;
}
