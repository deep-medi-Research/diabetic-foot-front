# 가짜 분석 프로그램

<mark>**`--fake` 촬영과 함께 쓰면 장비도 실제 분석 프로그램도 없이 촬영, 분석 상태 막대, 결과 창까지 앱을 끝까지 실행할 수 있습니다.**</mark> `dfr_worker.py`는 [분석 연동 약속](../../docs/analysis-contract.md)과 같은 줄 형식으로 답하고, 모든 촬영에 같은 고정 결과 파일을 씁니다. 결과 값은 식으로 만든 숫자라 실제 분석과 관계가 없습니다. 모델 파일은 쓰지 않고, 파이썬 3.9 이상의 표준 라이브러리만 있으면 됩니다.

## 앱에서 실행

앱 인자 `--analyzer-python`에 파이썬(`python3` 같은 이름이나 경로), `--analyzer-script`에 `dfr_worker.py` 경로를 줍니다. 상대 경로는 앱을 띄운 폴더 기준이고, 이름만 준 파이썬은 PATH에서 찾습니다.

Windows(PowerShell, 레포 폴더에서). 빌드는 [README의 빌드 절](../../README.md#명령줄에서-빌드하고-실행)을 봅니다.

```powershell
.\build\windows-debug\app\hwlab_app.exe --spec specs\see3cam_manual_720p60.json --out build\shots --fake --analyzer-python python --analyzer-script tools\fake_analysis\dfr_worker.py
```

macOS(레포 폴더에서)

```bash
build/macos-debug/app/hwlab_app --spec specs/see3cam_manual_720p60.json --out build/shots --fake \
  --analyzer-python python3 \
  --analyzer-script tools/fake_analysis/dfr_worker.py
```

Visual Studio에서는 시작 항목 **앱: 가짜 장치와 가짜 분석**이 같은 인자로 띄웁니다.

이름, 생년월일, 상완 혈압, 발목 혈압, 발목 혈압 단계를 채우고 촬영을 누릅니다. 녹화가 끝나면 결과 창이 열리고, 분석 막대는 1~2초 안에 "분석 완료"가 됩니다. 판 이름은 `fake`이고 결과는 `<촬영 폴더>/perfusion/fake/`에 생깁니다.

- **파이썬을 찾지 못하면**<br>분석 칸에 "파이썬 없음: python (PATH에서 찾지 못했습니다)"가 보입니다. PowerShell에서 `where.exe python`으로 위치를 확인해 전체 경로를 줍니다
- **설치 폴더에 넣기**<br>구성할 때 `-DHWLAB_ANALYZER_DIR=<레포 전체 경로>/tools/fake_analysis`를 주면 `cmake --install`이 `bin/analysis`로 설치합니다. 앱은 `bin/python`의 파이썬을 쓰므로 Windows는 `HWLAB_PYTHON_DIR`도 줍니다
- **분석 시간 늘리기**<br>환경 변수 `FAKE_ANALYSIS_STAGE_MS`(기본 200)가 진행 단계 사이 대기 밀리초입니다. 앱이 분석 프로그램에 인자를 더 줄 수 없어 환경 변수로 받습니다

## 화면을 직접 채우지 않고 확인

환자 정보 인자와 `--press-start`로 촬영을 자동으로 시작하고, `--screenshot`으로 화면과 결과 창을 저장합니다. 녹화는 `--after` 시각의 3분의 1에 시작하고 촬영과 분석에 약 40초가 걸리므로 `--after`는 100000 이상을 줍니다.

```bash
build/macos-debug/app/hwlab_app --spec specs/see3cam_manual_720p60.json --out /tmp/shots --fake \
  --patient 테스트 --birth 1955-11-03 --arm-pressure 120/80 --ankle-pressure 120/80 --ankle-level 0 \
  --analyzer-python python3 --analyzer-script tools/fake_analysis/dfr_worker.py \
  --press-start --after 100000 --screenshot /tmp/main.png
```

`/tmp/main.png`는 촬영 화면, `/tmp/main_result.png`는 결과 창입니다. 이미 만든 촬영 폴더의 결과 창만 저장하려면 다음을 씁니다.

```bash
build/macos-debug/app/hwlab_app --result <촬영 폴더> --result-position 0.5 --screenshot /tmp/result.png --after 5000
```

화면이 없는 Windows SSH 세션에서는 `$env:QT_QPA_PLATFORM = 'offscreen'`을 먼저 정합니다. 빌드 폴더에 offscreen 플러그인이 함께 들어 있습니다.

## 결과

성공한 요청마다 앱이 읽는 결과 파일 일곱 개를 `output` 폴더에 씁니다. `gif`가 false이면 GIF만 빠지고, `classify`가 false이면 커프 단계가 없습니다.

| 항목 | 값 |
| --- | --- |
| 왼발 PI, 오른발 PI | 0.12%, 0.15% |
| 좌우 비 | 0.80 |
| 커프 상태 | 0(정상) |
| 측정 품질 | 측정 완료 (확인 필요) |
| 시간 창 | 11개(가운데 5초~25초) |
| 영상 | 900장, 30fps |

- **같은 요청이면 npz, GIF, PNG가 바이트까지 같습니다**<br>요약은 `name`과 `dfr`(미리 분할 기록, 걸린 시간)만 요청마다 다릅니다
- **시간 창 수가 요약, npz, GIF에서 같습니다**<br>GIF 아래 흰 막대 길이가 (창 번호 + 1) / 11이라, 결과 창이 재생 위치에 맞는 장을 보이는지 눈으로 확인할 수 있습니다
- **`perfusion.npz`의 배열 이름, dtype, 모양은 약속과 같습니다**<br>앱이 건너뛰어야 하는 문자열 배열(`sides`, `cuff_side`)도 들어 있습니다

## 실패 흉내

입력이 틀린 요청은 실제 분석 프로그램과 같은 문장으로 거절합니다([약속 10절](../../docs/analysis-contract.md#10-오류)). 입력 확인을 통과한 요청의 `name`이 다음 중 하나이면 그 경우를 흉내 냅니다.

| `name` | 동작 |
| --- | --- |
| `fail` | 진행 줄 여섯 개 뒤 두 줄짜리 오류가 든 실패 완료 줄 |
| `hang` | `[1/5]` 뒤 60초 동안 출력 없음 |
| `crash` | `[1/5]` 뒤 오류 기록을 쓰고 종료 코드 3 |
| `nosummary` | 결과 파일 없이 성공 완료 줄 |

앱은 촬영 폴더 이름을 `name`으로 보냅니다. 촬영 폴더를 복사해 폴더 이름을 `fail`로, 매니페스트 이름을 `fail_shot.json`으로 바꾸고, 앱 인자 `--analyze <그 폴더>`로 분석하게 합니다.

## 직접 띄우기

```bash
python3 dfr_worker.py --variant fake --idle-exit 0
```

표준 입력에 [약속 4절](../../docs/analysis-contract.md#4-앱이-보내는-줄)의 줄을 한 줄씩 씁니다. 먼저 `{"preload":true}`를 보내고, 그 뒤 분석 요청을 보냅니다. 요청의 영상 파일은 있어야 하지만 내용은 읽지 않습니다.

| 인자 | 기본값 |
| --- | --- |
| `--variant <판>` | 필수 |
| `--idle-exit <초>` | 600 |
| `--presegment-threads <수>` | 없음 |
| `--prestream [수]` | 없음 |
| `--stage-ms <밀리초>` | 200 |

`--variant`가 `variants/`에 없으면 준비 줄 전에 끝나고, `--idle-exit`가 0 이하이면 스스로 끝나지 않습니다. 영상 머리글의 장 수 확인, 가로로 긴 영상 거절, 첫 장에서 발을 찾지 못한 경우는 영상을 풀어야 하므로 흉내 내지 않습니다.

## 시험

platform 시험이 이 스크립트를 앱과 같은 명령줄로 띄워 `Analyzer`와 결과 읽기로 확인합니다. 파이썬 3.9 이상이 없으면 시험을 건너뜁니다.

```powershell
cmake --build --preset windows-debug --target test_fake_analysis
ctest --preset windows-debug -R FakeAnalysis
```

macOS는 프리셋 이름을 `macos-debug`로 바꿉니다.
