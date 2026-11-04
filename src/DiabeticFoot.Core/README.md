# DiabeticFoot.Core

촬영 판정과 저장 형식을 만드는 순수 라이브러리입니다. 원본 C++ `core/` 층(deepmediHardwareLab)을 C# .NET 8로 옮겼고, 스레드, 시계, 파일 입출력, OS 호출이 없으며 같은 입력에는 항상 같은 출력을 냅니다.

의존은 app, platform.windows, platform, core 순서로 아래로만 향합니다. 이 프로젝트는 다른 프로젝트를 참조하지 않고 BCL만 씁니다.

## 쓰는 규칙

- **네임스페이스**<br>`hwlab::flow`는 `DiabeticFoot.Core.Flow`, `hwlab::pixel`은 `DiabeticFoot.Core.Pixel`, `hwlab::record`는 `DiabeticFoot.Core.Record`, `hwlab::timing`은 `DiabeticFoot.Core.Timing`입니다. 층에 속하지 않는 어휘 타입(시각, 프레임, 설정값)은 `DiabeticFoot.Core`입니다
- **자유 함수**<br>층마다 정적 클래스 하나(`FlowFunctions`, `PixelFunctions`, `RecordFunctions`, `TimingFunctions`)에 원본 함수 이름 그대로 있습니다. 한 값만 보는 단순 판정(`IsEmpty(rect)`, `IsApplied(state)`)은 그 타입의 속성입니다. 원본 헤더의 `inline constexpr` 상수는 가장 가까운 타입의 `const`입니다
- **이름**<br>enum 값은 `k` 접두 없이 PascalCase입니다(`kUyvy`는 `Uyvy`). 필드는 PascalCase입니다(`exposure_step`은 `ExposureStep`). `Event`, `Action`, `Step`은 `FlowEvent`, `FlowAction`, `FlowStep`입니다
- **값 타입과 참조 타입**<br>원본 기본값이 모두 0이거나 비어 있으면 `readonly record struct`, 0이 아닌 기본값(`storable = true`, `rate = 1.0` 등)이나 문자열과 목록 멤버가 있으면 `sealed record`(참조 타입)입니다. C# 구조체의 `default`는 항상 0이라, 0이 아닌 기본값이 있는 타입을 구조체로 두면 `default` 값이 잘못된 상태가 되기 때문입니다. record는 모두 `==`와 `Equals`로 필드 값을 비교합니다. 목록 멤버를 가진 record는 원본이 `operator==`를 정의한 `FrameRateCheck`와 `PpgErrorWindow`만 목록까지 값으로 비교하고, 나머지(`FlowStep`, `Sidecar` 등)는 목록을 참조로 비교하므로 목록은 `SequenceEqual`로 비교합니다
- **갱신**<br>상태 값은 바꾸지 않고 `with` 식으로 새 값을 만듭니다(`state with { Phase = Phase.Settling }`). 원본의 값 복사 인자와 같은 효과입니다
- **선택값**<br>`std::optional<T>`는 `T?`입니다. 오류 문장을 `std::string*`로 돌려주던 함수는 `T? Name(..., out string error)` 모양이고, 성공하면 `error`는 빈 문자열입니다
- **순서열**<br>`std::span<const T>`와 `std::vector<T>`는 모두 `IReadOnlyList<T>`입니다. 배열과 `List<T>`를 그대로 넘기면 복사 없이 읽고, 한 함수의 출력을 다른 함수의 입력에 바로 넣을 수 있습니다. 화소 바이트는 `ReadOnlySpan<byte>`와 `Span<byte>`, 프레임의 평면은 `ReadOnlyMemory<byte>`입니다
- **정수 폭**<br>`int32_t`는 `int`, `int64_t`는 `long`, `uint64_t`는 `ulong`, 인덱스와 개수(`size_t`)는 `int`입니다
- **바이트를 만드는 함수**<br>AVI는 `byte[]`, 사이드카와 매니페스트(`BuildSidecarJson`, `BuildPpgSidecarJson`, `BuildManifestJson`)는 `string`을 돌려줍니다. 원본이 UTF-8 바이트를 그대로 썼으므로 파일에 쓸 때는 BOM 없는 UTF-8(`new UTF8Encoding(false)`)로 인코딩하고 줄 끝은 `\n` 그대로 둡니다. `snprintf`로 만들던 소수 자리(`%.4f` 등)는 같은 문자열을 내는 내부 도우미(`CFormat`)를 씁니다. 꼭 절반인 값(0.125)을 짝수 쪽으로 반올림하는 점이 `double.ToString("F2")`와 다릅니다
- **프레임**<br>`FrameView`는 화소를 소유하지 않고 `ReadOnlyMemory<byte>`로 가리킵니다. 평면 메모리는 그 뷰를 받은 함수가 돌려줄 때까지만 유효하다고 보고 쓰며, 범위를 벗어나면 정의되지 않은 동작 대신 예외가 납니다

