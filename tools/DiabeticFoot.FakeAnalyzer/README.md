# 가짜 분석기

<mark>**관류 분석 프로그램 대신 띄워 앱의 분석 상태 화면과 결과 창을 만들고 시험하는 콘솔 프로그램이고, 모든 요청에 같은 고정 결과를 냅니다.**</mark> 주고받는 줄과 결과 파일은 [분석 연동 약속](../../docs/analysis-contract.md)과 같습니다. 결과 값은 식으로 만든 숫자라 실제 분석과 관계가 없습니다.

## 빌드

```text
dotnet build tools/DiabeticFoot.FakeAnalyzer
```

`tools/DiabeticFoot.FakeAnalyzer/bin/Debug/net8.0/`에 실행 파일(`DiabeticFoot.FakeAnalyzer.exe`, `DiabeticFoot.FakeAnalyzer.dll`)과 판 정보 `variants/fake/VARIANT.json`이 생깁니다.

## 앱에서 띄우기

앱은 분석 프로그램을 `<파이썬> <스크립트> --idle-exit 0 ... --variant <판>`으로 띄우고, 판은 스크립트 폴더의 `variants/`에서 찾습니다([약속 2절](../../docs/analysis-contract.md#2-분석-프로그램-찾기와-띄우기)). 가짜 분석기는 `--`로 시작하지 않는 첫 인자를 무시하고 자기 폴더의 `variants/`를 쓰므로, 파이썬 자리에 실행 파일을, 스크립트 자리에 같은 폴더의 dll을 주면 앱 코드를 바꾸지 않고 띄울 수 있습니다.

```text
파이썬     <bin>\DiabeticFoot.FakeAnalyzer.exe
스크립트   <bin>\DiabeticFoot.FakeAnalyzer.dll
```

C++ 앱에서는 `--analyzer-python`과 `--analyzer-script` 인자가 이 두 경로를 받습니다. 판 이름은 `fake`이고, 결과는 `<촬영 폴더>/perfusion/fake/`에 생깁니다.

## 직접 띄우기

```text
dotnet DiabeticFoot.FakeAnalyzer.dll --variant fake --idle-exit 0
```

표준 입력에 [약속 4절](../../docs/analysis-contract.md#4-앱이-보내는-줄)의 줄을 한 줄씩 씁니다. 먼저 `{"preload":true}`를 보내고, 그 뒤 분석 요청을 보냅니다. 요청의 영상 파일은 있어야 하지만 내용은 읽지 않습니다.

| 인자 | 기본값 | 하는 일 |
| --- | --- | --- |
| `--variant <판>` | 필수 | `variants/<판>/VARIANT.json`이 없으면 준비 줄 전에 끝남 |
| `--idle-exit <초>` | 600 | 0 이하이면 스스로 끝나지 않음 |
| `--presegment-threads <n>` | 없음 | 녹화 중 첫 장 미리 분할을 켬 |
| `--prestream [n]` | 없음 | 녹화 중 미리 읽기와 미리 분할을 켬 |
| `--stage-ms <ms>` | 200 | 진행 단계 사이 대기. 가짜 분석기에만 있음 |

## 결과

성공한 요청마다 앱이 읽는 결과 파일([약속 7절](../../docs/analysis-contract.md#7-결과-파일))을 `output` 폴더에 씁니다. `gif`가 false이면 GIF만 빠집니다.

- **값은 모든 요청에 같습니다**<br>왼발 PI 0.12%, 오른발 PI 0.15%, 커프 상태 0(정상), 측정 품질 "측정 완료 (확인 필요)"와 경고 항목 하나, 30초 녹화(900장, 30fps), 시간 창 11개입니다
- **같은 요청이면 npz, GIF, PNG가 바이트까지 같습니다**<br>요약은 `name`과 `dfr`(미리 분할 기록, 걸린 시간)만 요청마다 다릅니다
- **GIF 아래 흰 막대 길이가 창 번호를 나타냅니다**<br>창 k의 장은 막대가 (k + 1) / 11만큼 차 있어, 결과 창이 재생 위치에 맞는 장을 보이는지 눈으로 확인할 수 있습니다
- **`perfusion.npz`의 배열 이름, dtype, 모양은 실제 결과와 같습니다**<br>앱이 건너뛰어야 하는 문자열 배열(`sides`, `cuff_side`)도 실제처럼 들어 있습니다
- **그림을 미루는 요청이면 요약을 한 번 더 바꿔 씁니다**<br>실제 분석 프로그램처럼 figures 줄 전에 임시 파일에 쓰고 이름을 바꿉니다. 앱이 요약 파일을 열어 두고 있어 바꾸지 못하면 `[dfr] stamp-failed`를 찍습니다

## 실패 흉내

요청의 입력 확인은 실제 분석 프로그램과 같은 문장으로 거절합니다([약속 10절](../../docs/analysis-contract.md#10-오류)). `rec_dir`, `output` 없음, 영상 경로가 한쪽만 있음, 영상 파일 없음, `pad_classifier: true`, JSON이 아닌 줄(BOM이 붙은 줄 포함)입니다.

입력 확인을 통과한 요청의 `name`이 다음 중 하나이면 그 경우를 흉내 냅니다.

| `name` | 동작 |
| --- | --- |
| `fail` | 진행 줄 여섯 개 뒤 여러 줄 오류가 든 실패 완료 줄 |
| `hang` | `[1/5]` 뒤 60초 동안 출력 없음, 완료 줄 없음 |
| `crash` | `[1/5]` 뒤 표준 오류에 예외 기록, 종료 코드 3 |
| `nosummary` | 결과 파일 없이 성공 완료 줄 |

앱은 촬영 폴더 이름을 `name`으로 보내므로, 앱에서 흉내 내려면 촬영 폴더 이름을 `fail` 등으로 바꾸고 매니페스트 이름도 `fail_shot.json`으로 바꾼 폴더를 분석하게 합니다.

다음은 흉내 내지 않습니다. 영상 머리글의 장 수 확인, 가로로 긴 영상 거절, 첫 장에서 발을 찾지 못함, 분석 중 예외입니다. 영상을 풀어야 하는 경우들입니다.

## 시험

```text
dotnet test tests/DiabeticFoot.FakeAnalyzer.Tests
```

시험은 빌드한 가짜 분석기를 앱과 같은 명령줄로 띄우고, 출력 줄과 결과 파일이 약속을 지키는지 확인합니다. npz는 앱의 읽기와 같은 범위(무압축, CRC, npy 머리)로, GIF와 PNG는 직접 풀어서 확인합니다.
