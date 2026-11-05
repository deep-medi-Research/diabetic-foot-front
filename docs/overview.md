# 코드 길잡이

**코드 안의 이름은 모두 `hwlab`이고, 아래 표에서 찾는 기능의 파일로 바로 들어가면 됩니다.** 이 문서는 처음 코드를 여는 사람이 이름의 뜻, 기능별 파일 위치, 이 레포에 없는 것을 한 곳에서 찾도록 쓴 입구입니다. 층 구조와 판정 기준은 [00-architecture.md](00-architecture.md), 촬영이 디스크에 남기는 파일은 [shot-folder.md](shot-folder.md)에 있습니다.

## 이름 `hwlab`

**`hwlab`은 이 앱의 내부 이름입니다.** 촬영 장비를 개발할 때 붙인 프로젝트 이름(HardwareLab의 줄임말)이 코드에 들어갔고, 레포 이름 diabetic-foot-front는 코드에 쓰지 않습니다. 제품 쪽 이름은 `DiabeticFoot`이고 저장 폴더(`문서\DiabeticFoot`)에만 나옵니다.

- **코드**<br>네임스페이스 `hwlab::`(층마다 `hwlab::timing`, `hwlab::platform` 등), 빌드 대상 `hwlab_*`
- **실행 파일과 화면**<br>`hwlab_app`, QML 모듈 `HwLab`
- **빌드 옵션과 환경 변수**<br>`HWLAB_ANALYZER_DIR`, `HWLAB_FFMPEG_DIR`처럼 `HWLAB_`로 시작합니다
- **설치된 PC에 남는 이름**<br>기억한 값의 레지스트리 키 `HKCU\Software\deepmedi\hwlab`, 앱 데이터 폴더 `deepmedi\hwlab`, 단일 실행 이름 `HWLabAppRunning`

이름은 바꾸지 않습니다. 레지스트리 키와 앱 데이터 폴더가 이 이름이라 바꾸면 이미 설치된 PC의 기억한 값과 기록이 끊깁니다.

## 어디에 있나

| 찾는 것 | 먼저 볼 파일 |
| --- | --- |
| 진입점 | `app/src/main.cc` |
| 촬영 선언(specs) | `specs/*.json`, `core/src/flow/capture_spec.cc` |
| 촬영 흐름 판정 | `core/src/flow/flow.cc` |
| 촬영 흐름 실행 | `platform/src/capture.cc` |
| 촬영 화면 상태 | `app/src/capture_controller.cc` |
| RGB 카메라 | `platform/include/hwlab/platform/rgb_camera.h` |
| 열화상 | `platform/include/hwlab/platform/thermal_camera.h` |
| LED | `platform/include/hwlab/platform/light.h` |
| PPG | `platform/include/hwlab/platform/ppg_sensor.h` |
| 파일 쓰기 | `platform/src/shot_folder.cc` |
| 분석 경계 | `platform/src/analyzer.cc` |
| 결과 창 | `app/qml/ResultWindow.qml` |

표 아래에서 항목마다 파일을 더 짚습니다. 경로는 모두 레포 루트 기준입니다.

- **진입점**<br>`app/src/main.cc`의 `main()`이 앱 인자를 읽고(맨 위 주석에 전체 목록), 촬영 선언을 읽고, 장치와 컨트롤러를 만들어 `app/qml/Main.qml`을 띄웁니다. 장비 없이 실행하는 `--fake`도 여기서 가짜 장치를 끼웁니다. 장치별 실기 확인 도구(`hwlab_capture`, `hwlab_ppg`, `hwlab_thermal`, `hwlab_serial_ports`)는 `platform/tools/`에 있습니다
- **촬영 흐름**<br>다섯 단계(선언 읽기, 미리보기, 안정화, 녹화, 마무리)는 [00-architecture.md](00-architecture.md)의 그림 1과 표 4에 있습니다. 상태를 옮기는 판단은 `core/src/flow/flow.cc`(`flow::Advance`), 그 판단을 스레드 셋(수신, 제어, 쓰기)으로 실행하는 곳이 `platform/src/capture.cc`, 화면과 이어 주는 곳이 `app/src/capture_controller.cc`입니다. 설정값을 바꾸는 순서는 `core/src/flow/settings_plan.cc`와 `platform/src/reconcile.cc`입니다
- **카메라**<br>RGB는 `rgb_camera.h`가 인터페이스이고, Windows 구현은 `platform/src/windows/rgb_camera_windows.cc`, 장비 없는 시험용은 `platform/src/fake_camera.cc`입니다. 프레임 시각은 `core/src/timing`에서 판정합니다
- **열화상**<br>인터페이스는 `thermal_camera.h`, Windows 구현은 `platform/src/windows/thermal_camera_windows.cc`(제조사 SDK 헤더는 이 파일만 include합니다), 수신과 쓰기는 `platform/src/thermal_stream.cc`, 장비 없는 시험용은 `fake_thermal_camera.cc`입니다. 화면 쪽은 `app/src/thermal_view.cc`와 `app/qml/ThermalPane.qml`입니다
- **LED**<br>`platform/src/light.cc`가 직렬 포트로 밝기 명령을 보내고, 화면 쪽은 `app/src/light_controller.cc`와 `app/qml/LightPanel.qml`입니다. 직렬 포트 자체는 `serial_port.cc`와 `windows/serial_port_windows.cc`입니다
- **PPG**<br>장치 메시지 해석은 `platform/src/ppg_wire.cc`, 장치 스레드와 연결 상태와 파일 쓰기는 `platform/src/ppg_sensor.cc`, 화면 쪽은 `app/src/ppg_controller.cc`, `app/qml/PpgPanel.qml`입니다
- **파일 쓰기**<br>JSON과 파일 이름의 바이트는 core의 `core/src/record`가 만들고(`shot.cc`, `sidecar.cc`), 디스크에 쓰는 일은 platform이 합니다. 폴더와 매니페스트는 `shot_folder.cc`, RGB 영상은 `video_writer.cc`(코덱 고르기), `ffv1_file.cc`, `avi_file.cc`, 열화상은 `thermal_writer.cc`, PPG는 `ppg_sensor.cc`입니다. 파일 목록과 필드는 [shot-folder.md](shot-folder.md)에 있습니다
- **분석 경계**<br>앱이 분석 프로그램을 띄우는 곳은 `platform/src/analyzer.cc`와 `child_process_posix.cc`, `windows/child_process_windows.cc`, 요청 줄과 출력 줄 해석은 `perfusion_analysis.cc`, 결과 읽기는 `perfusion_summary.cc`와 `perfusion_detail.cc`, 대기열과 판 관리는 `app/src/analysis_controller.cc`, `analysis_variants.cc`입니다. 주고받는 형식은 [analysis-contract.md](analysis-contract.md)입니다. C++ 코드는 분석 프로그램의 파일을 include하지 않고 읽지도 않습니다
- **결과 창**<br>화면은 `app/qml/ResultWindow.qml`, 값 준비는 `app/src/result_view.cc`, 분석 막대는 `app/qml/AnalysisBar.qml`입니다. 분석이 없어도 `--result <촬영 폴더>`로 이 창만 열 수 있습니다