## 원본 헤더와 C# 대응

원본 경로는 `core/include/hwlab/`, `core/src/<층>/include/hwlab/<층>/`입니다. C# 경로는 `src/DiabeticFoot.Core/`입니다.

### 어휘

| 원본 헤더 | C# 파일 |
| --- | --- |
| `timestamp.h` | `Timestamp.cs` |
| `stream.h` | `Stream.cs` |
| `frame.h` | `Frame.cs` |
| `camera_settings.h` | `CameraSettings.cs` |

- **Timestamp.cs**<br>`Timestamp`
- **Stream.cs**<br>`StreamKind`, `FrameIndex`
- **Frame.cs**<br>`PixelFormat`, `FrameView`
- **CameraSettings.cs**<br>`SettingKind`, `SettingMode`, `CameraState`, `CameraSettings`

`IntervalUs(from, to)`는 `Timestamp.IntervalUs`, `ExposureStepMs(step)`는 `CameraSettings.ExposureStepMs`입니다. `FrameIndex`는 `uint64_t` 별칭 대신 값 타입입니다.

### timing

함수는 `TimingFunctions`에 있습니다.

| 원본 헤더 | C# 파일 |
| --- | --- |
| `frame_gap.h` | `Timing/FrameGap.cs` |
| `settle.h` | `Timing/Settle.cs` |
| `clock_map.h` | `Timing/ClockMap.cs` |
| `record_rate.h` | `Timing/RecordRate.cs` |
| `window_index.h` | `Timing/WindowIndex.cs` |

- **FrameGap.cs**<br>`FrameGap`, `SlowSpan`, `FrameRateCheck`
- **Settle.cs**<br>`SettleRule`, `SettleState`
- **ClockMap.cs**<br>`ClockSample`, `ClockMap`, `ClockSegment`
- **RecordRate.cs**<br>`RecordRateRule`, `RecordRateState`, `RecordRateStep`

함수: `FindFrameGaps`, `CountMissingFrames`, `MedianIntervalUs`, `MatchesPeriod`, `CheckFrameRate`, `AfterSettingChange`, `AfterFrame`, `FitClockMap`, `ToCommonTime`(구간 한 개, 구간 목록), `FitClockSegments`, `NearestFrames`, `AdvanceRecordRate`, `WindowIndexAt`. `IsApplied`와 `IsSettled`는 `SettleState`의 속성입니다.

### pixel

함수는 `PixelFunctions`에 있습니다.

| 원본 헤더 | C# 파일 |
| --- | --- |
| `rect.h` | `Pixel/Rect.cs` |
| `brightness.h` | `Pixel/Brightness.cs` |
| `crop.h` | `Pixel/Crop.cs` |
| `convert.h` | `Pixel/Convert.cs` |
| `rgb_sums.h` | `Pixel/RgbSums.cs` |
| `side_regions.h` | `Pixel/SideRegions.cs` |
| `calibration_watch.h` | `Pixel/CalibrationWatch.cs` |
| `foot_warmth.h` | `Pixel/FootWarmth.cs` |

- **Rect.cs**<br>`Rect`, `ChromaGrid`
- **Brightness.cs**<br>`Brightness`, `Flicker`, `LightResponse`, `LightResponseRule`
- **Convert.cs**<br>`Y16Range`, `Rgb8`, `Palette`
- **RgbSums.cs**<br>`RgbSums`, `RgbChannels`
- **SideRegions.cs**<br>`SideRegions`, `Side`, `SideProblem`, `SideFinding`
- **CalibrationWatch.cs**<br>`CalibrationRule`, `CalibrationPhase`, `CalibrationWatch`
- **FootWarmth.cs**<br>`Warmth`, `FootWarmthCheck`

`uyvy_rgb.h`는 내부 도우미(`UyvyRgb.cs`)입니다. `IsEmpty`와 `IsPortrait`는 `Rect`의 속성, `IsCalibrationFinished`는 `CalibrationWatch`의 속성입니다. `std::array<int64_t, 3>`은 `RgbChannels`(R, G, B와 `this[int]`)입니다. `FootWarmthCheck`는 상태를 가진 클래스이고 스레드 안전하지 않습니다.

### record

함수는 `RecordFunctions`에 있습니다.

| 원본 헤더 | C# 파일 |
| --- | --- |
| `avi.h` | `Record/Avi.cs` |
| `sidecar.h` | `Record/Sidecar.cs` |
| `shot.h` | `Record/Shot.cs` |
| `shot_limits.h` | `Record/ShotLimits.cs` |
| `analysis_inputs.h` | `Record/AnalysisInputs.cs` |

