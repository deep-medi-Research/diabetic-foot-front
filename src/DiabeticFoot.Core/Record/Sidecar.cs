using DiabeticFoot.Core.Pixel;
using DiabeticFoot.Core.Timing;

namespace DiabeticFoot.Core.Record;

/// <summary>영상이 담은 발입니다. 환자 기준 왼발과 오른발입니다.</summary>
public enum FootSide : byte
{
    Left,
    Right,
}

/// <summary>촬영 조명을 누가 켜고 정했는지입니다. 매니페스트 lighting.source와 사이드카 light.source가 같은 값을 씁니다.</summary>
public enum LightSource : byte
{
    Unknown,

    /// <summary>앱 밖에서 사람이 켬(예: LED 보드에 전원만 넣음). 앱은 밝기를 보내지도 읽지도 않습니다.</summary>
    External,

    /// <summary>앱이 직렬 포트로 밝기를 보냅니다(platform의 Light).</summary>
    App,
}

/// <summary>열화상 렌즈 셰이딩 보정을 어떻게 적용했는지입니다(설계 문서 표 2).</summary>
public enum LensShadingSource : byte
{
    /// <summary>이 카메라의 보정 파일이 없어 보정하지 않았습니다.</summary>
    None,

    /// <summary>열 때 이 카메라의 보정 파일을 불러와 적용했습니다.</summary>
    Loaded,

    /// <summary>이번 실행에서 사람이 렌즈를 가리고 보정했습니다.</summary>
    Corrected,

    /// <summary>불러오기나 보정이 실패했습니다. 적용된 보정을 알 수 없습니다(보정이 실패한 뒤 SDK가 앞 보정을 두는지는 확인하지 않았습니다).</summary>
    Failed,
}

/// <summary>
/// 열화상 사이드카의 렌즈 셰이딩 보정 기록(lens_shading)입니다.
/// </summary>
/// <param name="Source">보정 적용 방식입니다.</param>
/// <param name="FileUnixS">적용한 보정 파일의 수정 시각(UTC 유닉스 초)입니다. 보정한 날짜를 보이는 데 씁니다. 파일이 없거나 저장하지 못했으면 비웁니다.</param>
/// <remarks>Loaded와 Corrected는 SDK가 성공을 반환했다는 뜻이고, 보정할 때 렌즈 앞이 고른 면이었는지는 알 수 없습니다.</remarks>
public readonly record struct LensShadingRecord(LensShadingSource Source = LensShadingSource.None, long? FileUnixS = null);

/// <summary>
/// RGB 사이드카의 조명 기록(light)입니다.
/// </summary>
/// <remarks>
/// Source와 Percent는 같은 촬영의 매니페스트 lighting.source, lighting.brightness_percent와 같은 값입니다.
/// Percent는 표 2의 조명 밝기(LED 보드에 보내는 데 성공한 값)이고 장치에서 읽은 값이 아닙니다.
/// 포트는 COM 이름 대신 USB VID:PID와 일련번호로 남깁니다. COM 번호는 꽂는 USB 단자에 따라 바뀌기 때문입니다.
/// </remarks>
public sealed record LightRecord
{
    public LightSource Source { get; init; } = LightSource.Unknown;

    /// <summary>녹화를 시작할 때의 조명 밝기(0에서 100)입니다. 앱이 보내지 않았으면 비웁니다.</summary>
    public int? Percent { get; init; }

    /// <summary>LED 보드 직렬 변환기의 VID입니다. 모르면 비웁니다.</summary>
    public ushort? UsbVendorId { get; init; }

    /// <summary>LED 보드 직렬 변환기의 PID입니다. 모르면 비웁니다.</summary>
    public ushort? UsbProductId { get; init; }

    /// <summary>직렬 변환기의 USB 일련번호입니다. 모르면 빈 문자열(null로 씀)입니다.</summary>
    public string Serial { get; init; } = "";

    /// <summary>사람이 LED 밝기가 바뀌는 것을 보고 이 포트가 LED 보드임을 확인했습니다.</summary>
    public bool Confirmed { get; init; }

    /// <summary>마지막 LED 변경이 영상 밝기에 나타났는지입니다.</summary>
    public LightResponse Response { get; init; } = LightResponse.Unknown;

    /// <summary>이 영상 파일에 쓴 프레임의 깜빡임 요약입니다. 녹화 뒤 채웁니다.</summary>
    public Flicker? Flicker { get; init; }
}

