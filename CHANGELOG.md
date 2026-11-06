# 변경 기록

촬영 앱의 버전별 변경이고 최신 버전이 위입니다.
형식은 [Keep a Changelog 1.1.0](https://keepachangelog.com/ko/1.1.0/)을 따릅니다. 앱의 버전은 앱을 담은 설치 프로그램의 릴리즈 버전이고, 이 레포의 태그 `vX.Y.Z`가 그 설치 프로그램에 들어간 커밋을 가리킵니다.

쓰는 규칙은 아래와 같습니다.

- 앱 동작이 바뀌는 변경은 `## [Unreleased]`에 모읍니다. 설치 프로그램 릴리즈가 이 레포에 태그 `vX.Y.Z`를 달면, 그 태그 커밋까지의 항목을 `## [X.Y.Z] - <YYYY-MM-DD>` 절로 옮깁니다.
- 분류는 `### 추가`, `### 변경`, `### 폐기 예정`, `### 제거`, `### 수정`, `### 보안` 순서이고 항목이 없는 분류는 쓰지 않습니다.
- 항목은 한 줄 목록이고, 호환이 깨지는 항목은 `(호환 깨짐)`으로 시작해 분류의 맨 위에 둡니다.
- 문서, 그림, CI만 바꾼 변경은 적지 않습니다.

## [Unreleased]

### 추가

- PPG 사이드카(`_ppg.json`, `schema_version` 3)에 측정 데이터마다 마지막 표본의 장치 시각과 PC 수신 시각을 배열 두 개(`anchor_device_us`, `anchor_host_us`)로 남깁니다. 시계 맞춤 방법을 고친 뒤에도 지난 촬영의 공통 시각을 다시 계산할 수 있습니다.

## [0.1.19] - 2026-11-06

### 변경

- (호환 깨짐) 앱은 분석 API 1.0.0([diabetic-foot-api](https://github.com/deep-medi-Research/diabetic-foot-api) 태그 `v1.0.0`)을 따르고, 따르는 판을 레포 루트의 `ANALYSIS_API_VERSION`에 적습니다. 분석 프로그램의 준비 줄에 `api_version`이 없거나 주 버전이 다르면 그 분석 프로그램을 끝내고 이유와 함께 실패 상태로 둡니다.
- 결과 창의 분석 정보 줄에 준비 줄과 요약 `dfr`의 `api_version`, `worker_version`을 보입니다.
- 판 고르기 줄에 `VARIANT.json`의 `upstream` 커밋과 날짜만 보이고, 분석 API가 정하지 않는 `release`는 읽지 않습니다.
- 분석 프로그램 진입 스크립트의 파일 이름을 빌드 옵션 `HWLAB_ANALYZER_ENTRY`로 받습니다. 기본값은 지금 배포 패키지의 파일 이름입니다.

### 제거

- 레포 안의 가짜 분석 프로그램(`tools/fake_analysis`)을 지웠습니다. 시험과 개발 실행은 구성할 때 받는 분석 API 저장소의 `fake/fake_analyzer.py`를 씁니다.