- **Avi.cs**<br>`VideoFormat`, `FilePatch`, `AviOutput`, `AviBuilder`
- **Sidecar.cs**<br>`Sidecar`, `LightRecord`, `LensShadingRecord`, `FrameRecord`, `PpgSidecar`, `PpgEvent`, `PpgErrorSpan`, `PpgErrorShare`, `PpgErrorWindow`, `FootSide`, `LightSource`, `LensShadingSource`
- **Shot.cs**<br>`ShotInfo`, `ShotLayout`, `ShotFile`, `ShotDevice`, `ShotFolderPath`, `FileRole`, `ShotProblem`, `LightingInfo`, `ThermalAbsence`, `BloodPressure`, `FootWarmthRecord`
- **ShotLimits.cs**<br>`VideoSize`, `ShotSizePlan`, `FreeSpace`, `FreeSpaceJudgement`
- **AnalysisInputs.cs**<br>`SidecarSummary`, `AnalysisFiles`, `InputFinding`, `InputProblem`

`AviBuilder`는 클래스이고 `FrameBytes`, `FramesWritten`, `IsValid`가 속성입니다. 호출 순서는 `Start`, (`BeginFrame` 뒤 `FrameBytes`만큼 화소 덧붙이기) 반복, `Finish`입니다. 상수는 `AviBuilder.DefaultRiffLimitBytes`, `BloodPressure.MinMmhg`, `BloodPressure.MaxMmhg`, `BloodPressure.AnkleLevels`, `ShotLayout.CollectedResultFolder`, `ShotLayout.MaxReanalysisNumber`, `ShotSizePlan.SidecarBytesPerFrame`, `AnalysisFiles.MinThermalFramesInRgbWindow`입니다. `ShotLayout.NameOf(role)`는 원본처럼 멤버 함수입니다. `CFormat`과 `JsonWriter`는 내부 도우미입니다.

### flow

함수는 `FlowFunctions`에 있습니다.

| 원본 헤더 | C# 파일 |
| --- | --- |
| `flow.h` | `Flow/Flow.cs` |
| `settings_plan.h` | `Flow/SettingsPlan.cs` |
| `capture_spec.h` | `Flow/CaptureSpec.cs` |

- **Flow.cs**<br>`Phase`, `EventKind`, `FlowEvent`, `EndReason`, `ActionKind`, `FlowAction`, `FlowRule`, `FlowState`, `FlowStep`
- **SettingsPlan.cs**<br>`SettingsSpec`, `CommandKind`, `SettingCommand`, `SettingField`
- **CaptureSpec.cs**<br>`VideoCodec`, `CaptureSpec`, `SpecProblem`

상수는 `CaptureSpec.MaxFrameSide`, `CaptureSpec.MinExposureStep`, `CaptureSpec.MaxExposureStep`, `CaptureSpec.MaxGain`, `CaptureSpec.MaxWhiteBalanceK`입니다. `AllowsLightChange(phase)`는 enum에 멤버를 둘 수 없어 `FlowFunctions`에 있습니다.

`Advance`가 촬영 흐름의 판단을 모두 합니다. 상태와 사건을 받아 다음 상태와 platform이 순서대로 실행할 할 일을 돌려줍니다. 선언 파일 읽기는 `CaptureSpec? ParseCaptureSpec(string json, out string error)`이고 오류 문장과 "n번째 바이트" 위치는 원본과 같습니다(입력을 UTF-8 바이트로 보고 셉니다). 모르는 키가 여럿이면 원본처럼 바이트 순서로 앞선 키를 알립니다. 선언 파일은 `new UTF8Encoding(false).GetString(File.ReadAllBytes(path))`로 읽습니다. `File.ReadAllText`는 UTF-8 BOM을 지워 원본이 거부하는 BOM 붙은 파일을 통과시키고, 잘못된 UTF-8 바이트를 U+FFFD로 바꿉니다.

## 원본과 달라진 점

- **참조 타입 반환**<br>`ChromaGridOf`는 호출마다 새 `ChromaGrid`를 만듭니다. 원본은 값 반환이었습니다
- **오류 반환**<br>`ReadSidecarSummary`는 성공하면 `error`를 빈 문자열로 둡니다. 원본은 성공하면 `error`를 건드리지 않았습니다
- **버퍼 범위**<br>원본은 평면 포인터를 범위 검사 없이 읽었고, C#은 범위를 벗어나면 예외입니다. 정상 입력에서는 같은 결과입니다
- **소수 서식**<br>사이드카의 소수 자리는 `CFormat.Fixed`로 만듭니다. `snprintf("%.*f")`와 같은 문자열을 내는 것을 160건으로 확인했습니다

## 테스트

`tests/DiabeticFoot.Core.Tests`가 원본 `core/test/`의 모든 테스트를 같은 입력과 같은 기대값으로 옮긴 것입니다. 원본 테스트가 읽는 데이터 파일은 `TestData/`에 있습니다.

```
dotnet test tests/DiabeticFoot.Core.Tests
```