/// <summary>
/// 파일에 쓴 프레임 하나의 기록입니다. 노출과 게인은 프레임에 붙어 오지 않아 프레임마다 남길 수 없습니다.
/// </summary>
/// <param name="Index">스트림을 연 뒤 받은 순서입니다.</param>
/// <param name="DeviceTime">장치 또는 드라이버가 붙인 시각입니다.</param>
/// <param name="HostTime">PC가 받은 시각입니다. 장치 시각이 실제 간격과 다른지 대조하는 데 씁니다.</param>
/// <param name="BrightnessSum">영역 안 화소의 밝기 합계입니다.</param>
/// <param name="ClippedPixels">밝기가 clip_level 이상인 화소 수입니다.</param>
public readonly record struct FrameRecord(
    FrameIndex Index = default,
    Timestamp DeviceTime = default,
    Timestamp HostTime = default,
    long BrightnessSum = 0,
    long ClippedPixels = 0);

/// <summary>영상 파일 하나에 대한 기록(사이드카)입니다. 영상 파일과 같은 이름의 JSON으로 저장합니다.</summary>
public sealed record Sidecar
{
    /// <summary>같은 폴더 안의 파일 이름입니다.</summary>
    public string VideoFile { get; init; } = "";

    public string Device { get; init; } = "";

    public StreamKind Stream { get; init; }

    public VideoFormat Format { get; init; }

    /// <summary>ffv1, uncompressed입니다.</summary>
    public string Codec { get; init; } = "";

    /// <summary>좌우로 나누지 않은 전체 화면이면 비웁니다.</summary>
    public FootSide? Foot { get; init; }

    public Rect Crop { get; init; }

    public SettingMode Mode { get; init; }

    /// <summary>녹화 직전에 다시 읽은 값입니다. 자동 맞춤의 노출값은 읽을 방법이 없어 비웁니다.</summary>
    public CameraState Settings { get; init; }

    /// <summary>조명 기록입니다. RGB 사이드카에만 채우고 열화상 사이드카는 비웁니다(null).</summary>
    public LightRecord? Light { get; init; }

    /// <summary>
    /// 녹화를 끝낸 때 열린 열화상 연결의 렌즈 셰이딩 보정 기록입니다. RGB 사이드카, 보정 기능이 없는 열화상 카메라,
    /// 녹화를 끝낸 때 열화상이 끊겨 있던 경우는 비웁니다(null).
    /// </summary>
    public LensShadingRecord? LensShading { get; init; }

    /// <summary>장치 시각을 공통 시간축(PC 시계)으로 옮기는 식입니다. 프레임별 공통 시각은 이 식으로 계산합니다.</summary>
    public ClockMap Clock { get; init; } = new();

    /// <summary>열화상 확인용 영상의 표시 범위(원시값)입니다. 열화상이 아니거나 확인용 영상이 없으면 비웁니다.</summary>
    public Y16Range? DisplayRange { get; init; }

    public long TargetFrames { get; init; }

    public long WrittenFrames { get; init; }

    /// <summary>녹화 중 받은 프레임 수입니다. 기록 주기로 거른 프레임과 저장 누락을 포함합니다.</summary>
    public long ReceivedFrames { get; init; }

    /// <summary>기록 주기(<see cref="RecordRateRule"/>)입니다. 0이면 받은 프레임을 모두 기록합니다.</summary>
    public long RecordPeriodUs { get; init; }

    /// <summary>RGB 녹화의 첫 프레임과 마지막 프레임 사이 공통 시각에 기록한 프레임 수입니다. 관류 분석은 열화상의 이 장 수를 봅니다.</summary>
    public long WrittenInRgbWindow { get; init; }

    public Timestamp FirstFrame { get; init; }

    public Timestamp LastFrame { get; init; }

    /// <summary>프레임마다 밝기를 잰 화소 수(Crop 영역)입니다.</summary>
    public long BrightnessPixels { get; init; }

    public int ClipLevel { get; init; }

    /// <summary>파일에 쓴 순서입니다.</summary>
    public IReadOnlyList<FrameRecord> Records { get; init; } = Array.Empty<FrameRecord>();

    /// <summary>카메라 단 누락입니다.</summary>
    public IReadOnlyList<FrameGap> Gaps { get; init; } = Array.Empty<FrameGap>();

