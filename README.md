# diabetic-foot-front

**당뇨발 관류 촬영 장비의 촬영 앱 코드(C++20, Qt Quick)입니다. 관류 분석 프로그램은 이 레포 밖의 외부 프로그램이고, 앱은 분석 프로그램 없이도 빌드하고 촬영합니다.** 앱은 RGB 카메라, 열화상 카메라, PPG 센서로 발바닥을 30초 동안 함께 녹화하고, 저장한 촬영을 분석 프로그램에 넘겨 결과를 보여 줍니다.

![촬영 한 번의 흐름 애니메이션](docs/images/flow.gif)

촬영 한 번(약 33초)을 줄여서 보여 줍니다. 그림의 분석 시간은 그림을 그린 때의 값입니다.

![촬영 한 번의 흐름](docs/images/flow.png)

여섯 단계와 각 단계의 판정, 예외, 스레드, 층 구조입니다.

- **설계**<br>[`docs/00-architecture.md`](docs/00-architecture.md)
- **분석 프로그램과 주고받는 형식**<br>[`docs/analysis-contract.md`](docs/analysis-contract.md)
- **그림 원본**<br>`tools/draw_flow.py`, `tools/draw_flow_animation.py`

## 구성

| 폴더 | 내용 |
| --- | --- |
| `core/` | 계산(OS, 장비 무관) |
| `platform/` | 장치, 파일, 관류 분석 프로그램 실행 |
| `app/` | 화면 |
| `specs/` | 촬영 선언 |
| `vendor/` | 제조사 SDK, 샘플, 문서 |
| `docs/` | 설계 문서, 분석 연동 약속 |
| `tools/` | 그림 원본, 결과 창 확인 도구, 가짜 분석 프로그램 |

## 주요 기능

| 기능 | 요점 |
| --- | --- |
| 동시 녹화 | RGB 900장, 열화상 9Hz, PPG 500Hz |
| 좌우 발 분할 | 발마다 따로 저장 |
| 좌우반전 표시 | 화면에서 왼발이 왼쪽 |
| 안정화 | 잠금 뒤 75프레임, fps 검사 |
| 열화상 셔터 교정 | 녹화 직전 한 번 |
| 환자 정보 | 최근 환자, 상완과 발목 혈압, 발목 혈압 단계(촬영 폴더의 가운데 단계) |
| 결과 창 | 분석이 끝나면 요약 값, RGB와 열화상, 관류 지도, 30초 그래프 |

저장 위치는 `문서\DiabeticFoot`입니다. 판정은 참고값이며 진단이 아닙니다.

## 관류 분석 프로그램

**앱은 분석 프로그램 폴더를 빌드할 때 `HWLAB_ANALYZER_DIR`로 받고, 비워 두면 분석 없이 빌드합니다.** 분석 프로그램이 없으면 앱은 분석 칸에 "관류 분석 프로그램 없음"을 보이고, 촬영과 저장은 그대로 합니다.

