namespace DiabeticFoot.Platform;

/// <summary>PpgParser가 버린 바이트와 메시지 수입니다. 통신 상태를 기록에 남기는 데 씁니다.</summary>
/// <param name="CrcFailures">CRC가 맞지 않아 버린 시작 후보 수입니다.</param>
/// <param name="BadLengths">길이가 범위를 벗어나 버린 시작 후보 수입니다.</param>
/// <param name="Implausible">사양서에 없는 코드이거나 코드에 맞지 않는 길이라 버린 시작 후보 수입니다.</param>
/// <param name="SkippedBytes">시작을 다시 찾느라 건너뛴 바이트 수입니다.</param>
/// <param name="Messages">완성한 메시지 수입니다.</param>
public readonly record struct PpgParserCounts(
    long CrcFailures = 0,
    long BadLengths = 0,
    long Implausible = 0,
    long SkippedBytes = 0,
    long Messages = 0);

/// <summary>
/// 받은 바이트 덩어리를 메시지로 나눕니다. CRC가 맞지 않거나 형식이 어긋나면 1바이트씩 건너뛰며 다시 맞춥니다.
/// 코드마다 길이가 하나로 정해진 메시지(상태, 오류, 정보, 시간)는 길이도 검사합니다. 위치를 잃었을 때 표본 값 안의
/// <c>FF 4D</c>를 시작으로 잘못 잡고 수천 바이트를 기다리지 않기 위해서입니다. 한 스레드에서만 씁니다.
/// </summary>
public sealed class PpgParser
{
    // 가장 긴 메시지는 추가 APG 결과(길이 0x12EA)입니다. 이보다 크면 잘못 맞춘 시작으로 보고 다시 찾습니다.
    const int MaxLength = 5000;

    byte[] _buffer = new byte[256];
    int _length;
    long _crcFailures;
    long _badLengths;
    long _implausible;
    long _skippedBytes;
    long _messages;

    public PpgParserCounts Counts => new(_crcFailures, _badLengths, _implausible, _skippedBytes, _messages);

    /// <summary>바이트를 더하고 이번에 완성된 메시지를 받은 순서대로 돌려줍니다.</summary>
    /// <param name="bytes">직렬 포트에서 읽은 덩어리입니다.</param>
    /// <param name="hostTimeUs">덩어리를 읽은 PC 시각입니다. 이 덩어리에서 끝나는 메시지에 붙습니다.</param>
    public IReadOnlyList<PpgMessage> Feed(ReadOnlySpan<byte> bytes, long hostTimeUs)
    {
        Append(bytes);
        var messages = new List<PpgMessage>();
        ReadOnlySpan<byte> buffer = _buffer.AsSpan(0, _length);
        int pos = 0;
        while (pos < buffer.Length)
        {
            // 머리 FF 뒤에 발신지가 오는 자리를 찾습니다. 발신지 바이트가 아직 없으면 다음 덩어리를 기다립니다.
            int start = pos;
            while (start < buffer.Length)
            {
                if (buffer[start] == 0xFF
                    && (start + 1 == buffer.Length
                        || buffer[start + 1] == PpgSource.Pc
                        || buffer[start + 1] == PpgSource.Device))
                {
                    break;
                }
                ++start;
            }
            Skip(ref pos, start - pos);
            if (buffer.Length - pos < 5) break;

            int length = (buffer[pos + 2] << 8) | buffer[pos + 3];
            if (length is < 3 or > MaxLength)
            {
                ++_badLengths;
                Skip(ref pos, 1);
                continue;
            }
            byte code = buffer[pos + 4];
            if (!Plausible(code, length))
            {
                ++_implausible;
                Skip(ref pos, 1);
                continue;
            }
            int total = 4 + length;
            if (buffer.Length - pos < total) break;
            ReadOnlySpan<byte> frame = buffer.Slice(pos, total);
            ushort crc = PpgWire.Crc16(frame[..^2]);
            if (frame[^2] != (crc & 0xFF) || frame[^1] != (crc >> 8))
            {
                ++_crcFailures;
                Skip(ref pos, 1);
                continue;
            }
            messages.Add(new PpgMessage(frame[1], code, frame[5..^2].ToArray(), hostTimeUs));
            ++_messages;
            pos += total;
        }
        buffer[pos..].CopyTo(_buffer);
        _length -= pos;
        return messages;
    }

    void Skip(ref int pos, int count)
    {
        _skippedBytes += count;
        pos += count;
    }

    void Append(ReadOnlySpan<byte> bytes)
    {
        if (_length + bytes.Length > _buffer.Length)
        {
            Array.Resize(ref _buffer, Math.Max(_buffer.Length * 2, _length + bytes.Length));
        }
        bytes.CopyTo(_buffer.AsSpan(_length));
        _length += bytes.Length;
    }

    // 장치가 보내는 코드와 PC가 보내는 코드입니다. 이 밖의 코드는 잘못 맞춘 시작으로 봅니다.
    static bool KnownCode(byte code) => code
        is (byte)'D' or (byte)'P' or (byte)'R' or (byte)'I' or (byte)'T'
        or PpgCode.Measure or PpgCode.Result or PpgCode.Status or PpgCode.Error or PpgCode.Info or PpgCode.Time;

    static bool Plausible(byte code, int length)
    {
        if (!KnownCode(code)) return false;
        return code switch
        {
            PpgCode.Status or PpgCode.Error => length == 4,
            PpgCode.Info or PpgCode.Time => length == 0x12,
            // 코드 1 + 표본 2N + 끝 3바이트 + CRC 2이므로 짝수입니다.
            PpgCode.Measure => length % 2 == 0,
            _ => true,
        };
    }
}
