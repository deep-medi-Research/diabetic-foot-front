using System.Globalization;
using System.Text;
using DiabeticFoot.Core;
using DiabeticFoot.Core.Record;
using DiabeticFoot.Core.Timing;

namespace DiabeticFoot.Platform;

/// <summary><see cref="PpgFunctions.ReadPpgCsv"/>의 결과입니다. 열마다 같은 길이입니다.</summary>
public sealed record PpgCsv
{
    public IReadOnlyList<long> CommonTimeUs { get; init; } = Array.Empty<long>();

    public IReadOnlyList<int> Value { get; init; } = Array.Empty<int>();

    public IReadOnlyList<int> MeasureError { get; init; } = Array.Empty<int>();

    public IReadOnlyList<int> HeartRate { get; init; } = Array.Empty<int>();

    public IReadOnlyList<int> Spo2 { get; init; } = Array.Empty<int>();
}

/// <summary>원본 ppg_sensor.h의 상수와 자유 함수입니다.</summary>
public static class PpgFunctions
{
    /// <summary>장치를 확인하기 전 정보 요청을 보내는 간격입니다. 기존 앱과 같습니다.</summary>
    public const long PpgInfoIntervalUs = 1_000_000;

    /// <summary>
    /// 포트를 연 뒤 이 시간 동안 장치가 답하지 않으면 준비 중(<see cref="PpgState.Preparing"/>)으로 보입니다. 기존 앱과 같습니다.
    /// 기존 앱 주석의 실기 기록으로 준비된 장치는 정보 요청에 수십 ms 안에 답합니다.
    /// </summary>
    public const long PpgPreparingAfterUs = 1_500_000;

    /// <summary>
    /// 포트를 연 뒤 이 시간 동안 장치가 답하지 않으면 PPG 장치를 찾지 못한 것으로 보고 포트를 닫습니다. 기존 앱과 같습니다.
    /// 전원을 켠 장치는 초기화를 끝내야 답합니다. 실기(2026-10-19)에서 약 42초, 기존 앱 주석으로 35~60초입니다.
    /// </summary>
    public const long PpgFindTimeoutUs = 90_000_000;

    /// <summary>화면 파형의 점 하나에 묶는 표본 수입니다. 500Hz 표본 5개를 평균해 100Hz 점 하나로 그립니다.</summary>
    public const int PpgWaveGroup = 5;

    /// <summary>화면 파형에 쓰는 최근 표본 수입니다(500Hz로 5초).</summary>
    public const int PpgWaveSamples = 2500;

    /// <summary>
    /// 촬영 버튼에서 PPG 장치를 다시 시작한 뒤 RGB 녹화를 미루는 시간입니다(PPG 안정 대기).
    /// </summary>
    /// <remarks>
    /// 다시 시작하면 원시값이 오른 뒤 몇 초 안에 한두 번 6,000~7,000씩 떨어지고, 그동안 장치 심박수에도 틀린 값이 섞입니다.
    /// 계단이 생기지 않는 촬영도 있어 장치가 안정됐는지는 판정하지 않고 이 시간을 늘 기다립니다. 실기(2026-10-21~22, 한 사람,
    /// 장치 하나): 다시 시작한 37번 모두 마지막 계단이 9.6초 안에 있었고, 계단이 생긴 19번의 마지막 계단은 평균 7.4초
    /// (3.0~9.6초)였습니다. 다른 사람과 손가락 위치는 재지 않아 넉넉히 15초로 정했습니다. 열화상을 쓰면 셔터 교정이 이 대기와 함께
    /// 끝나게 요청해 같은 시각에 녹화를 시작합니다. 교정이 길어지면 그만큼 늦게 시작합니다(설계 문서 표 10).
    /// </remarks>
    public const long PpgSettleUs = 15_000_000;

    /// <summary>측정 데이터가 이 시간 넘게 오지 않으면 다시 시작할 때가 됩니다(<see cref="PpgNeedsRestart"/>).</summary>
    public const long PpgRestartQuietUs = 2_000_000;

    /// <summary>다시 시작한 뒤 이 시간이 지나기 전에는 다시 시작하지 않습니다. 전원을 켠 뒤 준비 중인 장치에 시작 명령을 쌓지 않기 위함입니다.</summary>
    public const long PpgRestartIntervalUs = 5_000_000;

    /// <summary>촬영 폴더에 쓰는 PPG 파일 이름의 뒷부분입니다. 파일 이름은 <c>&lt;촬영 이름&gt;_ppg.csv</c>입니다.</summary>
    public const string PpgFileSuffix = "_ppg.csv";