- **`HWLAB_ANALYZER_DIR`**<br>`dfr_worker.py`가 있는 폴더입니다. `cmake --install`이 `bin/analysis`로 설치합니다. 값을 줬는데 `dfr_worker.py`가 없으면 구성이 멈춥니다
- **`HWLAB_PYTHON_DIR`**<br>분석 프로그램이 쓰는 파이썬 폴더입니다. `bin/python`으로 설치합니다
- **주고받는 방식**<br>앱이 분석 프로그램을 띄우고, 표준 입력의 JSON 한 줄과 표준 출력의 줄, 결과 파일로만 주고받습니다. 형식은 [`docs/analysis-contract.md`](docs/analysis-contract.md)에 있습니다
- **개발할 때**<br>설치하지 않은 분석 프로그램은 앱 인자 `--analyzer-python <python 실행 파일> --analyzer-script <dfr_worker.py>`로 가리킵니다
- **분석 프로그램 없이 개발할 때**<br>`tools/fake_analysis`의 가짜 분석 프로그램이 같은 인자로 뜨고, 모든 촬영에 같은 고정 결과를 냅니다. 실행 방법은 아래 [실행](#실행)에 있습니다

## 빌드

시험 프레임워크(googletest)는 CMake가 구성할 때 내려받으므로 네트워크가 필요합니다.

```bash
# core: macOS, Windows
cmake -S core -B build/core && cmake --build build/core && ctest --test-dir build/core

# platform: 파이프라인 시험은 macOS에서도 돕니다. Windows 카메라와 실기 도구는 Windows에서만 빌드합니다
cmake -S platform -B build/platform && cmake --build build/platform && ctest --test-dir build/platform

# app: Qt 6.5 이상. Windows는 FFmpeg LGPL 공유 라이브러리와 제조사 SDK DLL 폴더를 줍니다
cmake -S app -B build/app -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt> \
      -DHWLAB_FFMPEG_DIR=<FFmpeg> -DHWLAB_VENDOR_DLL_DIR=<eCAMFwSw.dll 폴더>
cmake --build build/app
ctest --test-dir build/app
cmake --install build/app --prefix <배포 폴더(절대 경로)>   # Qt 경로 없이 실행되는 폴더
```

- **Windows**<br>Visual Studio 2022 개발자 명령 프롬프트(`VsDevCmd.bat -arch=x64`)에서 실행합니다. 장비 연결과 열화상은 Windows에서만 열립니다
- **macOS**<br>core, platform, app이 모두 빌드되고 시험이 돕니다. Homebrew의 `cmake`, `ffmpeg`, `qt`를 쓰고 app은 `-DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt`를 줍니다. FFmpeg가 있으면 FFV1 시험까지 돕고, Homebrew의 FFmpeg는 GPL 빌드라 시험에만 씁니다. 카메라는 열지 않고 `--fake`로 실행합니다
- **제조사 SDK**<br>`vendor/`에 있습니다. eCAMFwSw.dll 폴더는 `vendor/see3cam_24cug_sdk/Win32/Binary/64Bit/HIDLibraries`, 열화상 SDK 폴더(`-DHWLAB_THERMAL_SDK_DIR`)는 `vendor/thermal_expert_sdk/c++`입니다
- **app 시험 끄기**<br>배포 빌드에서는 `-DHWLAB_APP_TESTS=OFF`로 끌 수 있습니다

## 실행

```
<배포 폴더>\bin\hwlab_app.exe --spec <배포 폴더>\specs\see3cam_manual_720p60.json --out <저장 폴더>
```

- **`--out`**<br>처음 저장 위치입니다. 화면에서 고른 저장 위치를 기억하고 있으면 그 위치를 씁니다
- **`--fake`**<br>장비 없이 가짜 RGB 카메라, 가짜 열화상 카메라, 가짜 PPG 장치로 실행합니다. 분석 프로그램이 없어도 뜹니다. 화면에서 바꾼 값을 다음 실행에 남기지 않습니다
- **`--result <촬영 폴더>`**<br>그 촬영의 결과 창만 엽니다
- **`--no-prestream`**<br>녹화 중 미리 분석을 쓰지 않습니다

그 밖의 인자는 `app/src/main.cc` 맨 위에 있습니다.

### 분석 프로그램 없이 결과 창까지 보기

**`--fake`에 `tools/fake_analysis`의 가짜 분석 프로그램을 더하면 장비도 실제 분석 프로그램도 없이 촬영, 분석 상태 막대, 결과 창을 끝까지 볼 수 있습니다.** 파이썬 3.9 이상만 있으면 되고, 가짜 분석 프로그램은 모든 촬영에 같은 고정 결과를 냅니다. 스크립트의 상대 경로는 앱을 띄운 폴더 기준이고, `python3` 같은 이름만 준 파이썬은 PATH에서 찾습니다.

macOS(레포 폴더에서)

```bash
build/app/hwlab_app --spec specs/see3cam_manual_720p60.json --out /tmp/shots --fake \
  --analyzer-python python3 \
  --analyzer-script tools/fake_analysis/dfr_worker.py
```

Windows(명령 프롬프트, 레포 폴더에서)

```bat
<배포 폴더>\bin\hwlab_app.exe --spec <배포 폴더>\specs\see3cam_manual_720p60.json --out C:\temp\shots --fake ^
  --analyzer-python python ^
  --analyzer-script tools\fake_analysis\dfr_worker.py
```

- **파이썬을 찾지 못하면**<br>분석 칸에 "파이썬 없음: python (PATH에서 찾지 못했습니다)"가 보입니다. `where python`으로 위치를 확인해 `C:\Python311\python.exe`처럼 전체 경로를 줍니다
- **설치 폴더에 넣기**<br>구성할 때 `-DHWLAB_ANALYZER_DIR=<레포 전체 경로>/tools/fake_analysis`를 주면 `cmake --install`이 `bin/analysis`로 설치합니다. 앱은 `bin/python`의 파이썬을 쓰므로 Windows는 `HWLAB_PYTHON_DIR`도 줍니다
- **인자, 고정 결과 값, 실패 흉내**<br>[`tools/fake_analysis/README.md`](tools/fake_analysis/README.md)