    /// <summary>저장 누락입니다.</summary>
    public long DroppedFrames { get; init; }

    /// <summary>쓰기 대기열 크기입니다.</summary>
    public int BufferFrames { get; init; }

    /// <summary>녹화 중 쓰기 대기열이 가장 길었던 값입니다. BufferFrames에 닿으면 저장 누락이 생깁니다.</summary>
    public int MaxQueueFrames { get; init; }

    /// <summary>영상 파일 크기입니다. 모르면 -1입니다.</summary>
    public long FileBytes { get; init; } = -1;

    /// <summary>받은 프레임 간격의 중앙값입니다. 선언 fps와 다르면 카메라가 fps를 바꾼 것입니다.</summary>
    public long MedianIntervalUs { get; init; }

    /// <summary>느린 간격(주기의 1.2배 초과 1.5배 이하)과 1초 창 최저 fps입니다. 받은 프레임 시각으로 잽니다.</summary>
    public FrameRateCheck FrameRate { get; init; } = new();
}

/// <summary>PPG 기록 중 장치가 보낸 상태 메시지나 오류 메시지 하나입니다.</summary>
/// <param name="HostTime">PC 수신 시각입니다.</param>
/// <param name="Code">메시지 코드입니다. 's' 상태, 'e' 오류입니다.</param>
/// <param name="Value">상태 값('v', 's', 'p', 'u', 'o') 또는 오류 값입니다.</param>
public readonly record struct PpgEvent(Timestamp HostTime = default, byte Code = 0, byte Value = 0);

/// <summary>
/// PPG 기록에서 같은 측정 오류 값이 이어진 표본들입니다(측정 오류 구간).
/// </summary>
/// <param name="Code">측정 오류 값('f' 손가락 빠짐, 's' 신호 나쁨, 'p' 센서 연결 안 됨)입니다.</param>
/// <param name="Begin">첫 표본의 공통 시각입니다.</param>
/// <param name="End">마지막 표본의 공통 시각입니다.</param>
/// <param name="Samples">구간의 표본 수입니다.</param>
/// <remarks>
/// 장치는 측정 데이터(약 48ms)마다 측정 오류 값 하나를 보내므로 구간은 측정 데이터 단위로 바뀝니다. 측정 데이터 하나의
/// 표본 수는 촬영 네 번에서 평균 23.91~23.98개였습니다(2026-10-21 실측).
/// </remarks>
public readonly record struct PpgErrorSpan(byte Code = 0, Timestamp Begin = default, Timestamp End = default, long Samples = 0);

/// <summary>공통 시각 구간 하나 안에서 측정 오류 값 하나가 나온 양입니다.</summary>
/// <param name="Code">측정 오류 값입니다.</param>
/// <param name="Samples">이 값인 표본 수입니다.</param>
/// <param name="LongestSamples">이 값이 가장 길게 이어진 표본 수입니다.</param>
/// <param name="Runs">이 값이 이어진 횟수입니다. 끊겼다가 다시 나오면 하나씩 늘어납니다.</param>
public readonly record struct PpgErrorShare(byte Code = 0, long Samples = 0, long LongestSamples = 0, int Runs = 0);

/// <summary>공통 시각 구간 하나(RGB 녹화 구간) 안 PPG 표본의 측정 오류 집계입니다.</summary>
public sealed record PpgErrorWindow
{
    /// <summary>RGB 첫 장의 공통 시각입니다.</summary>
    public Timestamp Begin { get; init; }

    /// <summary>RGB 마지막 장의 공통 시각입니다.</summary>
    public Timestamp End { get; init; }

    /// <summary>구간 안 표본 수입니다. 비율의 분모입니다.</summary>
    public long Samples { get; init; }

    /// <summary>구간 안에 나온 측정 오류 값마다 하나입니다. 값 순서입니다.</summary>
    public IReadOnlyList<PpgErrorShare> Codes { get; init; } = Array.Empty<PpgErrorShare>();

    public bool Equals(PpgErrorWindow? other) =>
        other is not null && Begin == other.Begin && End == other.End && Samples == other.Samples &&
        Codes.SequenceEqual(other.Codes);

    public override int GetHashCode() => HashCode.Combine(Begin, End, Samples, Codes.Count);
}

/// <summary>PPG 파일 하나의 사이드카에 남기는 값입니다.</summary>
public sealed record PpgSidecar
{
    /// <summary>PPG 파일 이름(폴더 없이)입니다.</summary>
    public string SamplesFile { get; init; } = "";