    /// <summary>촬영 폴더에 쓰는 PPG 사이드카 이름의 뒷부분입니다. 파일 이름은 <c>&lt;촬영 이름&gt;_ppg.json</c>입니다.</summary>
    public const string PpgSidecarSuffix = "_ppg.json";

    const long SampleUs = 1_000_000 / PpgWire.SampleHz;
    const string PpgCsvHeader = "common_time_us,value,measure_error,heart_rate,spo2";
    static readonly UTF8Encoding Utf8 = new(false);

    /// <summary>
    /// 화면 파형의 점을 만듭니다. 표본을 <see cref="PpgWaveGroup"/>개씩 평균하고 1~99% 범위를 0~1로 옮깁니다.
    /// 손가락을 넣고 뺄 때 튀는 값 몇 개가 파형 높이를 정하지 않게 하기 위해 최솟값과 최댓값 대신 1%와 99% 값을 씁니다.
    /// </summary>
    /// <param name="values">오래된 표본부터입니다.</param>
    /// <returns>오래된 점부터 0~1 값입니다. 점이 둘이 안 되면 빈 목록입니다.</returns>
    public static IReadOnlyList<double> PpgWavePoints(IReadOnlyList<ushort> values)
    {
        int groups = values.Count / PpgWaveGroup;
        if (groups < 2) return Array.Empty<double>();
        int skip = values.Count - groups * PpgWaveGroup;
        var average = new double[groups];
        for (int g = 0; g < groups; ++g)
        {
            double sum = 0;
            for (int k = 0; k < PpgWaveGroup; ++k) sum += values[skip + g * PpgWaveGroup + k];
            average[g] = sum / PpgWaveGroup;
        }
        double[] sorted = (double[])average.Clone();
        Array.Sort(sorted);
        double low = sorted[(groups - 1) / 100];
        double high = sorted[(groups - 1) * 99 / 100];
        double span = Math.Max(high - low, 1.0);
        var points = new double[groups];
        for (int g = 0; g < groups; ++g) points[g] = Math.Clamp((average[g] - low) / span, 0.0, 1.0);
        return points;
    }

    /// <summary>
    /// 촬영하지 않는 동안 PPG 장치를 다시 시작(중지 뒤 시작)해야 하는지 정합니다.
    /// 장치가 측정 구간에 들어갔거나(상태 's') 측정을 끝냈거나(상태 'p'), 측정 데이터가 <see cref="PpgRestartQuietUs"/> 넘게 오지
    /// 않으면 다시 시작합니다. 장치를 측정 구간에 두지 않고 미리보기를 이어가게 해서 화면 파형이 끊기지 않게 하기 위함입니다.
    /// </summary>
    /// <param name="sinceRestartUs">마지막으로 다시 시작한 뒤 흐른 시간입니다.</param>
    /// <returns>다시 시작해야 하면 true입니다. 포트가 닫혔거나 sinceRestartUs가 <see cref="PpgRestartIntervalUs"/>보다 짧으면 false입니다.</returns>
    /// <remarks>
    /// 손가락 없이 장치 미리보기가 끝나면 장치는 상태 's'나 'p' 없이 오류 메시지 'f'를 보내고 측정 데이터를 멈춥니다. 시작 명령을
    /// 다시 받기 전까지 그대로 멈춰 있고, 받으면 바로 미리보기로 돌아갑니다(2026-10-21 실측, 미리보기 10초로 시작해 80초 동안
    /// 기다림). 측정 구간에서 손가락이 빠지면 오류 'm'으로 측정을 끝낸다고 사양서가 적습니다.
    /// </remarks>
    public static bool PpgNeedsRestart(PpgReading reading, long sinceRestartUs)
    {
        if (!reading.Open || sinceRestartUs < PpgRestartIntervalUs) return false;
        return reading.Status == PpgStatus.Measuring || reading.Status == PpgStatus.Finished ||
               reading.QuietUs > PpgRestartQuietUs;
    }

