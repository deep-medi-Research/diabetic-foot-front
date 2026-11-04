namespace DiabeticFoot.Platform.Tests;

public class PpgWireTests
{
    // 장치가 보내는 메시지를 만듭니다. PC 명령과 발신지만 다릅니다.
    static byte[] FromDevice(byte code, byte[] data)
    {
        byte[] message = PpgWire.BuildCommand(code, data);
        message[1] = PpgSource.Device;
        ushort crc = PpgWire.Crc16(message.AsSpan(0, message.Length - 2));
        message[^2] = (byte)(crc & 0xFF);
        message[^1] = (byte)(crc >> 8);
        return message;
    }

    static byte[] MeasureData(ushort[] samples, byte heartRate, byte spo2, byte error)
    {
        var data = new List<byte>();
        foreach (ushort sample in samples)
        {
            data.Add((byte)(sample >> 8));
            data.Add((byte)(sample & 0xFF));
        }
        data.Add(heartRate);
        data.Add(spo2);
        data.Add(error);
        return [.. data];
    }

    static byte[] Concat(params byte[][] parts) => parts.SelectMany(part => part).ToArray();

    // 사양서 예제: 정보 요청 FF 55 00 03 49의 CRC 바이트는 E0 DE입니다.
    [Fact]
    public void InfoRequest_MatchesSpecExample()
    {
        Assert.Equal(new byte[] { 0xFF, 0x55, 0x00, 0x03, 0x49, 0xE0, 0xDE }, PpgWire.BuildInfoRequest());
    }

    // 시작 명령은 길이 0x0009(코드 1 + 데이터 6 + CRC 2)이고 데이터는 요청 순서대로입니다.
    [Fact]
    public void BuildsStartCommand()
    {
        var request = new PpgStartRequest { PreviewS = 30, MeasureS = 60, Sensor = 'F', Gender = 'M', Age = 40, Reference = 'A' };

        Assert.True(PpgWire.TryBuildStart(request, out byte[]? start, out _));

        Assert.Equal(13, start.Length);
        Assert.Equal(0x00, start[2]);
        Assert.Equal(0x09, start[3]);
        Assert.Equal((byte)'D', start[4]);
        Assert.Equal(new byte[] { 30, 60, (byte)'F', (byte)'M', 40, (byte)'A' }, start[5..11]);
    }

    // 장치가 받지 않는 값은 명령을 만들지 않습니다.
    [Fact]
    public void RejectsOutOfRangeStart()
    {
        Assert.False(PpgWire.TryBuildStart(new PpgStartRequest { MeasureS = 30 }, out byte[]? command, out string? error));
        Assert.Null(command);
        Assert.Contains("60초 또는 150초", error);
        Assert.False(PpgWire.TryBuildStart(new PpgStartRequest { PreviewS = 5 }, out _, out _));
    }

    // 장치가 받는 값의 경계는 받고, 한 칸 벗어나면 받지 않습니다.
    [Theory]
    [InlineData(10, 60, 'P', 'M', 1, 'A', true)]
    [InlineData(60, 150, 'F', 'F', 120, 'W', true)]
    [InlineData(9, 60, 'P', 'M', 40, 'A', false)]
    [InlineData(61, 60, 'P', 'M', 40, 'A', false)]
    [InlineData(60, 61, 'P', 'M', 40, 'A', false)]
    [InlineData(60, 60, 'X', 'M', 40, 'A', false)]
    [InlineData(60, 60, 'P', 'X', 40, 'A', false)]
    [InlineData(60, 60, 'P', 'M', 0, 'A', false)]
    [InlineData(60, 60, 'P', 'M', 121, 'A', false)]
    [InlineData(60, 60, 'P', 'M', 40, 'X', false)]
    public void StartRequestRange(int preview, int measure, char sensor, char gender, int age, char reference, bool accepted)
    {
        var request = new PpgStartRequest
        {
            PreviewS = preview, MeasureS = measure, Sensor = sensor, Gender = gender, Age = age, Reference = reference,
        };

        Assert.Equal(accepted, PpgWire.TryBuildStart(request, out byte[]? command, out string? error));
        Assert.Equal(accepted, command is not null);
        Assert.Equal(!accepted, error is not null);
    }

    [Fact]
    public void StopCommand_IsCodeP()
    {
        byte[] stop = PpgWire.BuildStop();

        Assert.Equal(7, stop.Length);
        Assert.Equal((byte)'P', stop[4]);
    }