    /// <summary>장치를 연 직렬 포트 이름입니다.</summary>
    public string Port { get; init; } = "";

    /// <summary>장치 정보 응답의 데이터(ASCII 15바이트: MP-SDK 버전 6자리, HRV 기준 6자리, APG 기준 3자리)입니다. 받지 못했으면 비웁니다.</summary>
    public string? DeviceInfo { get; init; }

    public int SampleRateHz { get; init; }

    /// <summary>기록을 시작한 PC 시각입니다.</summary>
    public Timestamp RecordBegin { get; init; }

    /// <summary>기록을 끝낸 PC 시각입니다.</summary>
    public Timestamp RecordEnd { get; init; }

    public IReadOnlyList<ClockSegment> Clock { get; init; } = Array.Empty<ClockSegment>();

    public long Samples { get; init; }

    /// <summary>표본을 담은 측정 데이터 수입니다. 시계 맞춤 표본 수와 같습니다.</summary>
    public long Messages { get; init; }

    /// <summary>첫 표본의 공통 시각입니다.</summary>
    public Timestamp FirstSample { get; init; }

    /// <summary>마지막 표본의 공통 시각입니다.</summary>
    public Timestamp LastSample { get; init; }

    /// <summary>공통 시각으로 잰 표본 간격의 중앙값입니다. 장치 시계 속도가 드러납니다.</summary>
    public long MedianIntervalUs { get; init; }

    /// <summary>공통 시각의 표본 간격으로 찾은 누락입니다. 수신 빈틈이 여기에 남습니다.</summary>
    public IReadOnlyList<FrameGap> Gaps { get; init; } = Array.Empty<FrameGap>();

    /// <summary>측정 오류가 0이 아닌 표본 수입니다.</summary>
    public long MeasureErrorSamples { get; init; }

    /// <summary>기록 동안 해석기가 버린 시작 후보와 바이트입니다.</summary>
    public long CrcFailures { get; init; }

    public long BadLengths { get; init; }

    public long Implausible { get; init; }

    public long SkippedBytes { get; init; }

    public IReadOnlyList<PpgEvent> Events { get; init; } = Array.Empty<PpgEvent>();

    /// <summary>기록 전체의 측정 오류 구간입니다.</summary>
    public IReadOnlyList<PpgErrorSpan> ErrorSpans { get; init; } = Array.Empty<PpgErrorSpan>();

    /// <summary>RGB 녹화 구간 안 측정 오류 집계입니다. RGB 사이드카를 읽지 못했으면 비웁니다.</summary>
    public PpgErrorWindow? RgbWindow { get; init; }
}

public static partial class RecordFunctions
{
    // 2: stream, light_percent, clock, per_frame.common_time_us 추가
    // 3: display_range, frames.received, frames.record_period_us, frames.written_in_rgb_window 추가
    // 4: light_percent를 light(source, percent, port_vid_pid, serial, confirmed, response, flicker)로 바꿈
    // 5: settings.exposure_ms(노출 단계의 명목 노출 시간) 추가
    // 6: lens_shading(열화상 렌즈 셰이딩 보정 결과) 추가
    // 7: lens_shading을 source(none, loaded, corrected, failed)와 file_unix_s(보정 파일의 수정 시각)로 바꿈
    // 8: frames.slow_intervals, frames.min_fps_1s, frames.slow_spans(느린 간격, CheckFrameRate) 추가
    private const int SchemaVersion = 8;

    // PPG 사이드카는 영상 사이드카와 따로 셉니다.
    // 2: device_info(장치 정보 응답) 추가
    private const int PpgSchemaVersion = 2;

    /// <summary>
    /// 표본마다의 측정 오류에서 측정 오류 구간을 찾습니다.
    /// </summary>
    /// <param name="times">표본마다 공통 시각. 시각 순서입니다</param>
    /// <param name="errors">표본마다 측정 오류 값(0 정상). times와 길이가 다르면 짧은 쪽까지만 봅니다</param>
    /// <returns>시각 순서의 측정 오류 구간</returns>
    public static IReadOnlyList<PpgErrorSpan> FindPpgErrorSpans(IReadOnlyList<Timestamp> times, IReadOnlyList<byte> errors)
    {
        var spans = new List<PpgErrorSpan>();
        int n = Math.Min(times.Count, errors.Count);
        for (int i = 0; i < n; i++)
        {
            if (errors[i] == 0)
            {
                continue;
            }
            if (i > 0 && errors[i - 1] == errors[i])
            {
                PpgErrorSpan last = spans[^1];
                spans[^1] = last with { End = times[i], Samples = last.Samples + 1 };
                continue;
            }
            spans.Add(new PpgErrorSpan(errors[i], times[i], times[i], 1));
        }
        return spans;
    }

