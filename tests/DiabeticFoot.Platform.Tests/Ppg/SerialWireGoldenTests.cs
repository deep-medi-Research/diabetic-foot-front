using System.Globalization;
using System.Text;

namespace DiabeticFoot.Platform.Tests;

/// <summary>
/// 원본 C++ 구현이 같은 입력에 낸 바이트(data/serial/cpp_golden.txt)와 이식본이 내는 바이트를 줄 단위로 비교합니다.
/// 줄 형식은 다음과 같고 바이트는 16진수 대문자이며 빈 값은 '-'입니다.
///   crc 입력 CRC
///   command 코드 데이터 메시지
///   start 미리보기 측정 센서 성별 나이 기준 ok 메시지 | err 이유
///   light 밝기 명령
///   decode 데이터 (null | 표본(쉼표) 심박수 SpO2 오류)
///   parse 번호, in 입력, chunks 덩어리 크기들, msg 발신지 코드 데이터 시각, counts 다섯 집계
///   fake 이름 다음에 op(동작)와 expect(기대하는 장치 상태나 장치가 보낸 바이트)
///   wave 표본 번호 값, wave_fnv 표본 수 해시
/// </summary>
public class SerialWireGoldenTests
{
    static readonly string[] Lines =
        File.ReadAllLines(Path.Combine(AppContext.BaseDirectory, "data", "serial", "cpp_golden.txt"))
            .Where(line => !line.StartsWith('#'))
            .ToArray();

    static IEnumerable<string[]> Rows(string kind, int maxParts = int.MaxValue) =>
        Lines.Where(line => line.StartsWith(kind + " ")).Select(line => line.Split(' ', maxParts));

    static byte[] Bytes(string hex) => hex == "-" ? [] : Convert.FromHexString(hex);

    static string Hex(ReadOnlySpan<byte> bytes) => bytes.IsEmpty ? "-" : Convert.ToHexString(bytes);

    [Fact]
    public void Crc16()
    {
        List<string[]> rows = Rows("crc").ToList();
        Assert.True(rows.Count > 60);
        foreach (string[] row in rows) Assert.Equal(row[2], PpgWire.Crc16(Bytes(row[1])).ToString("X4"));
    }

    [Fact]
    public void BuildCommand()
    {
        List<string[]> rows = Rows("command").ToList();
        Assert.True(rows.Count > 30);
        foreach (string[] row in rows)
        {
            byte code = Convert.ToByte(row[1], 16);
            Assert.Equal(row[3], Hex(PpgWire.BuildCommand(code, Bytes(row[2]))));
        }
    }

    [Fact]
    public void BuildStopAndInfoRequest()
    {
        Assert.Equal(Rows("stop").Single()[1], Hex(PpgWire.BuildStop()));
        Assert.Equal(Rows("info").Single()[1], Hex(PpgWire.BuildInfoRequest()));
    }

    [Fact]
    public void BuildStart()
    {
        List<string[]> rows = Rows("start", 9).ToList();
        Assert.True(rows.Count > 300);
        Assert.Contains(rows, row => row[7] == "ok");
        Assert.Contains(rows, row => row[7] == "err");
        foreach (string[] row in rows)
        {
            var request = new PpgStartRequest
            {
                PreviewS = int.Parse(row[1], CultureInfo.InvariantCulture),
                MeasureS = int.Parse(row[2], CultureInfo.InvariantCulture),
                Sensor = row[3][0],
                Gender = row[4][0],
                Age = int.Parse(row[5], CultureInfo.InvariantCulture),
                Reference = row[6][0],
            };

            bool ok = PpgWire.TryBuildStart(request, out byte[]? command, out string? error);

            Assert.Equal(row[7] == "ok", ok);
            Assert.Equal(row[8], ok ? Hex(command!) : error);
        }
    }

    [Fact]
    public void LightCommand()
    {
        List<string[]> rows = Rows("light").ToList();
        Assert.Equal(601, rows.Count);
        foreach (string[] row in rows)
        {
            int percent = int.Parse(row[1], CultureInfo.InvariantCulture);
            Assert.Equal(row[2], Convert.ToHexString(LightProtocol.MakeCommand(percent)));
        }
    }

    [Fact]
    public void DecodeMeasure()
    {
        List<string[]> rows = Rows("decode").ToList();
        Assert.True(rows.Count > 50);
        Assert.Contains(rows, row => row[2] == "null");
        Assert.Contains(rows, row => row[2] != "null");
        foreach (string[] row in rows)
        {
            PpgMeasure? decoded = PpgWire.DecodeMeasure(Bytes(row[1]));

            if (row[2] == "null")
            {
                Assert.Null(decoded);
                continue;
            }
            Assert.NotNull(decoded);
            string samples = decoded.Samples.Length == 0 ? "-" : string.Join(',', decoded.Samples);
            Assert.Equal($"{row[2]} {row[3]} {row[4]} {row[5]}", $"{samples} {decoded.HeartRate} {decoded.Spo2} {decoded.Error}");
        }
    }

    [Fact]
    public void MeasureErrorText()
    {
        foreach (string[] row in Rows("error_text", 3))
        {
            string expected = row[2] == "-" ? "" : row[2];
            Assert.Equal(expected, PpgWire.MeasureErrorText(byte.Parse(row[1], CultureInfo.InvariantCulture)));
        }
    }

