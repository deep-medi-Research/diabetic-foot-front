# 촬영 폴더

**촬영 한 번은 폴더 하나입니다. 안에 영상, 사이드카 JSON, 매니페스트, 열화상 파일, PPG 파일이 들어가고, 분석이 끝나면 `perfusion/` 폴더가 생깁니다.** 장비 없이 `--fake`로 찍은 촬영 한 번은 약 490 MB를 쓰고, 그중 98%가 열화상 파일 네 개입니다. 이 문서는 코드가 실제로 쓰는 파일 이름과 필드 이름을 적었고, 근거 코드는 `core/src/record/shot.cc`, `core/src/record/sidecar.cc`, `platform/src/shot_folder.cc`, `platform/src/thermal_stream.cc`, `platform/src/ppg_sensor.cc`입니다.

## 폴더 구조

```text
<저장 위치>/
  <이름>_<생년월일>/<발목 혈압 단계>/<날짜_시각>/     촬영 폴더
    <날짜_시각>_shot.json                              매니페스트
    <날짜_시각>_left.mkv, _left.json, _left.mkv.queue.txt     왼발 영상, 사이드카, 쓰기 기록
    <날짜_시각>_right.mkv, _right.json, _right.mkv.queue.txt  오른발
    <날짜_시각>_full.mkv, _full.json, _full.mkv.queue.txt     전체 화면(--save-full일 때만)
    <날짜_시각>_thermal.raw16                          열화상 원시값
    <날짜_시각>_thermal.json                           열화상 사이드카
    <날짜_시각>_thermal.avi                            열화상 확인용 영상
    <날짜_시각>_thermal_camera_temp.csv                카메라 온도(카메라가 주는 장비만)
    thermal_left.npy, thermal_right.npy                열화상 좌우 발(분석 입력)
    <날짜_시각>_ppg.csv, _ppg.json                     PPG(PPG 장치를 연 촬영만)
    perfusion/<판>/                                    분석 결과(분석이 끝난 뒤)
  results/                                             분석 결과 사본(저장 위치 바로 아래)
```

- **폴더 이름**<br>이름과 생년월일은 환자 정보이므로 폴더 이름에 환자 정보가 들어갑니다. 예: `시험_19551103/D/20261105_164616`
- **발목 혈압 단계**<br>`0`, `V`, `D`, `M`, `S` 중 하나이고 대소문자를 구분합니다. 발목 혈압으로 계산한 커프 압력 단계입니다(`record::kAnkleLevels`)
- **`<날짜_시각>`**<br>촬영 시각 `YYYYMMDD_HHMMSS`이고 파일 이름의 앞부분입니다. 같은 폴더가 이미 있으면 덮어쓰지 않고 `_2`, `_3`을 붙입니다(`20261105_164616_2`)
- **`left`, `right`**<br>화면의 왼쪽과 오른쪽이 아니라 환자의 왼발과 오른발입니다. 어느 쪽 화소 영역에 어느 발이 찍혔는지는 매니페스트의 `pixel_left_foot`가 정합니다
- **고정 이름**<br>`thermal_left.npy`, `thermal_right.npy`는 날짜_시각이 없습니다. 분석 프로그램이 이 이름으로 읽습니다
- **이전 구조**<br>`<날짜>/<이름>_<생년월일>/<기준>/<시각>` 구조로 저장한 촬영도 읽습니다(`record::SplitShotFolderPath`). 새로 쓰는 촬영은 위 구조입니다

## 파일 목록

| 파일 | 내용 | 가짜 촬영 크기 |
| --- | --- | --- |
| `_shot.json` | 매니페스트 | 1.5 KB |
| `_left.mkv`, `_right.mkv` | 발 영상(FFV1) | 3.9 MB씩 |
| `_left.json`, `_right.json` | 발 영상 사이드카 | 112 KB씩 |
| `_thermal.raw16` | 열화상 원시값 | 165.9 MB |
| `_thermal.avi` | 열화상 확인용 영상 | 165.9 MB |
| `thermal_left.npy`, `thermal_right.npy` | 열화상 좌우 발 | 82.9 MB씩 |
| `_thermal.json` | 열화상 사이드카 | 18 KB |
| `_ppg.csv`, `_ppg.json` | PPG 파형과 사이드카 | 0.58 MB, 23 KB |
| `perfusion/<판>/` | 분석 결과 | 0.4 MB |

크기는 `specs/see3cam_manual_720p60.json`(1280x720, 60 fps, 1800장)으로 macOS에서 `--fake` 촬영한 값입니다. PPG는 `--ppg-port COM6`을 준 촬영에서 쟀고, 분석 결과는 분석 API 저장소의 가짜 분석 프로그램이 쓴 값입니다.