    /// <summary>
    /// 공통 시각 [begin, end] 안 표본의 측정 오류를 값마다 셉니다.
    /// </summary>
    /// <param name="times">표본마다 공통 시각. 시각 순서입니다</param>
    /// <param name="errors">표본마다 측정 오류 값(0 정상). times와 길이가 다르면 짧은 쪽까지만 봅니다</param>
    /// <param name="begin">구간 시작(포함). RGB 첫 장의 공통 시각</param>
    /// <param name="end">구간 끝(포함). RGB 마지막 장의 공통 시각</param>
    /// <returns>집계. 구간 안 표본이 없으면 Samples가 0이고 Codes가 빕니다</returns>
    public static PpgErrorWindow SummarizePpgErrors(
        IReadOnlyList<Timestamp> times, IReadOnlyList<byte> errors, Timestamp begin, Timestamp end)
    {
        long samples = 0;
        var byCode = new PpgErrorShare[256];
        byte previous = 0;
        long run = 0;
        int n = Math.Min(times.Count, errors.Count);
        for (int i = 0; i < n; i++)
        {
            if (times[i] < begin || times[i] > end)
            {
                previous = 0;
                continue;
            }
            samples++;
            byte code = errors[i];
            if (code != previous)
            {
                run = 0;
            }
            previous = code;
            if (code == 0)
            {
                continue;
            }
            PpgErrorShare share = byCode[code];
            if (run == 0)
            {
                share = share with { Runs = share.Runs + 1 };
            }
            run++;
            share = share with { Samples = share.Samples + 1, LongestSamples = Math.Max(share.LongestSamples, run) };
            byCode[code] = share;
        }
        var codes = new List<PpgErrorShare>();
        for (int code = 1; code < byCode.Length; code++)
        {
            if (byCode[code].Samples == 0)
            {
                continue;
            }
            codes.Add(byCode[code] with { Code = (byte)code });
        }
        return new PpgErrorWindow { Begin = begin, End = end, Samples = samples, Codes = codes };
    }

