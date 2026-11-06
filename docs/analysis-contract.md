# 관류 분석 API 연동

<mark>**앱과 관류 분석 프로그램이 주고받는 형식은 분석 API 저장소가 정하고, 이 레포는 레포 루트의 `ANALYSIS_API_VERSION`에 적힌 판 하나를 따릅니다.**</mark> 이 문서는 형식을 되풀이하지 않고, 앱 쪽에서 정한 것과 그것을 구현한 코드만 적습니다.

- **형식의 원본**<br>[diabetic-foot-api](https://github.com/deep-medi-Research/diabetic-foot-api) 저장소의 [v1.0.0](https://github.com/deep-medi-Research/diabetic-foot-api/tree/v1.0.0) 태그입니다. 명령줄, 줄 형식, 요청 키, 결과 파일, 오류, 시간 제한은 그 저장소의 [`docs/analysis-api.md`](https://github.com/deep-medi-Research/diabetic-foot-api/blob/v1.0.0/docs/analysis-api.md)와 `schema/`에 있습니다
- **앱이 따르는 판**<br>`ANALYSIS_API_VERSION`의 한 줄(`1.0.0`)입니다. 배포 도구는 이 파일로 분석 프로그램 패키지가 맞는 판인지 확인합니다. 판을 올리려면 이 파일만 고칩니다
- **이 문서에서 "앱"은 촬영 앱이고 "분석 프로그램"은 관류 분석 프로그램입니다**

## 앱의 구현

| 하는 일 | 코드 |
| --- | --- |
| 분석 프로그램 띄우기, 판 폴더 찾기, 대기열, 시간 제한, 다시 띄우기, API 판 확인 | `platform/src/analyzer.cc`, `child_process_posix.cc`, `windows/child_process_windows.cc` |
| 요청 줄 만들기, 출력 줄 해석 | `platform/src/perfusion_analysis.cc` |
| 요약 읽기, 저장 위치로 복사 | `platform/src/perfusion_summary.cc`, `json_reader.h` |
| 결과 배열(npz) 읽기와 지도 그리기 | `platform/src/npy_archive.cc`, `perfusion_detail.cc`, `template_map.cc` |
| 판마다 하나씩 띄우기, 켜고 끄기, 분석 막대 | `app/src/analysis_controller.cc`, `analysis_variants.cc` |
| 결과 창 | `app/src/result_view.cc`, `app/qml/ResultWindow.qml` |

## 설치 구성

분석 API는 진입 스크립트의 이름을 정하지 않으므로, 앱은 분석 프로그램 폴더와 진입 스크립트 이름을 설정으로 받습니다.

- **`HWLAB_ANALYZER_DIR`, `HWLAB_ANALYZER_ENTRY`**<br>분석 프로그램 폴더와 그 안의 진입 스크립트 파일 이름을 구성할 때 줍니다. 이름의 기본값은 지금 배포 패키지가 쓰는 파일 이름(`app/CMakeLists.txt`)이라 패키지가 넘기지 않아도 됩니다. `cmake --install`이 폴더를 `bin/analysis`로 설치하고, 앱은 `bin/analysis/<진입 스크립트>`를 띄웁니다. 폴더에 그 파일이 없으면 구성이 멈춥니다. 폴더를 비워 두면 분석 없이 빌드합니다
- **`HWLAB_PYTHON_DIR`**<br>분석 프로그램을 실행할 파이썬 폴더입니다. `bin/python`으로 설치하고, 시스템 PATH의 파이썬은 쓰지 않습니다
- **개발할 때**<br>앱 인자 `--analyzer-python`, `--analyzer-script`로 설치하지 않은 분석 프로그램을 가리킵니다. 상대 경로는 앱을 띄운 폴더 기준입니다
- **띄우기 전 확인**<br>파이썬이나 스크립트가 없거나 크기가 0이면 앱은 분석 프로그램을 띄우지 않고 분석 칸에 "관류 분석 프로그램 없음"을 보입니다. 촬영과 저장은 그대로 합니다
- **API 판 확인**<br>준비 줄의 `api_version`이 없거나 주 버전이 `ANALYSIS_API_VERSION`과 다르면 앱은 그 프로세스를 끝내고 분석 칸에 이유를 보입니다. 결과 창은 요약 `dfr`의 `variant`, `upstream_commit`, `worker_version`, `api_version`을 분석 정보 줄에 보입니다

## 앱이 정한 동작

분석 API가 클라이언트 앱이 정하도록 둔 값과 동작입니다.

- **대기열은 앱에 있습니다**<br>분석 요청 한 건의 완료 줄을 받은 뒤에 다음 요청을 보냅니다. 판이 여럿이면 앞 판의 분석이 끝날 때까지 뒤 판의 요청도 보내지 않습니다
- **안정화와 녹화 중에는 preload와 분석 요청을 보내지 않습니다**<br>presegment 요청만 보냅니다. 분석 요청은 RGB 영상을 저장하고 분석 입력 확인을 통과한 뒤에 보냅니다
- **다시 띄우기**<br>분석 중에 프로세스가 끝나거나 진행 줄이 180초 동안 없으면 그 요청을 실패로 두고 다시 띄웁니다. 사용자가 분석을 취소해도 강제 종료하고 다시 띄운 뒤 preload를 보냅니다. 다시 띄워도 대기열의 요청은 남습니다. 값 180초는 `AnalyzerTiming::stage_timeout_ms`입니다
- **종료**<br>앱을 닫으면 표준 입력을 닫고 3초 기다린 뒤 강제 종료합니다. 분석 중이면 바로 강제 종료합니다
- **figures 줄 기다리기**<br>완료 줄 뒤 30초 안에 figures 줄이 없으면 기다리지 않고 결과를 복사합니다
- **기록 파일**<br>요청을 보낸 때부터 끝날 때까지 받은 줄을 모두 `<output>/analyzer_log.txt`에 덧붙입니다. 보낸 요청 줄은 `> `, 앱이 정한 실패 이유는 `! `를 앞에 붙입니다
- **보내는 요청 값**<br>`output`은 `<촬영 폴더>/perfusion/<판>`, `name`은 촬영 폴더 이름, `cuff_side`는 `none`, `template`은 `v106w`, `classify`와 `gif`와 `defer_figures`는 `true`, `pad_classifier`는 `false`입니다. 영상 경로와 장 수는 촬영 폴더의 매니페스트와 사이드카에서 읽습니다
- **저장 위치로 복사**<br>요약, 보고서 텍스트, PNG, GIF, 프레임 간격 CSV를 `results` 폴더로 복사하고, 파일 이름의 `<name>` 부분을 촬영 폴더에서 만든 앞부분과 `_<판>`으로 바꿉니다. npz는 복사하지 않습니다
- **경로 길이 상한**<br>Windows 경로 상한을 셀 때 판 이름을 3자, 결과 파일 이름의 뒷부분을 23자까지로 봅니다. 분석 API가 두 길이를 정하지 않아 앱이 가정한 값입니다(`core/src/record/shot_limits.cc`)

## 옛 결과 읽기

분석 API 이전의 분석 프로그램이 남긴 결과도 결과 창에서 엽니다. 요약에 있는 키로 형식을 가리고(분석 API 10.1절), 읽는 키는 `platform/include/hwlab/platform/perfusion_summary.h`에 있습니다. 판 폴더가 없는 `<촬영 폴더>/perfusion/` 바로 아래의 결과는 "이전 분석" 탭으로 엽니다.

## 시험

- **`platform/test/test_fake_analysis.cc`**<br>분석 API 저장소의 가짜 분석 프로그램(`fake/fake_analyzer.py`)을 앱과 같은 명령줄로 띄웁니다. 가짜 분석 프로그램은 받은 줄마다 요청 스키마로 확인하고 어긴 줄에는 종료 코드 4로 끝나므로, 이 시험이 통과하면 앱이 보내는 요청 줄이 API를 지킵니다. 저장소는 구성할 때 `ANALYSIS_API_VERSION`의 태그(`v1.0.0`)로 받습니다. 파이썬 3.9 이상이 없으면 시험을 건너뜁니다
- **`platform/test/fake_analyzer.cc`와 `test_analyzer.cc`, `test_child_process.cc`**<br>종료 코드, 시간 제한, 취소, 출력량, 우선순위 같은 프로세스 관리를 시험하는 C++ 도구입니다. 분석 API의 줄 모양으로 답하지만 요청 줄을 확인하지 않습니다
- **`test_perfusion_analysis.cc`, `test_perfusion_summary.cc`, `test_perfusion_detail.cc`, `test_npy_archive.cc`**<br>줄 해석과 결과 파일 읽기를 시험합니다. 요약과 npz는 `platform/test/data`의 고정 파일입니다