## 시험은 층마다 따로 있습니다

| 층 | 위치 | 프레임워크 | 장비 |
| --- | --- | --- | --- |
| core | `core/test` | GoogleTest | 필요 없음 |
| platform | `platform/test` | GoogleTest | 가짜 장치 |
| app | `app/test` | QtTest | 가짜 장치 |
| 결과 창 그림 | `tools/check_result_gif.py` | 파이썬 | 필요 없음 |

- **core**<br>시간, 화소, 파일 형식 판정이 입력과 기대 출력으로 맞는지 봅니다. OS와 장치를 쓰지 않으므로 가장 빠릅니다
- **platform**<br>가짜 카메라, 가짜 열화상, 가짜 직렬 포트(`platform/include/hwlab/platform/fake_*.h`)로 촬영 한 번을 끝까지 돌립니다. 분석 프로그램 쪽은 `platform/test/fake_analyzer.cc`가 약속 문서의 줄 형식으로 답합니다. 실제 장비가 필요한 확인은 `platform/tools/`의 실기 확인 도구로 합니다
- **app**<br>화면 상태(ViewModel)의 시험이라 실행 파일 하나가 ctest 시험 하나입니다. 가장 긴 `test_capture_controller`가 40초쯤 걸립니다
- **빌드와 실행 방법**<br>레포 루트 README의 빌드 절을 따릅니다

## 이 레포에 없는 것

**관류 분석 프로그램, 설치 프로그램과 릴리즈 스크립트, 이전 레포의 이슈는 이 레포에 없습니다.** 문서와 주석에 이름이 나오면 아래 뜻입니다.

- **관류 분석 프로그램**<br>녹화 파일로 관류를 계산하는 외부 파이썬 프로그램이고 이 레포에 없습니다. 앱은 `HWLAB_ANALYZER_DIR`로 폴더를 받고, 없으면 분석 없이 빌드합니다. 개발할 때는 `tools/fake_analysis`의 가짜 분석 프로그램을 씁니다
- **설치 프로그램, 릴리즈 스크립트, 배포 패키지 만들기**<br>이 레포의 끝은 `cmake --install`이 만드는 배포 폴더입니다. 설치 파일을 만들고 릴리즈하는 스크립트는 이 레포 밖에 있고, 코드 주석이 "이 레포 밖의 설치 프로그램"이라고 부르는 것이 이것입니다
- **이전 레포 이슈 `#번호`**<br>이 레포가 나뉘기 전의 개발 레포에서 쓰던 이슈 번호입니다. 이 레포의 이슈나 PR 번호가 아니므로 열 수 없고, 해당 문장에 이유와 결과를 풀어 적어 두었습니다
- **기존 앱**<br>이 앱 이전에 쓰던 촬영 프로그램입니다. 코드가 이 레포에 없고, [00-architecture.md](00-architecture.md)의 표 11(달라진 동작)과 표 12(기능의 층 배치)에서 견줍니다
- **실험 노트북**<br>개발 중 실측에 쓴 Windows 노트북입니다. 문서와 주석의 실측 수치 앞에 날짜와 함께 붙습니다
- **제조사 사양서**<br>`vendor/`에 SDK, 샘플, 문서가 있고, 그 밖의 사양서(예: PPG 장치의 MP-SDK 문서)는 "이 레포에는 없음"이라고 표시했습니다