    // 여러 덩어리로 나뉘어 온 메시지를 이어서 합치고, 마지막 바이트가 든 덩어리의 시각을 메시지 시각으로 씁니다.
    [Fact]
    public void Parser_ParsesMessageSplitAcrossChunks()
    {
        byte[] status = FromDevice((byte)'s', [(byte)'v']);
        byte[] measure = FromDevice((byte)'m', MeasureData([100, 200, 65535], 72, 98, 0));
        byte[] all = Concat(status, measure);

        var parser = new PpgParser();
        IReadOnlyList<PpgMessage> got = parser.Feed(all.AsSpan(0, 5), 10);
        Assert.Empty(got);
        got = parser.Feed(all.AsSpan(5, status.Length + 3), 20);
        PpgMessage first = Assert.Single(got);
        Assert.Equal((byte)'s', first.Code);
        Assert.Equal(new[] { (byte)'v' }, first.Data);
        Assert.Equal(20, first.HostTimeUs);
        int used = 5 + status.Length + 3;
        got = parser.Feed(all.AsSpan(used), 30);
        PpgMessage second = Assert.Single(got);
        Assert.Equal((byte)'m', second.Code);
        Assert.Equal(30, second.HostTimeUs);
        PpgMeasure? decoded = PpgWire.DecodeMeasure(second.Data);
        Assert.NotNull(decoded);
        Assert.Equal(new ushort[] { 100, 200, 65535 }, decoded.Samples);
        Assert.Equal(72, decoded.HeartRate);
        Assert.Equal(98, decoded.Spo2);
        Assert.Equal(0, decoded.Error);
        Assert.Equal(2, parser.Counts.Messages);
        Assert.Equal(0, parser.Counts.SkippedBytes);
    }

    // 앞의 잡음과 CRC가 틀린 메시지를 건너뛰고 다음 정상 메시지를 찾습니다.
    [Fact]
    public void Parser_ResyncsAfterNoiseAndBadCrc()
    {
        byte[] broken = FromDevice((byte)'s', [(byte)'s']);
        broken[^1] ^= 0x01;
        byte[] good = FromDevice((byte)'s', [(byte)'p']);
        byte[] all = Concat([0x00, 0x12, 0xFF, 0x00], broken, good);

        var parser = new PpgParser();
        IReadOnlyList<PpgMessage> got = parser.Feed(all, 1);

        PpgMessage message = Assert.Single(got);
        Assert.Equal(new[] { (byte)'p' }, message.Data);
        Assert.Equal(1, parser.Counts.CrcFailures);
        Assert.True(parser.Counts.SkippedBytes > 0);
    }

    // 표본 값 안의 FF 4D를 상태 메시지 시작으로 잘못 잡아도 길이가 맞지 않으면 버리고 계속 찾습니다.
    [Fact]
    public void Parser_RejectsFixedLengthCodeWithWrongLength()
    {
        byte[] fakeStart = [0xFF, 0x4D, 0x10, 0x00, (byte)'s'];
        byte[] good = FromDevice((byte)'e', [(byte)'m']);

        var parser = new PpgParser();
        IReadOnlyList<PpgMessage> got = parser.Feed(Concat(fakeStart, good), 1);

        PpgMessage message = Assert.Single(got);
        Assert.Equal((byte)'e', message.Code);
        Assert.True(parser.Counts.BadLengths + parser.Counts.Implausible >= 1);
    }

    // 바이트를 하나씩 받아도 한 번에 받은 것과 같은 메시지와 같은 집계가 나옵니다.
    [Fact]
    public void Parser_ByteAtATimeEqualsOneChunk()
    {
        byte[] stream = Concat(
            [0x01, 0xFF],
            FromDevice((byte)'i', "200000270270320"u8.ToArray()),
            FromDevice((byte)'m', MeasureData([1, 2, 3, 4], 0, 0, 0)),
            [0xFF, 0x4D, 0x10, 0x00, (byte)'s'],
            FromDevice((byte)'s', [(byte)'v']));

        var whole = new PpgParser();
        var wholeMessages = whole.Feed(stream, 7).ToList();
        var piecewise = new PpgParser();
        var pieceMessages = new List<PpgMessage>();
        foreach (byte b in stream) pieceMessages.AddRange(piecewise.Feed([b], 7));

        Assert.Equal(wholeMessages.Count, pieceMessages.Count);
        for (int i = 0; i < wholeMessages.Count; i++)
        {
            Assert.Equal(wholeMessages[i].Code, pieceMessages[i].Code);
            Assert.Equal(wholeMessages[i].Data, pieceMessages[i].Data);
        }
        Assert.Equal(3, wholeMessages.Count);
        Assert.Equal(whole.Counts, piecewise.Counts);
    }

    // 끝 3바이트를 뺀 길이가 홀수인 측정 데이터는 풀지 않습니다.
    [Fact]
    public void DecodeMeasure_RejectsOddSampleBytes()
    {
        Assert.Null(PpgWire.DecodeMeasure([1, 2, 3, 4]));
        Assert.Null(PpgWire.DecodeMeasure([1, 2]));
        PpgMeasure? empty = PpgWire.DecodeMeasure([0, 0, (byte)'f']);
        Assert.NotNull(empty);
        Assert.Empty(empty.Samples);
        Assert.Equal("손가락 빠짐", PpgWire.MeasureErrorText(empty.Error));
    }

    [Theory]
    [InlineData(0, "")]
    [InlineData('f', "손가락 빠짐")]
    [InlineData('s', "신호 나쁨")]
    [InlineData('p', "센서 연결 안 됨")]
    [InlineData('x', "알 수 없는 측정 오류")]
    public void MeasureErrorText_NamesEachCode(int error, string expected)
    {
        Assert.Equal(expected, PpgWire.MeasureErrorText((byte)error));
    }
}
