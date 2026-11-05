#ifndef HWLAB_RECORD_SIDECAR_H_
#define HWLAB_RECORD_SIDECAR_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "hwlab/camera_settings.h"
#include "hwlab/pixel/brightness.h"
#include "hwlab/pixel/convert.h"
#include "hwlab/pixel/rect.h"
#include "hwlab/record/avi.h"
#include "hwlab/stream.h"
#include "hwlab/timestamp.h"
#include "hwlab/timing/clock_map.h"
#include "hwlab/timing/frame_gap.h"

namespace hwlab::record {

/** 영상이 담은 발입니다. 환자 기준 왼발과 오른발입니다. */
enum class FootSide : uint8_t {
  kLeft,
  kRight,
};

/** 촬영 조명을 누가 켜고 정했는지입니다. 매니페스트 lighting.source와 사이드카 light.source가 같은 값을 씁니다. */
enum class LightSource : uint8_t {
  kUnknown,
  kExternal,  // 앱 밖에서 사람이 켬(예: LED 보드에 전원만 넣음). 앱은 밝기를 보내지도 읽지도 않음
  kApp,       // 앱이 직렬 포트로 밝기를 보냄(platform::Light)
};

/** 열화상 렌즈 셰이딩 보정을 어떻게 적용했는지입니다(설계 문서 표 2). */
enum class LensShadingSource : uint8_t {
  kNone,       // 이 카메라의 보정 파일이 없어 보정하지 않음
  kLoaded,     // 열 때 이 카메라의 보정 파일을 불러와 적용함
  kCorrected,  // 이번 실행에서 사람이 렌즈를 가리고 보정함
  kFailed,     // 불러오기나 보정이 실패함. 적용된 보정을 알 수 없음(보정이 실패한 뒤 SDK가 앞 보정을 두는지는 확인하지 않음)
};

/**
 * 열화상 사이드카의 렌즈 셰이딩 보정 기록(lens_shading)입니다.
 *
 * @note kLoaded와 kCorrected는 SDK가 성공을 반환했다는 뜻이고, 보정할 때 렌즈 앞이 고른 면이었는지는 알 수 없습니다.
 */
struct LensShadingRecord {
  LensShadingSource source = LensShadingSource::kNone;
  // 적용한 보정 파일의 수정 시각(UTC 유닉스 초). 보정한 날짜를 보이는 데 씁니다. 파일이 없거나 저장하지 못했으면 비웁니다
  std::optional<int64_t> file_unix_s;