    /// <summary>
    /// PPG 기록을 CSV로 씁니다. 표본이 없어도 열 이름 줄만 있는 파일을 씁니다.
    /// </summary>
    /// <remarks>
    /// 첫 줄은 열 이름 <c>common_time_us,value,measure_error,heart_rate,spo2</c>이고, 그 뒤 표본마다 한 줄입니다.
    /// common_time_us는 공통 시각(PC 시계, 마이크로초)이라 RGB 사이드카의 common_time_us와 바로 견줍니다.
    /// measure_error는 0 정상, 102('f') 손가락 빠짐, 115('s') 신호 나쁨, 112('p') 센서 연결 안 됨입니다.
    /// </remarks>
    /// <param name="record"><see cref="PpgShotRecord.Finish"/>의 결과입니다.</param>
    public static bool WritePpgCsv(string path, PpgRecord record, out string error)
    {
        var text = new StringBuilder(PpgCsvHeader).Append('\n');
        for (int i = 0; i < record.Values.Count; ++i)
        {
            text.Append(record.CommonTime[i].Us.ToString(CultureInfo.InvariantCulture)).Append(',')
                .Append(record.Values[i].ToString(CultureInfo.InvariantCulture)).Append(',')
                .Append(record.MeasureError[i].ToString(CultureInfo.InvariantCulture)).Append(',')
                .Append(record.HeartRate[i].ToString(CultureInfo.InvariantCulture)).Append(',')
                .Append(record.Spo2[i].ToString(CultureInfo.InvariantCulture)).Append('\n');
        }
        return WriteText(path, text.ToString(), "PPG 파일을 만들지 못했습니다: ", "PPG 파일을 다 쓰지 못했습니다: ", out error);
    }

    /// <summary>
    /// <see cref="WritePpgCsv"/>가 쓴 파일을 읽습니다. 파일이 없거나 열 이름 줄이 다르거나 숫자가 아닌 줄이 있으면 null이고 error에
    /// 이유를 씁니다.
    /// </summary>
    public static PpgCsv? ReadPpgCsv(string path, out string error)
    {
        string text;
        try
        {
            text = Encoding.UTF8.GetString(File.ReadAllBytes(path));
        }
        catch (Exception e) when (IsFileError(e))
        {
            error = "PPG 파일이 없습니다: " + path;
            return null;
        }
        var commonTime = new List<long>();
        var value = new List<int>();
        var measureError = new List<int>();
        var heartRate = new List<int>();
        var spo2 = new List<int>();
        bool header = true;
        foreach (string raw in text.Split('\n'))
        {
            string line = raw.EndsWith('\r') ? raw[..^1] : raw;
            if (line.Length == 0) continue;
            if (header)
            {
                if (line != PpgCsvHeader)
                {
                    error = "PPG 파일의 열 이름이 다릅니다: " + line;
                    return null;
                }
                header = false;
                continue;
            }
            if (!ScanCsvRow(line, out long t, out uint v, out uint e, out uint h, out uint o))
            {
                error = "PPG 파일에 읽을 수 없는 줄: " + line;
                return null;
            }
            commonTime.Add(t);
            value.Add((int)v);
            measureError.Add((int)e);
            heartRate.Add((int)h);
            spo2.Add((int)o);
        }
        if (header)
        {
            error = "PPG 파일에 열 이름 줄이 없습니다: " + path;
            return null;
        }
        error = "";
        return new PpgCsv { CommonTimeUs = commonTime, Value = value, MeasureError = measureError, HeartRate = heartRate, Spo2 = spo2 };
    }

    /// <summary>
    /// PPG 기록의 사이드카를 씁니다(<c>RecordFunctions.BuildPpgSidecarJson</c>).
    /// </summary>
    /// <remarks>
    /// 누락은 공통 시각의 표본 간격이 500Hz 주기의 1.5배를 넘는 자리이고(<c>FindFrameGaps</c>), 수신 빈틈이 여기에 남습니다.
    /// 측정 오류 구간은 기록 전체에서 찾습니다(<c>FindPpgErrorSpans</c>). 장치 정보를 받지 못했으면 device_info는 null입니다.
    /// </remarks>
    /// <param name="record"><see cref="PpgShotRecord.Finish"/>의 결과입니다.</param>
    /// <param name="samplesFile">같은 기록을 쓴 PPG 파일 이름(폴더 없이)입니다.</param>
    /// <param name="port">장치를 연 포트 이름입니다.</param>
    /// <param name="rgbWindow">RGB 녹화 구간 안 측정 오류 집계(<c>SummarizePpgErrors</c>)입니다. RGB 사이드카를 읽지 못했으면 null입니다.</param>
    public static bool WritePpgSidecar(
        string path, PpgRecord record, string samplesFile, string port, PpgErrorWindow? rgbWindow, out string error)
    {
        long measureErrorSamples = 0;
        foreach (byte e in record.MeasureError) measureErrorSamples += e != 0 ? 1 : 0;
        var sidecar = new PpgSidecar
        {
            SamplesFile = samplesFile,
            Port = port,
            DeviceInfo = record.DeviceInfo.Length != 0 ? record.DeviceInfo : null,
            SampleRateHz = PpgWire.SampleHz,
            RecordBegin = record.BeginTime,
            RecordEnd = record.EndTime,
            Clock = record.Clock,
            Samples = record.Values.Count,
            Messages = record.Anchors.Count,
            FirstSample = record.CommonTime.Count > 0 ? record.CommonTime[0] : default,
            LastSample = record.CommonTime.Count > 0 ? record.CommonTime[^1] : default,
            MedianIntervalUs = TimingFunctions.MedianIntervalUs(record.CommonTime),
            Gaps = TimingFunctions.FindFrameGaps(record.CommonTime, SampleUs),
            MeasureErrorSamples = measureErrorSamples,
            CrcFailures = record.Counts.CrcFailures,
            BadLengths = record.Counts.BadLengths,
            Implausible = record.Counts.Implausible,
            SkippedBytes = record.Counts.SkippedBytes,
            Events = record.Events,
            ErrorSpans = RecordFunctions.FindPpgErrorSpans(record.CommonTime, record.MeasureError),
            RgbWindow = rgbWindow,
        };
        return WriteText(
            path, RecordFunctions.BuildPpgSidecarJson(sidecar),
            "PPG 사이드카를 만들지 못했습니다: ", "PPG 사이드카를 다 쓰지 못했습니다: ", out error);
    }

