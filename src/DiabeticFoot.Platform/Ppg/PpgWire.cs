using System.Diagnostics.CodeAnalysis;

namespace DiabeticFoot.Platform;

/// <summary>메시지 발신지입니다.</summary>
public static class PpgSource
{
    public const byte Pc = 0x55; // 'U'
    public const byte Device = 0x4D; // 'M'
}

/// <summary>장치가 보내는 메시지 코드입니다.</summary>
public static class PpgCode
{
    /// <summary>파형 표본, 심박수, SpO2, 측정 오류입니다.</summary>
    public const byte Measure = (byte)'m';
    public const byte Result = (byte)'r';
    public const byte Status = (byte)'s';
    public const byte Error = (byte)'e';
    public const byte Info = (byte)'i';
    public const byte Time = (byte)'t';
}

/// <summary>상태 메시지의 값입니다.</summary>
public static class PpgStatus
{
    public const byte Boot = (byte)'o';
    public const byte Preview = (byte)'v';
    public const byte Measuring = (byte)'s';
    public const byte Finished = (byte)'p';
    public const byte Stopped = (byte)'u';
}

/// <summary>
/// PPG 장치와 주고받는 메시지 하나입니다.
/// 형식은 FF, 발신지(PC 0x55, 장치 0x4D), 길이 2바이트(큰 쪽 먼저, 코드부터 CRC까지의 바이트 수), 코드 1바이트, 데이터,
/// CRC16 2바이트(작은 쪽 먼저)입니다. 전체 크기는 4 + 길이입니다.
/// </summary>
/// <remarks>
/// 장치 사양서(Interface Basic Rev.06, 2025-07-31)와 실기(2026-10-19~20)로 확인한 형식입니다.
/// </remarks>
/// <param name="Source">발신지입니다(<see cref="PpgSource"/>).</param>
/// <param name="Code">메시지 코드입니다.</param>
/// <param name="Data">코드와 CRC를 뺀 데이터입니다.</param>
/// <param name="HostTimeUs">메시지의 마지막 바이트가 들어 있던 수신 덩어리의 PC 시각(마이크로초)입니다.</param>
public sealed record PpgMessage(byte Source, byte Code, byte[] Data, long HostTimeUs);

/// <summary>측정 시작 명령에 넣는 값입니다.</summary>
public sealed record PpgStartRequest
{
    /// <summary>미리보기 길이(초)입니다. 장치는 10~60을 받습니다. 파형은 미리보기 동안에도 옵니다.</summary>
    public int PreviewS { get; init; } = 60;

    /// <summary>측정 길이(초)입니다. 장치는 60 또는 150만 받습니다.</summary>
    public int MeasureS { get; init; } = 60;

    /// <summary>센서 종류입니다. 'P' 집게형, 'F' 손가락형이며 실험 장치는 'P'입니다.</summary>
    public char Sensor { get; init; } = 'P';

    /// <summary>성별 'M' 또는 'F'입니다. 장치의 결과 계산에만 쓰이고 파형에는 영향이 없습니다.</summary>
    public char Gender { get; init; } = 'M';

    public int Age { get; init; } = 40;

    /// <summary>결과 기준 'A' 동양인, 'W' 서양인입니다.</summary>
    public char Reference { get; init; } = 'A';
}

/// <summary>측정 데이터 메시지('m') 하나를 푼 값입니다.</summary>
/// <param name="Samples">파형 표본(부호 없는 16비트)입니다. 500Hz 순서대로입니다.</param>
/// <param name="HeartRate">장치가 계산한 심박수(bpm)입니다. 계산 전이면 0입니다.</param>
/// <param name="Spo2">장치가 계산한 SpO2(%)입니다. 계산 전이면 0입니다.</param>
/// <param name="Error">측정 오류입니다. 0 정상, 'f' 손가락 빠짐, 's' 신호 나쁨, 'p' 센서 연결 안 됨입니다.</param>
public sealed record PpgMeasure(ushort[] Samples, int HeartRate, int Spo2, byte Error);

/// <summary>PPG 장치로 보내는 명령을 만들고 측정 데이터를 푸는 순수 함수입니다.</summary>
public static class PpgWire
{
    /// <summary>
    /// 장치의 파형 표본 주기(Hz)입니다.
    /// 장치 표본 시계는 PC보다 약 0.5% 느립니다(2026-10-19 실측, 30초에 약 0.15초). 표본 번호만으로 시각을 매기지
    /// 않고 PC 수신 시각으로 맞춥니다.
    /// </summary>
    public const int SampleHz = 500;

    const byte Header = 0xFF;