  friend bool operator==(const LensShadingRecord&, const LensShadingRecord&) = default;
};

/**
 * RGB 사이드카의 조명 기록(light)입니다.
 *
 * source와 percent는 같은 촬영의 매니페스트 lighting.source, lighting.brightness_percent와 같은 값입니다.
 *
 * @note percent는 표 2의 조명 밝기(LED 보드에 보내는 데 성공한 값)이고 장치에서 읽은 값이 아닙니다.
 *       포트는 COM 이름 대신 USB VID:PID와 일련번호로 남깁니다. COM 번호는 꽂는 USB 단자에 따라 바뀌기 때문입니다.
 */
struct LightRecord {
  LightSource source = LightSource::kUnknown;
  std::optional<int32_t> percent;          // 녹화를 시작할 때의 조명 밝기(0~100). 앱이 보내지 않았으면 비웁니다
  std::optional<uint16_t> usb_vendor_id;   // LED 보드 직렬 변환기의 VID. 모르면 비웁니다
  std::optional<uint16_t> usb_product_id;  // LED 보드 직렬 변환기의 PID. 모르면 비웁니다
  std::string serial;                      // 직렬 변환기의 USB 일련번호. 모르면 빈 문자열(null로 씀)
  bool confirmed = false;                  // 사람이 LED 밝기가 바뀌는 것을 보고 이 포트가 LED 보드임을 확인함
  pixel::LightResponse response = pixel::LightResponse::kUnknown;  // 마지막 LED 변경이 영상 밝기에 나타났는지
  std::optional<pixel::Flicker> flicker;   // 이 영상 파일에 쓴 프레임의 깜빡임 요약. 녹화 뒤 채웁니다
};

/**
 * 파일에 쓴 프레임 하나의 기록입니다.
 *
 * @note 노출과 게인은 프레임에 붙어 오지 않아 프레임마다 남길 수 없습니다.
 */
struct FrameRecord {
  FrameIndex index = 0;      // 스트림을 연 뒤 받은 순서
  Timestamp device_time;     // 장치 또는 드라이버가 붙인 시각
  Timestamp host_time;       // PC가 받은 시각. 장치 시각이 실제 간격과 다른지 대조하는 데 씁니다
  int64_t brightness_sum = 0;
  int64_t clipped_pixels = 0;
};

/** 영상 파일 하나에 대한 기록(사이드카)입니다. 영상 파일과 같은 이름의 JSON으로 저장합니다. */
struct Sidecar {
  std::string video_file;  // 같은 폴더 안의 파일 이름
  std::string device;
  StreamKind stream = StreamKind::kRgb;
  VideoFormat format;
  std::string codec;  // ffv1, uncompressed
  std::optional<FootSide> foot;  // 좌우로 나누지 않은 전체 화면이면 비웁니다
  pixel::Rect crop;
  SettingMode mode = SettingMode::kAuto;
  CameraState settings;  // 녹화 직전에 다시 읽은 값. 자동 맞춤의 노출값은 읽을 방법이 없어 비웁니다
  std::optional<LightRecord> light;  // 조명 기록. RGB 사이드카에만 채우고 열화상 사이드카는 비웁니다(null)
  // 녹화를 끝낸 때 열린 열화상 연결의 렌즈 셰이딩 보정 기록. RGB 사이드카, 보정 기능이 없는 열화상 카메라,
  // 녹화를 끝낸 때 열화상이 끊겨 있던 경우는 비웁니다(null)
  std::optional<LensShadingRecord> lens_shading;
  timing::ClockMap clock;  // 장치 시각을 공통 시간축(PC 시계)으로 옮기는 식. 프레임별 공통 시각은 이 식으로 계산합니다
  std::optional<pixel::Y16Range> display_range;  // 열화상 확인용 영상의 표시 범위(원시값). 열화상이 아니거나 확인용 영상이 없으면 비웁니다
  int64_t target_frames = 0;
  int64_t written_frames = 0;
  int64_t received_frames = 0;  // 녹화 중 받은 프레임 수. 기록 주기로 거른 프레임과 저장 누락을 포함합니다
  int64_t record_period_us = 0;  // 기록 주기(timing::RecordRateRule). 0이면 받은 프레임을 모두 기록합니다
  // RGB 녹화의 첫 프레임과 마지막 프레임 사이 공통 시각에 기록한 프레임 수. 관류 분석은 열화상의 이 장 수를 봅니다
  int64_t written_in_rgb_window = 0;
  Timestamp first_frame;
  Timestamp last_frame;
  int64_t brightness_pixels = 0;  // 프레임마다 밝기를 잰 화소 수(crop 영역)
  int32_t clip_level = 0;
  std::vector<FrameRecord> records;     // 파일에 쓴 순서
  std::vector<timing::FrameGap> gaps;  // 카메라 단 누락
  int64_t dropped_frames = 0;           // 저장 누락
  int32_t buffer_frames = 0;            // 쓰기 대기열 크기
  int32_t max_queue_frames = 0;         // 녹화 중 쓰기 대기열이 가장 길었던 값. buffer_frames에 닿으면 저장 누락이 생깁니다
  int64_t file_bytes = -1;              // 영상 파일 크기. 모르면 -1
  int64_t median_interval_us = 0;       // 받은 프레임 간격의 중앙값. 선언 fps와 다르면 카메라가 fps를 바꾼 것입니다
  timing::FrameRateCheck frame_rate;    // 느린 간격(주기의 1.2배 초과 1.5배 이하)과 1초 창 최저 fps. 받은 프레임 시각으로 잽니다
};

/**
 * 사이드카 JSON을 만듭니다.
 *
 * 프레임별 기록은 값마다 배열 하나로 씁니다(numpy로 바로 읽음). 평균 밝기는 brightness_sum / brightness_pixels입니다.
 * 프레임별 공통 시각(common_time_us)은 timing::ToCommonTime(clock, device_time)이며, 스트림이 달라도 같은 시간축이라
 * RGB 사이드카와 열화상 사이드카의 프레임을 이 값으로 비교합니다.
 *
 * @param sidecar 기록
 * @return JSON 문자열. 같은 입력에는 같은 바이트를 내며, 키 순서는 고정이고 비어 있는 값은 null입니다
 */
std::string BuildSidecarJson(const Sidecar& sidecar);

/** PPG 기록 중 장치가 보낸 상태 메시지나 오류 메시지 하나입니다. */
struct PpgEvent {
  Timestamp host_time;  // PC 수신 시각
  uint8_t code = 0;     // 메시지 코드. 's' 상태, 'e' 오류
  uint8_t value = 0;    // 상태 값('v', 's', 'p', 'u', 'o') 또는 오류 값
};

/**
 * PPG 기록에서 같은 측정 오류 값이 이어진 표본들입니다(측정 오류 구간).
 *
 * @note 장치는 측정 데이터(약 48ms)마다 측정 오류 값 하나를 보내므로 구간은 측정 데이터 단위로 바뀝니다. 측정 데이터 하나의
 *       표본 수는 촬영 네 번에서 평균 23.91~23.98개였습니다(2026-10-21 실측).
 */
struct PpgErrorSpan {
  uint8_t code = 0;  // 측정 오류 값('f' 손가락 빠짐, 's' 신호 나쁨, 'p' 센서 연결 안 됨)
  Timestamp begin;   // 첫 표본의 공통 시각
  Timestamp end;     // 마지막 표본의 공통 시각
  int64_t samples = 0;