- **쓰는 곳**<br>매니페스트는 `platform/src/shot_folder.cc`가 녹화 전에, 영상과 사이드카는 `platform/src/capture.cc`의 쓰기 스레드가, 열화상 세 파일은 `platform/src/thermal_stream.cc`가 녹화 중에, `.npy`는 녹화가 끝난 뒤 `platform/src/shot_folder.cc`(`WriteThermalArrays`)가, PPG 파일은 `platform/src/ppg_sensor.cc`가 촬영을 저장할 때 씁니다. JSON 바이트는 `core/src/record`가 만듭니다
- **읽는 곳**<br>분석 프로그램은 두 발 영상, `.npy`, 사이드카, 매니페스트, `_ppg.csv`를 읽습니다([분석 API](https://github.com/deep-medi-Research/diabetic-foot-api/blob/v1.0.0/docs/analysis-api.md) 8절). 결과 창은 매니페스트, 사이드카, 영상, `_ppg.csv`를 읽어 그립니다(`app/src/result_view.cc`)
- **분석 입력**<br>매니페스트의 `analysis_inputs`가 알려 주는 네 개입니다. 두 발 영상(`left_video`, `right_video`)과 두 열화상 배열(`thermal_left_array`, `thermal_right_array`)입니다. 분석 전에 앱이 촬영 폴더의 파일과 사이드카를 확인합니다. 영상 문제는 분석을 막고 열화상 문제(파일 없음, 장 수 부족, 누락)는 경고만 합니다(`record::CheckAnalysisInputs`)
- **원본과 사본**<br>열화상의 원본은 `.raw16`입니다. `.npy`는 `.raw16`에서 다시 만들 수 있는 사본이고, `.avi`는 사람이 보는 확인용이라 분석에 쓰지 않습니다
- **`.mkv.queue.txt`**<br>FFV1 쓰기 대기열의 기록 한 줄입니다(`platform/src/ffv1_file.cc`). 이 레포의 코드는 읽지 않고 진단용으로만 남습니다. 예: `frames=1800 limit_mb=768 max_queued_mb=0.0 producer_wait_ms=0 close_wait_ms=0 mux_failed=0`. `limit_mb`는 파일당 대기열 한도이고 환경 변수 `HWLAB_WRITE_QUEUE_MB`로 바꿉니다
- **매니페스트에 없는 파일**<br>PPG 파일, 카메라 온도 파일, `.mkv.queue.txt`는 매니페스트의 `files`에 없고 이름 규칙으로 찾습니다
- **개인정보**<br>`patient_name`과 `birth_date`는 매니페스트와 폴더 이름에만 있고 사이드카에는 넣지 않습니다. 혈압도 매니페스트에만 있습니다

## 매니페스트 `<날짜_시각>_shot.json`

**매니페스트는 촬영 정보와 파일 목록을 한 번만 쓴 JSON이고, `schema_version`은 7입니다.** 녹화를 시작하기 전에 쓰므로 `files`는 이번 촬영이 만들 파일 목록입니다. 실제로 만들어졌는지는 파일이 있는지로 확인합니다. 같은 입력은 같은 바이트를 냅니다.

| 필드 | 내용 |
| --- | --- |
| `schema_version` | 7 |
| `patient_name`, `birth_date` | 이름, 생년월일(`YYYYMMDD`) |
| `blood_pressure` | `arm`, `ankle`(각각 `systolic_mmhg`, `diastolic_mmhg`), `ankle_level` |
| `shot_time` | 촬영 시각(PC 현지 시각, 시간대 없음) |
| `attempt` | 같은 날, 같은 환자, 같은 단계의 몇 번째 촬영(모르면 `null`) |
| `app` | `version`(개발 빌드는 `null`), `commit` |
| `spec` | `file`(촬영 선언 파일 이름), `sha256` |
| `pixel_left_foot` | 화소 왼쪽에 찍힌 환자의 발(`left`, `right`) |
| `lighting` | `source`, `product`, `brightness_percent` |
| `thermal_recorded` | 열화상을 녹화했는지 |
| `foot_warmth` | 열화상을 녹화했고 판정이 끝났을 때만 있음 |
| `thermal_absent_reason`, `thermal_skip_confirmed` | 열화상을 녹화하지 않았을 때만 있음 |
| `devices` | 장치 목록(`stream`, `name`) |
| `folder` | 저장 위치 아래의 촬영 폴더 경로(구분자 `/`) |
| `files` | 역할 이름과 파일 이름 |
| `analysis_inputs` | 분석 입력 역할 이름 |

- **`blood_pressure.ankle_level`**<br>`name`(`0`, `V`, `D`, `M`, `S`)과 `pressure_mmhg`(계산한 압력, 0.5 단위일 수 있음)입니다. 계산식은 `record::AnkleLevelMmhg`에 있습니다
- **`spec.sha256`**<br>앱이 읽은 선언 파일의 바이트로 구한 SHA-256입니다. 같은 선언이라도 줄 끝(LF, CRLF)이 다르면 값이 다르므로 `.gitattributes`가 글 파일을 모든 OS에서 LF로 풀게 고정합니다. 값은 `sha256sum specs/<파일>`과 같습니다
- **`lighting.source`**<br>`unknown`, `external`(앱이 조명을 제어하지 않음), `app`(앱이 LED 보드에 밝기를 보냄) 중 하나입니다
- **`foot_warmth`**<br>`left`, `right`(두 발 열화상 대비), `min_warm`(따뜻하다고 본 아래 끝), `cold_confirmed`(차갑다는 안내에서 사용자가 진행을 고름)입니다
- **`thermal_absent_reason`**<br>`disabled`(실행할 때 열화상을 끔), `no_device`(이 OS에 열화상 구현이 없음), `not_receiving`(촬영 버튼 때 장치를 열지 못함), `preparing`(첫 프레임 대기 중), `unknown` 중 하나입니다
- **`files`의 역할 이름**<br>`full_video`, `full_sidecar`, `left_video`, `left_sidecar`, `right_video`, `right_sidecar`, `thermal_raw`, `thermal_sidecar`, `thermal_preview`, `thermal_left_array`, `thermal_right_array`, `manifest`입니다. 이번 촬영에 없는 역할은 빠집니다

## 영상 사이드카 `_left.json`, `_right.json`, `_full.json`, `_thermal.json`

**사이드카는 영상 파일마다 하나이고, 촬영 조건, 다시 읽은 설정값, 누락 기록, 프레임별 기록을 담습니다. `schema_version`은 8입니다.** 열화상 사이드카도 같은 필드를 쓰고, 값이 없는 칸은 `null`입니다.

| 필드 | 내용 |
| --- | --- |
| `schema_version` | 8 |
| `video_file` | 이 사이드카가 설명하는 파일 이름 |
| `device`, `stream` | 장치 이름, `rgb` 또는 `thermal` |
| `foot` | 발 영상이면 `left`, `right`. 전체 화면과 열화상은 `null` |
| `light` | 조명 기록(열화상은 `null`) |
| `lens_shading` | 열화상 렌즈 보정 상태(RGB는 `null`) |
| `format` | `pixel_format`, `width`, `height`, `frame_rate`, `codec` |
| `crop` | 원본 프레임 안에서 잘라 낸 영역 `x`, `y`, `width`, `height` |
| `settings` | 카메라에서 다시 읽은 설정값 |
| `clock` | 장치 시각을 공통 시각으로 옮기는 식 |
| `display_range` | 열화상 확인용 영상의 표시 범위(`low`, `high`) |
| `frames` | 장 수, 누락, 간격 요약 |
| `per_frame` | 프레임마다 한 값씩 든 배열 |

- **`format`**<br>`pixel_format`은 `UYVY`, `NV12`, `Y16`(열화상) 중 하나이고, `codec`은 RGB가 `ffv1` 또는 `uncompressed`(촬영 선언의 코덱), 열화상이 `raw16`입니다. 발 영상은 `crop`이 잘라 낸 크기가 `width`, `height`입니다
- **`light`**<br>`source`, `percent`, `port_vid_pid`, `serial`, `confirmed`(사람이 확인함), `response`(`unknown`, `applied`, `not_seen`), `flicker`입니다. `flicker`는 `frames`, `mean`, `stddev`, `peak_to_peak`, `periodic_fraction`, `dominant_period_frames`로 프레임별 영역 밝기의 깜빡임을 요약합니다
- **`lens_shading`**<br>`source`(`none`, `loaded`, `corrected`, `failed`)와 `file_unix_s`(보정 파일의 수정 시각)입니다
- **`settings`**<br>`mode`(`auto`, `manual`), `exposure_auto`, `exposure_step`, `exposure_ms`, `gain`, `white_balance_auto`, `white_balance_k`, `sdk_frame_rate`, `locked`입니다. 카메라가 알려 주지 않은 값은 `null`입니다
- **`clock`**<br>`offset_us`, `rate`, `max_residual_us`입니다. 공통 시각 = `offset_us` + `rate` x 장치 시각이고(`timing::ToCommonTime`), 공통 시각은 PC 시계(마이크로초)입니다
- **`frames`**<br>`target`(목표 장 수), `written`, `received`, `record_period_us`(열화상 기록 주기, 0이면 모두 기록), `written_in_rgb_window`, `first_time_us`, `last_time_us`, `median_interval_us`, `missing`(카메라 단 누락), `dropped`(저장 누락), `buffer_frames`, `max_queue_frames`, `file_bytes`, `gaps`(`after_frame`, `interval_us`, `missing`), `slow_intervals`, `min_fps_1s`, `slow_spans`(`start_us`, `end_us`, `intervals`)입니다
- **`per_frame`**<br>`brightness_pixels`, `clip_level`과 길이가 같은 배열 여섯 개(`index`, `device_time_us`, `host_time_us`, `common_time_us`, `brightness_sum`, `clipped_pixels`)입니다. 한 프레임의 평균 밝기는 `brightness_sum / brightness_pixels`입니다
- **사이드카 크기**<br>프레임 수에 비례합니다. 1800장 발 영상은 112 KB이고, 270장 열화상은 18 KB입니다

## 열화상 파일

- **`_thermal.raw16`**<br>머리글 없이 프레임마다 `width * height`개의 리틀 엔디언 uint16을 행 순서로 이어 붙입니다. 크기와 장 수는 파일에 없고 사이드카의 `format`, `frames.written`에 있습니다. 장 수는 파일 크기를 프레임 바이트 수로 나눈 값입니다
- **`thermal_left.npy`, `thermal_right.npy`**<br>NumPy format 1.0이고 dtype은 `<u2`, 모양은 (프레임 수, 영역 높이, 영역 폭)입니다. `.raw16`에서 왼발과 오른발 영역을 잘라 쓰고, 영역이 세로로 길지 않으면(높이가 폭 이하) 쓰지 않고 실패합니다
- **`_thermal.avi`**<br>열화상을 고정한 표시 범위(`display_range`)로 8비트 회색으로 펴서 UYVY 무압축 AVI로 쓴 확인용 영상입니다
- **`_thermal_camera_temp.csv`**<br>열화상 카메라가 카메라 온도를 주는 장비에서만 생기고, 열은 `index,device_time_us,host_time_us,fpa,shutter_raw,shutter_c`입니다. `.raw16`에 쓴 프레임마다 한 줄이고, 값이 없는 프레임은 값 칸이 빕니다. 장면의 온도가 아니라 카메라 자신의 값입니다

## PPG 파일

- **`_ppg.csv`**<br>첫 줄이 열 이름 `common_time_us,value,measure_error,heart_rate,spo2`이고 표본마다 한 줄입니다(500 Hz). `common_time_us`는 RGB 사이드카의 `common_time_us`와 같은 시계라 바로 견줍니다. `measure_error`는 0 정상, 102(`'f'`) 손가락 빠짐, 115(`'s'`) 신호 나쁨, 112(`'p'`) 센서 연결 안 됨입니다
- **`_ppg.json`**<br>`schema_version` 3입니다. 필드는 `samples_file`, `port`, `device_info`(장치가 답하지 않았으면 `null`), `sample_rate_hz`, `record`(`begin_us`, `end_us`), `clock`(시계 구간마다 `last_device_us`, `offset_us`, `rate`, `max_residual_us`), `anchor_device_us`, `anchor_host_us`, `samples`(`count`, `messages`, `first_time_us`, `last_time_us`, `median_interval_us`, `missing`, `measure_error`, `gaps`), `measure_errors`(`spans`, `rgb_window`), `link`(`crc_failures`, `bad_lengths`, `implausible`, `skipped_bytes`), `events`입니다
- **측정 데이터별 시각 쌍**<br>`anchor_device_us[i]`와 `anchor_host_us[i]`는 측정 데이터 하나의 마지막 표본 장치 시각과 PC가 그 데이터를 받은 시각이고, 배열 길이는 `samples.messages`입니다. 이 쌍을 `timing::FitClockSegments`에 수신 빈틈 50 ms(`PpgLink::kReceiveGapUs`)로 넣으면 `clock`과 같은 값이 나옵니다. 그래서 시계 맞춤 방법을 고친 뒤에도 지난 촬영의 공통 시각을 다시 계산할 수 있습니다. schema 2까지의 사이드카에는 이 두 배열이 없습니다
- **PPG는 RGB 녹화보다 깁니다**<br>촬영 버튼을 누른 때부터 저장할 때까지 기록하고 PPG 안정 대기도 들어갑니다. 같은 촬영에서 RGB 녹화 구간 안의 표본은 `samples`가 아니라 `measure_errors.rgb_window.samples`가 셉니다

## 분석 결과 `perfusion/<판>/`과 `results/`

**분석이 끝나면 분석 프로그램이 `<촬영 폴더>/perfusion/<판>/`에 `<날짜_시각>_`으로 시작하는 파일을 쓰고, 앱이 그중 요약, 보고서, 그림, GIF, 프레임 간격 CSV를 저장 위치의 `results/`로 복사합니다.** `<판>`은 분석 판의 이름입니다.

| 폴더 | 파일 | 만드는 곳 |
| --- | --- | --- |
| `perfusion/<판>/` | `_summary.json`, `_perfusion.npz`, `_waveforms.npz`, `_perfusion.gif`, `.png` | 분석 프로그램 |
| `perfusion/<판>/` | `_frame_timing.csv`, `_frame_windows.csv` | 앱 |
| `perfusion/<판>/` | `analyzer_log.txt` | 앱 |
| `results/` | 위 파일 중 npz를 뺀 사본 | 앱 |

- **분석 프로그램이 쓰는 파일**<br>형식은 [분석 API](https://github.com/deep-medi-Research/diabetic-foot-api/blob/v1.0.0/docs/analysis-api.md) 7절에 있습니다
- **`_frame_timing.csv`, `_frame_windows.csv`**<br>분석이 끝난 뒤 앱이 사이드카로 만든 프레임 간격 기록입니다(`platform/src/frame_timing.cc`). 열은 [00-architecture.md](00-architecture.md) 표 2의 프레임 간격 파일에 있습니다
- **`analyzer_log.txt`**<br>앱이 보낸 요청 줄과 그 요청이 끝나는 동안 분석 프로그램이 낸 출력 줄을 덧붙이는 기록입니다(`platform/src/analyzer.cc`)
- **`results/` 이름**<br>`<이름>_<생년월일>_<단계>_<날짜_시각>_<판>_<뒷부분>`입니다. 예: `시험_19551103_D_20261105_164616_fake_summary.json`. 같은 촬영을 다시 분석해 내용이 달라지면 덮어쓰지 않고 `_reanalysis2`, `_reanalysis3`을 더합니다(최대 99). 이름이 길어지므로 앱은 촬영 전에 Windows 경로 길이 259자를 검사합니다
- **이전 분석**<br>분석 판을 나누기 전의 결과는 `<촬영 폴더>/perfusion/`에 바로 있고, 결과 창은 "이전 분석" 탭으로 엽니다

## 크기

**가짜 촬영 한 번은 488 MiB(`du` 499,624 KiB)이고, 열화상 프레임 한 장이 세 파일에 저장되기 때문입니다.** 열화상은 640x480 화소에 화소당 2바이트라 한 장이 614,400 바이트이고, 선언의 `thermal_record_period_us`(111111, 9 Hz)로 30초에 270장을 기록합니다.

| 항목 | 계산 | 바이트 |
| --- | --- | --- |
| `.raw16` | 270장 x 614,400 | 165,888,000 |
| `.avi` | 270장 x 614,400 + 머리글 | 165,897,252 |
| `.npy` 둘 | 270장 x 307,200(폭 320) x 2 + 머리글 | 165,888,256 |

- **열화상이 대부분입니다**<br>위 세 항목이 497.7 MB로, 폴더 전체(약 506 MB, 10^6 기준)의 98%입니다
- **RGB 영상은 작게 나옵니다**<br>가짜 카메라의 영상이 단순해 FFV1이 잘 압축한 값(1800장에 3.9 MB)입니다. 실제 카메라의 영상은 훨씬 큽니다. 1920x1200, 900장 녹화에서 발 영상이 240 MB와 258 MB였다는 실측이 [00-architecture.md](00-architecture.md) 표 10에 있습니다. 현재 기본 선언(1280x720, 60 fps)의 실제 크기는 측정하지 않았습니다
- **촬영 전 디스크 검사는 무압축으로 셉니다**<br>FFV1은 내용에 따라 압축률이 달라, 앱은 RGB를 무압축 UYVY로 센 상한을 디스크 여유와 견줍니다(`record::EstimateShotBytes`). 그래서 여유가 모자라다는 안내가 실제 사용량보다 크게 잡힌 값일 수 있습니다
- **가짜 촬영을 반복하는 개발**<br>`--no-thermal`을 주면 열화상 파일(`.raw16`, `.avi`, `.npy` 둘, 사이드카)을 만들지 않아 RGB만 남습니다. 촬영이 쌓이면 디스크를 빨리 쓰므로 시험이 끝난 촬영 폴더를 지웁니다