    /// <summary>
    /// 사이드카 JSON을 만듭니다.
    /// </summary>
    /// <param name="sidecar">기록</param>
    /// <returns>
    /// JSON 문자열. 같은 입력에는 같은 바이트를 내며, 키 순서는 고정이고 비어 있는 값은 null입니다.
    /// 파일에 쓸 때는 BOM 없는 UTF-8로 인코딩합니다
    /// </returns>
    /// <remarks>
    /// 프레임별 기록은 값마다 배열 하나로 씁니다(numpy로 바로 읽음). 평균 밝기는 brightness_sum / brightness_pixels입니다.
    /// 프레임별 공통 시각(common_time_us)은 <c>ToCommonTime(clock, device_time)</c>이며, 스트림이 달라도 같은 시간축이라
    /// RGB 사이드카와 열화상 사이드카의 프레임을 이 값으로 비교합니다.
    /// </remarks>
    public static string BuildSidecarJson(Sidecar sidecar)
    {
        Sidecar s = sidecar;
        var w = new JsonWriter();
        w.Open(null, '{');
        w.Int("schema_version", SchemaVersion);
        w.String("video_file", s.VideoFile);
        w.String("device", s.Device);
        w.String("stream", NameOf(s.Stream));
        if (s.Foot is FootSide foot)
        {
            w.String("foot", foot == FootSide.Left ? "left" : "right");
        }
        else
        {
            w.Null("foot");
        }
        if (s.Light is LightRecord light)
        {
            WriteLight(w, light);
        }
        else
        {
            w.Null("light");
        }
        if (s.LensShading is LensShadingRecord lens)
        {
            w.Open("lens_shading", '{');
            w.String("source", NameOf(lens.Source));
            if (lens.FileUnixS is long fileUnixS)
            {
                w.Int("file_unix_s", fileUnixS);
            }
            else
            {
                w.Null("file_unix_s");
            }
            w.Close('}');
        }
        else
        {
            w.Null("lens_shading");
        }

        w.Open("format", '{');
        w.String("pixel_format", NameOf(s.Format.Format));
        w.Int("width", s.Format.Width);
        w.Int("height", s.Format.Height);
        w.Int("frame_rate", s.Format.FrameRate);
        w.String("codec", s.Codec);
        w.Close('}');

        w.Open("crop", '{');
        w.Int("x", s.Crop.X);
        w.Int("y", s.Crop.Y);
        w.Int("width", s.Crop.Width);
        w.Int("height", s.Crop.Height);
        w.Close('}');

        w.Open("settings", '{');
        w.String("mode", s.Mode == SettingMode.Auto ? "auto" : "manual");
        w.OptionalBool("exposure_auto", s.Settings.ExposureAuto);
        w.OptionalInt("exposure_step", s.Settings.ExposureStep);
        if (s.Settings.ExposureStep is int exposureStep)
        {
            w.Fixed("exposure_ms", CameraSettings.ExposureStepMs(exposureStep), 3);
        }
        else
        {
            w.Null("exposure_ms");
        }
        w.OptionalInt("gain", s.Settings.Gain);
        w.OptionalBool("white_balance_auto", s.Settings.WhiteBalanceAuto);
        w.OptionalInt("white_balance_k", s.Settings.WhiteBalanceK);
        w.OptionalInt("sdk_frame_rate", s.Settings.SdkFrameRate);
        w.OptionalBool("locked", s.Settings.Locked);
        w.Close('}');

        w.Open("clock", '{');
        w.Int("offset_us", s.Clock.OffsetUs);
        // 속도 차이는 수백 ppm이라 소수점 아래 9자리(0.001ppm)까지 남깁니다.
        w.Fixed("rate", s.Clock.Rate, 9);
        w.Int("max_residual_us", s.Clock.MaxResidualUs);
        w.Close('}');

        if (s.DisplayRange is Y16Range range)
        {
            w.Open("display_range", '{');
            w.Int("low", range.Low);
            w.Int("high", range.High);
            w.Close('}');
        }
        else
        {
            w.Null("display_range");
        }

        w.Open("frames", '{');
        w.Int("target", s.TargetFrames);
        w.Int("written", s.WrittenFrames);
        w.Int("received", s.ReceivedFrames);
        w.Int("record_period_us", s.RecordPeriodUs);
        w.Int("written_in_rgb_window", s.WrittenInRgbWindow);
        w.Int("first_time_us", s.FirstFrame.Us);
        w.Int("last_time_us", s.LastFrame.Us);
        w.Int("median_interval_us", s.MedianIntervalUs);
        w.Int("missing", TimingFunctions.CountMissingFrames(s.Gaps));
        w.Int("dropped", s.DroppedFrames);
        w.Int("buffer_frames", s.BufferFrames);
        w.Int("max_queue_frames", s.MaxQueueFrames);
        w.Int("file_bytes", s.FileBytes);
        w.Open("gaps", '[');
        foreach (FrameGap gap in s.Gaps)
        {
            w.Open(null, '{');
            w.Int("after_frame", gap.Before);
            w.Int("interval_us", gap.IntervalUs);
            w.Int("missing", gap.MissingFrames);
            w.Close('}');
        }
        w.Close(']');
        w.Int("slow_intervals", s.FrameRate.SlowIntervals);
        w.Fixed("min_fps_1s", s.FrameRate.MinFps, 1);
        w.Open("slow_spans", '[');
        foreach (SlowSpan span in s.FrameRate.Spans)
        {
            w.Open(null, '{');
            w.Int("start_us", span.StartUs);
            w.Int("end_us", span.EndUs);
            w.Int("intervals", span.Intervals);
            w.Close('}');
        }
        w.Close(']');
        w.Close('}');

        // 값마다 배열 하나입니다. 평균 밝기는 brightness_sum / brightness_pixels입니다.
        IReadOnlyList<FrameRecord> r = s.Records;
        w.Open("per_frame", '{');
        w.Int("brightness_pixels", s.BrightnessPixels);
        w.Int("clip_level", s.ClipLevel);
        w.IntArray("index", r.Count, i => (long)r[i].Index.Value);
        w.IntArray("device_time_us", r.Count, i => r[i].DeviceTime.Us);
        w.IntArray("host_time_us", r.Count, i => r[i].HostTime.Us);
        w.IntArray("common_time_us", r.Count, i => TimingFunctions.ToCommonTime(s.Clock, r[i].DeviceTime).Us);
        w.IntArray("brightness_sum", r.Count, i => r[i].BrightnessSum);
        w.IntArray("clipped_pixels", r.Count, i => r[i].ClippedPixels);
        w.Close('}');

        w.Close('}');
        return w.Take();
    }