    /// <summary>
    /// CRC16을 계산합니다. 초기값 0xFFFF, 다항식 0xA001(반사)입니다.
    /// 메시지에는 작은 쪽 바이트를 먼저 씁니다. 사양서 예제 <c>FF 55 00 03 49</c>의 CRC 바이트는 <c>E0 DE</c>입니다.
    /// </summary>
    /// <param name="bytes">머리(FF)부터 데이터 끝까지입니다.</param>
    public static ushort Crc16(ReadOnlySpan<byte> bytes)
    {
        ushort crc = 0xFFFF;
        foreach (byte b in bytes)
        {
            crc ^= b;
            for (int i = 0; i < 8; ++i) crc = (crc & 1) != 0 ? (ushort)((crc >> 1) ^ 0xA001) : (ushort)(crc >> 1);
        }
        return crc;
    }

    /// <summary>PC가 보내는 메시지를 머리부터 CRC까지 만듭니다.</summary>
    public static byte[] BuildCommand(byte code, ReadOnlySpan<byte> data = default) =>
        BuildMessage(PpgSource.Pc, code, data);

    /// <summary>발신지를 정해 메시지를 만듭니다. 장치 쪽 흉내(FakePpgDevice)가 장치 메시지를 만들 때 씁니다.</summary>
    internal static byte[] BuildMessage(byte source, byte code, ReadOnlySpan<byte> data)
    {
        int length = 1 + data.Length + 2;
        var message = new byte[4 + length];
        message[0] = Header;
        message[1] = source;
        message[2] = (byte)(length >> 8);
        message[3] = (byte)(length & 0xFF);
        message[4] = code;
        data.CopyTo(message.AsSpan(5));
        ushort crc = Crc16(message.AsSpan(0, message.Length - 2));
        // 사양서 예제 코드의 uchCRCHi가 먼저 나가며, 이 값이 16비트 CRC의 하위 바이트입니다.
        message[^2] = (byte)(crc & 0xFF);
        message[^1] = (byte)(crc >> 8);
        return message;
    }

    /// <summary>
    /// 측정 시작 명령을 만듭니다. 값이 장치가 받는 범위를 벗어나면 false이고 error에 이유를 씁니다.
    /// </summary>
    public static bool TryBuildStart(
        PpgStartRequest request,
        [NotNullWhen(true)] out byte[]? command,
        [NotNullWhen(false)] out string? error)
    {
        command = null;
        error = null;
        if (request.PreviewS is < 10 or > 60) error = "미리보기 길이는 10~60초입니다";
        else if (request.MeasureS is not (60 or 150)) error = "측정 길이는 60초 또는 150초입니다";
        else if (request.Sensor is not ('P' or 'F')) error = "센서 종류는 P 또는 F입니다";
        else if (request.Gender is not ('M' or 'F')) error = "성별은 M 또는 F입니다";
        else if (request.Age is < 1 or > 120) error = "나이는 1~120입니다";
        else if (request.Reference is not ('A' or 'W')) error = "결과 기준은 A 또는 W입니다";
        if (error is not null) return false;

        ReadOnlySpan<byte> data =
        [
            (byte)request.PreviewS, (byte)request.MeasureS, (byte)request.Sensor,
            (byte)request.Gender, (byte)request.Age, (byte)request.Reference,
        ];
        command = BuildCommand((byte)'D', data);
        return true;
    }

    /// <summary>측정 중지 명령입니다. 장치는 상태 'u'로 답합니다(실측 32ms).</summary>
    public static byte[] BuildStop() => BuildCommand((byte)'P');

    /// <summary>장치 정보 요청 명령입니다. 장치는 'i' 메시지(ASCII 15바이트)로 답합니다.</summary>
    public static byte[] BuildInfoRequest() => BuildCommand((byte)'I');

    /// <summary>
    /// 측정 데이터 메시지의 데이터를 풉니다. 형식은 16비트 표본 N개(큰 쪽 먼저), 심박수, SpO2, 측정 오류 각 1바이트입니다.
    /// </summary>
    /// <param name="data"><see cref="PpgMessage.Data"/>입니다.</param>
    /// <returns>푼 값입니다. 끝 3바이트를 뺀 길이가 홀수이거나 3바이트보다 짧으면 null입니다.</returns>
    public static PpgMeasure? DecodeMeasure(ReadOnlySpan<byte> data)
    {
        if (data.Length < 3) return null;
        int sampleBytes = data.Length - 3;
        if (sampleBytes % 2 != 0) return null;
        var samples = new ushort[sampleBytes / 2];
        for (int i = 0; i < samples.Length; ++i) samples[i] = (ushort)((data[2 * i] << 8) | data[2 * i + 1]);
        return new PpgMeasure(samples, data[sampleBytes], data[sampleBytes + 1], data[sampleBytes + 2]);
    }

    /// <summary>측정 오류 값을 화면에 보일 문장으로 바꿉니다. 0이면 빈 문자열입니다.</summary>
    public static string MeasureErrorText(byte error) => error switch
    {
        0 => "",
        (byte)'f' => "손가락 빠짐",
        (byte)'s' => "신호 나쁨",
        (byte)'p' => "센서 연결 안 됨",
        _ => "알 수 없는 측정 오류",
    };
}