  friend bool operator==(const PpgErrorSpan&, const PpgErrorSpan&) = default;
};

/** 공통 시각 구간 하나 안에서 측정 오류 값 하나가 나온 양입니다. */
struct PpgErrorShare {
  uint8_t code = 0;
  int64_t samples = 0;          // 이 값인 표본 수
  int64_t longest_samples = 0;  // 이 값이 가장 길게 이어진 표본 수
  int32_t runs = 0;             // 이 값이 이어진 횟수. 끊겼다가 다시 나오면 하나씩 늘어납니다

  friend bool operator==(const PpgErrorShare&, const PpgErrorShare&) = default;
};

/** 공통 시각 구간 하나(RGB 녹화 구간) 안 PPG 표본의 측정 오류 집계입니다. */
struct PpgErrorWindow {
  Timestamp begin;                   // RGB 첫 장의 공통 시각
  Timestamp end;                     // RGB 마지막 장의 공통 시각
  int64_t samples = 0;               // 구간 안 표본 수. 비율의 분모입니다
  std::vector<PpgErrorShare> codes;  // 구간 안에 나온 측정 오류 값마다 하나. 값 순서입니다

  friend bool operator==(const PpgErrorWindow&, const PpgErrorWindow&) = default;
};

/**
 * 표본마다의 측정 오류에서 측정 오류 구간을 찾습니다.
 *
 * @param times 표본마다 공통 시각. 시각 순서입니다
 * @param errors 표본마다 측정 오류 값(0 정상). times와 길이가 다르면 짧은 쪽까지만 봅니다
 * @return 시각 순서의 측정 오류 구간
 */
std::vector<PpgErrorSpan> FindPpgErrorSpans(std::span<const Timestamp> times, std::span<const uint8_t> errors);

/**
 * 공통 시각 [begin, end] 안 표본의 측정 오류를 값마다 셉니다.
 *
 * @param times 표본마다 공통 시각. 시각 순서입니다
 * @param errors 표본마다 측정 오류 값(0 정상). times와 길이가 다르면 짧은 쪽까지만 봅니다
 * @param begin 구간 시작(포함). RGB 첫 장의 공통 시각
 * @param end 구간 끝(포함). RGB 마지막 장의 공통 시각
 * @return 집계. 구간 안 표본이 없으면 samples가 0이고 codes가 빕니다
 */
PpgErrorWindow SummarizePpgErrors(std::span<const Timestamp> times, std::span<const uint8_t> errors, Timestamp begin,
                                  Timestamp end);

/** PPG 파일 하나의 사이드카에 남기는 값입니다. */
struct PpgSidecar {
  std::string samples_file;  // PPG 파일 이름(폴더 없이)
  std::string port;          // 장치를 연 직렬 포트 이름
  // 장치 정보 응답의 데이터(ASCII 15바이트: MP-SDK 버전 6자리, HRV 기준 6자리, APG 기준 3자리). 받지 못했으면 비웁니다
  std::optional<std::string> device_info;
  int32_t sample_rate_hz = 0;
  Timestamp record_begin;  // 기록을 시작한 PC 시각
  Timestamp record_end;    // 기록을 끝낸 PC 시각
  std::vector<timing::ClockSegment> clock;
  int64_t samples = 0;
  int64_t messages = 0;  // 표본을 담은 측정 데이터 수. 시계 맞춤 표본 수와 같습니다
  Timestamp first_sample;  // 첫 표본의 공통 시각
  Timestamp last_sample;   // 마지막 표본의 공통 시각
  int64_t median_interval_us = 0;      // 공통 시각으로 잰 표본 간격의 중앙값. 장치 시계 속도가 드러납니다
  std::vector<timing::FrameGap> gaps;  // 공통 시각의 표본 간격으로 찾은 누락. 수신 빈틈이 여기에 남습니다
  int64_t measure_error_samples = 0;   // 측정 오류가 0이 아닌 표본 수
  int64_t crc_failures = 0;            // 기록 동안 해석기가 버린 시작 후보와 바이트
  int64_t bad_lengths = 0;
  int64_t implausible = 0;
  int64_t skipped_bytes = 0;
  std::vector<PpgEvent> events;
  std::vector<PpgErrorSpan> error_spans;     // 기록 전체의 측정 오류 구간
  std::optional<PpgErrorWindow> rgb_window;  // RGB 녹화 구간 안 측정 오류 집계. RGB 사이드카를 읽지 못했으면 비웁니다
};

/**
 * PPG 사이드카 JSON을 만듭니다.
 *
 * 시계 구간마다 식(last_device_us, offset_us, rate, max_residual_us)을 남겨 CSV의 공통 시각을 다시 계산하고 검사할 수 있게 합니다.
 * 측정 오류 구간(measure_errors.spans)은 공통 시각과 함께, RGB 녹화 구간을 알면 RGB 첫 장을 0초로 잰 초(rgb_begin_s,
 * rgb_end_s)로도 씁니다. 결과 창의 가로축과 같은 값입니다.
 *
 * @param sidecar 기록
 * @return JSON 문자열. 같은 입력에는 같은 바이트를 내며 키 순서는 고정입니다
 */
std::string BuildPpgSidecarJson(const PpgSidecar& sidecar);

}  // namespace hwlab::record

#endif  // HWLAB_RECORD_SIDECAR_H_