    /// <summary>
    /// PPG 사이드카 JSON을 만듭니다.
    /// </summary>
    /// <param name="sidecar">기록</param>
    /// <returns>JSON 문자열. 같은 입력에는 같은 바이트를 내며 키 순서는 고정입니다. 파일에 쓸 때는 BOM 없는 UTF-8로 인코딩합니다</returns>
    /// <remarks>
    /// 시계 구간마다 식(last_device_us, offset_us, rate, max_residual_us)을 남겨 CSV의 공통 시각을 다시 계산하고 검사할 수 있게 합니다.
    /// 측정 오류 구간(measure_errors.spans)은 공통 시각과 함께, RGB 녹화 구간을 알면 RGB 첫 장을 0초로 잰 초(rgb_begin_s,
    /// rgb_end_s)로도 씁니다. 결과 창의 가로축과 같은 값입니다.
    /// </remarks>
    public static string BuildPpgSidecarJson(PpgSidecar sidecar)
    {
        PpgSidecar s = sidecar;
        var w = new JsonWriter();
        w.Open(null, '{');
        w.Int("schema_version", PpgSchemaVersion);
        w.String("samples_file", s.SamplesFile);
        w.String("port", s.Port);
        if (s.DeviceInfo is string deviceInfo)
        {
            w.String("device_info", deviceInfo);
        }
        else
        {
            w.Null("device_info");
        }
        w.Int("sample_rate_hz", s.SampleRateHz);

        w.Open("record", '{');
        w.Int("begin_us", s.RecordBegin.Us);
        w.Int("end_us", s.RecordEnd.Us);
        w.Close('}');

        w.Open("clock", '[');
        foreach (ClockSegment c in s.Clock)
        {
            w.Open(null, '{');
            w.Int("last_device_us", c.LastDevice.Us);
            w.Int("offset_us", c.Map.OffsetUs);
            w.Fixed("rate", c.Map.Rate, 9);
            w.Int("max_residual_us", c.Map.MaxResidualUs);
            w.Close('}');
        }
        w.Close(']');

        w.Open("samples", '{');
        w.Int("count", s.Samples);
        w.Int("messages", s.Messages);
        w.Int("first_time_us", s.FirstSample.Us);
        w.Int("last_time_us", s.LastSample.Us);
        w.Int("median_interval_us", s.MedianIntervalUs);
        w.Int("missing", TimingFunctions.CountMissingFrames(s.Gaps));
        w.Int("measure_error", s.MeasureErrorSamples);
        w.Open("gaps", '[');
        foreach (FrameGap gap in s.Gaps)
        {
            w.Open(null, '{');
            w.Int("after_sample", gap.Before);
            w.Int("interval_us", gap.IntervalUs);
            w.Int("missing", gap.MissingFrames);
            w.Close('}');
        }
        w.Close(']');
        w.Close('}');

        w.Open("measure_errors", '{');
        WriteErrorSpans(w, s.ErrorSpans, s.RgbWindow);
        if (s.RgbWindow is PpgErrorWindow window)
        {
            w.Open("rgb_window", '{');
            w.Int("begin_us", window.Begin.Us);
            w.Int("end_us", window.End.Us);
            w.Int("samples", window.Samples);
            w.Open("codes", '[');
            foreach (PpgErrorShare share in window.Codes)
            {
                w.Open(null, '{');
                w.String("code", CodeText(share.Code));
                w.Int("samples", share.Samples);
                w.Int("longest_samples", share.LongestSamples);
                w.Int("runs", share.Runs);
                w.Close('}');
            }
            w.Close(']');
            w.Close('}');
        }
        else
        {
            w.Null("rgb_window");
        }
        w.Close('}');

        w.Open("link", '{');
        w.Int("crc_failures", s.CrcFailures);
        w.Int("bad_lengths", s.BadLengths);
        w.Int("implausible", s.Implausible);
        w.Int("skipped_bytes", s.SkippedBytes);
        w.Close('}');

        w.Open("events", '[');
        foreach (PpgEvent e in s.Events)
        {
            w.Open(null, '{');
            w.Int("time_us", e.HostTime.Us);
            w.String("code", CodeText(e.Code));
            w.String("value", CodeText(e.Value));
            w.Close('}');
        }
        w.Close(']');

        w.Close('}');
        return w.Take();
    }