    /// <summary>
    /// RGB 녹화 구간 안 측정 오류 값 하나의 양을 화면에 보일 문장으로 바꿉니다.
    /// </summary>
    /// <param name="share">값 하나의 집계(<c>SummarizePpgErrors</c>)입니다.</param>
    /// <param name="windowSamples">같은 구간 안 표본 수입니다. 비율의 분모입니다.</param>
    /// <returns>예: "신호 나쁨 4.3초(14%)". 여러 번 끊겨 나왔으면 횟수를 붙입니다: "손가락 빠짐 0.5초(2%, 3번)"</returns>
    public static string PpgErrorShareText(PpgErrorShare share, long windowSamples)
    {
        double seconds = (double)share.Samples / PpgWire.SampleHz;
        double percent = windowSamples > 0 ? 100.0 * share.Samples / windowSamples : 0;
        // 1% 아래를 반올림해 0%로 쓰면 오류가 없었던 것처럼 읽히므로 소수점 아래 한 자리까지 씁니다.
        string amount = CFormat.Fixed(seconds, 1) + "초(" + CFormat.Fixed(percent, percent < 1 ? 1 : 0) + "%";
        string text = PpgWire.MeasureErrorText(share.Code) + " " + amount;
        if (share.Runs > 1) text += ", " + share.Runs.ToString(CultureInfo.InvariantCulture) + "번";
        return text + ")";
    }

    static bool WriteText(string path, string text, string openFailure, string writeFailure, out string error)
    {
        FileStream file;
        try
        {
            file = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.Read);
        }
        catch (Exception e) when (IsFileError(e))
        {
            error = openFailure + path;
            return false;
        }
        try
        {
            using (file) file.Write(Utf8.GetBytes(text));
        }
        catch (IOException)
        {
            error = writeFailure + path;
            return false;
        }
        error = "";
        return true;
    }

    static bool IsFileError(Exception e) => e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException;

    // 원본 sscanf("%lld,%u,%u,%u,%u")와 같게 숫자 앞 공백과 부호를 받고, 다섯 번째 값 뒤의 글자는 보지 않습니다.
    static bool ScanCsvRow(string line, out long t, out uint v, out uint e, out uint h, out uint o)
    {
        int pos = 0;
        v = e = h = o = 0;
        bool ok = ScanInteger(line, ref pos, out t)
            && Comma(line, ref pos) && ScanUnsigned(line, ref pos, out v)
            && Comma(line, ref pos) && ScanUnsigned(line, ref pos, out e)
            && Comma(line, ref pos) && ScanUnsigned(line, ref pos, out h)
            && Comma(line, ref pos) && ScanUnsigned(line, ref pos, out o);
        return ok;
    }

    static bool Comma(string line, ref int pos)
    {
        if (pos >= line.Length || line[pos] != ',') return false;
        ++pos;
        return true;
    }

    static bool ScanInteger(string line, ref int pos, out long value)
    {
        value = 0;
        while (pos < line.Length && char.IsWhiteSpace(line[pos])) ++pos;
        int start = pos;
        if (pos < line.Length && (line[pos] == '+' || line[pos] == '-')) ++pos;
        int digits = pos;
        while (pos < line.Length && char.IsAsciiDigit(line[pos])) ++pos;
        return pos > digits && long.TryParse(line.AsSpan(start, pos - start), NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out value);
    }

    // %u는 음수도 받아 부호 없는 값으로 감습니다.
    static bool ScanUnsigned(string line, ref int pos, out uint value)
    {
        bool ok = ScanInteger(line, ref pos, out long signed);
        value = unchecked((uint)signed);
        return ok;
    }
}