    [Fact]
    public void SerialNumberFromInstanceId()
    {
        List<string> rows = Lines.Where(line => line.StartsWith("serial_number ")).ToList();
        Assert.True(rows.Count >= 14);
        foreach (string row in rows)
        {
            string[] parts = row["serial_number ".Length..].Split(" => ");
            Assert.Equal(parts[1], UsbDeviceId.SerialNumberFromInstanceId(parts[0]));
        }
    }

    [Fact]
    public void ParserOnNoisyStreams()
    {
        int cases = 0;
        for (int i = 0; i < Lines.Length; i++)
        {
            if (!Lines[i].StartsWith("parse ")) continue;
            byte[] stream = Bytes(Lines[++i].Split(' ')[1]);
            int[] chunks = Lines[++i].Split(' ')[1].Split(',').Select(int.Parse).ToArray();
            var parser = new PpgParser();
            var actual = new List<string>();
            int used = 0;
            for (int k = 0; k < chunks.Length; k++)
            {
                IReadOnlyList<PpgMessage> got = parser.Feed(stream.AsSpan(used, chunks[k]), (long)k * 1000 + 5);
                used += chunks[k];
                actual.AddRange(got.Select(m => $"msg {m.Source:X2} {m.Code:X2} {Hex(m.Data)} {m.HostTimeUs}"));
            }
            var expected = new List<string>();
            while (Lines[i + 1].StartsWith("msg ")) expected.Add(Lines[++i]);
            PpgParserCounts counts = parser.Counts;

            Assert.Equal(expected, actual);
            Assert.Equal(
                Lines[++i],
                $"counts {counts.CrcFailures} {counts.BadLengths} {counts.Implausible} {counts.SkippedBytes} {counts.Messages}");
            cases++;
        }
        Assert.Equal(12, cases);
    }

    // 가짜 장치에 같은 동작을 순서대로 하고, 장치 상태와 장치가 포트로 보낸 바이트가 원본과 같은지 봅니다.
    [Fact]
    public void FakeDeviceScripts()
    {
        int scripts = 0;
        int expectations = 0;
        var line = new FakeSerialLine();
        FakePpgDevice device = new(line);
        ISerialPort port = FakeSerialPort.Opener("COM6", line)("COM6", new SerialConfig()).Port!;
        int handled = 0;

        foreach (string row in Lines.SkipWhile(l => !l.StartsWith("fake ")).TakeWhile(l => !l.StartsWith("wave ")))
        {
            string[] parts = row.Split(' ');
            if (parts[0] == "fake")
            {
                line = new FakeSerialLine();
                device = new FakePpgDevice(line);
                port = FakeSerialPort.Opener("COM6", line)("COM6", new SerialConfig()).Port!;
                scripts++;
            }
            else if (parts[0] == "op")
            {
                switch (parts[1])
                {
                    case "write": Assert.True(port.Write(Bytes(parts[2]), 100)); break;
                    case "handle": handled = device.HandleCommands().Count; break;
                    case "measure":
                        device.EmitMeasurement(int.Parse(parts[2]), byte.Parse(parts[3]), byte.Parse(parts[4]), byte.Parse(parts[5]));
                        break;
                    case "status": device.EmitStatus(byte.Parse(parts[2])); break;
                    case "error": device.EmitError(byte.Parse(parts[2])); break;
                    case "boot_start": device.StartBooting(); break;
                    case "boot_finish": device.FinishBooting(); break;
                    default: throw new InvalidOperationException(row);
                }
            }
            else if (parts[0] == "expect")
            {
                string expected = row["expect ".Length..];
                string actual = parts[1] switch
                {
                    "inbound" => "inbound " + Hex(Drain(port)),
                    "handled" =>
                        $"handled {handled} running={(device.Running ? 1 : 0)} starts={device.StartCount} stops={device.StopCount}" +
                        $" infos={device.InfoRequestCount} laststart={Hex(device.LastStartData)}",
                    _ => $"running={(device.Running ? 1 : 0)}",
                };
                Assert.Equal(expected, actual);
                expectations++;
            }
        }

        Assert.Equal(4, scripts);
        Assert.True(expectations > 60);
    }

    static byte[] Drain(ISerialPort port)
    {
        var all = new List<byte>();
        var buffer = new byte[4096];
        while (port.Read(buffer, 0) is > 0 and var n) all.AddRange(buffer.AsSpan(0, n).ToArray());
        return [.. all];
    }

    // 맥파 값은 C++ std::sin과 std::lround로 낸 값과 같아야 합니다. 앞 1500개는 값을 그대로, 200만 개는 해시로 비교합니다.
    [Fact]
    public void WaveSamples()
    {
        List<string[]> rows = Rows("wave").ToList();
        Assert.Equal(1500, rows.Count);
        foreach (string[] row in rows)
        {
            Assert.Equal(ushort.Parse(row[2], CultureInfo.InvariantCulture), FakePpgDevice.WaveAt(long.Parse(row[1], CultureInfo.InvariantCulture)));
        }

        string[] hash = Rows("wave_fnv").Single();
        long count = long.Parse(hash[1], CultureInfo.InvariantCulture);
        ulong fnv = 1469598103934665603ul;
        for (long i = 0; i < count; i++)
        {
            ushort value = FakePpgDevice.WaveAt(i);
            fnv = (fnv ^ (byte)value) * 1099511628211ul;
            fnv = (fnv ^ (byte)(value >> 8)) * 1099511628211ul;
        }
        Assert.Equal(hash[2], fnv.ToString("X16"));
    }
}