    private static string NameOf(PixelFormat format) => format switch
    {
        PixelFormat.Uyvy => "UYVY",
        PixelFormat.Nv12 => "NV12",
        PixelFormat.Y16 => "Y16",
        _ => "",
    };

    private static string NameOf(LightSource source) => source switch
    {
        LightSource.Unknown => "unknown",
        LightSource.External => "external",
        LightSource.App => "app",
        _ => "",
    };

    private static string NameOf(LensShadingSource source) => source switch
    {
        LensShadingSource.None => "none",
        LensShadingSource.Loaded => "loaded",
        LensShadingSource.Corrected => "corrected",
        LensShadingSource.Failed => "failed",
        _ => "",
    };

    private static string NameOf(LightResponse response) => response switch
    {
        LightResponse.Unknown => "unknown",
        LightResponse.Applied => "applied",
        LightResponse.NotSeen => "not_seen",
        _ => "",
    };

    private static string NameOf(StreamKind stream) => stream switch
    {
        StreamKind.Rgb => "rgb",
        StreamKind.Thermal => "thermal",
        _ => "",
    };

    // 밝기 값(화소 하나의 Y)은 소수점 아래 4자리, 비율과 주기도 4자리로 고정합니다.
    private static void WriteLight(JsonWriter w, LightRecord light)
    {
        w.Open("light", '{');
        w.String("source", NameOf(light.Source));
        w.OptionalInt("percent", light.Percent);
        if (light.UsbVendorId is ushort vendor && light.UsbProductId is ushort product)
        {
            w.String("port_vid_pid", CFormat.HexUpper(vendor, 4) + ":" + CFormat.HexUpper(product, 4));
        }
        else
        {
            w.Null("port_vid_pid");
        }
        if (light.Serial.Length == 0)
        {
            w.Null("serial");
        }
        else
        {
            w.String("serial", light.Serial);
        }
        w.OptionalBool("confirmed", light.Confirmed);
        w.String("response", NameOf(light.Response));
        if (light.Flicker is Flicker f)
        {
            w.Open("flicker", '{');
            w.Int("frames", f.FrameCount);
            w.Fixed("mean", f.Mean, 4);
            w.Fixed("stddev", f.Stddev, 4);
            w.Fixed("peak_to_peak", f.PeakToPeak, 4);
            w.Fixed("periodic_fraction", f.PeriodicFraction, 4);
            if (f.DominantPeriodFrames is double period)
            {
                w.Fixed("dominant_period_frames", period, 4);
            }
            else
            {
                w.Null("dominant_period_frames");
            }
            w.Close('}');
        }
        else
        {
            w.Null("flicker");
        }
        w.Close('}');
    }

    // 장치가 보낸 바이트 하나입니다. 인쇄할 수 없는 바이트를 그대로 쓰면 UTF-8이 아닌 JSON이 되므로 16진수로 씁니다.
    private static string CodeText(byte code) =>
        code >= 0x20 && code < 0x7f ? ((char)code).ToString() : "0x" + CFormat.HexLower(code, 2);

    private static void WriteErrorSpans(JsonWriter w, IReadOnlyList<PpgErrorSpan> spans, PpgErrorWindow? window)
    {
        w.Open("spans", '[');
        foreach (PpgErrorSpan span in spans)
        {
            w.Open(null, '{');
            w.String("code", CodeText(span.Code));
            w.Int("begin_us", span.Begin.Us);
            w.Int("end_us", span.End.Us);
            w.Int("samples", span.Samples);
            if (window is not null)
            {
                w.Fixed("rgb_begin_s", Timestamp.IntervalUs(window.Begin, span.Begin) / 1e6, 3);
                w.Fixed("rgb_end_s", Timestamp.IntervalUs(window.Begin, span.End) / 1e6, 3);
            }
            else
            {
                w.Null("rgb_begin_s");
                w.Null("rgb_end_s");
            }
            w.Close('}');
        }
        w.Close(']');
    }
}
