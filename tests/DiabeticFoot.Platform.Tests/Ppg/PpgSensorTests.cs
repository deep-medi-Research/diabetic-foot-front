using System.Diagnostics;
using System.Text;
using DiabeticFoot.Core;
using DiabeticFoot.Core.Record;
using static DiabeticFoot.Platform.PpgFunctions;

namespace DiabeticFoot.Platform.Tests;

// 원본 test_ppg_sensor.cc의 도우미입니다.
static class PpgTestSupport
{
    public static byte[] FromDevice(char code, params byte[] data) => PpgWire.BuildMessage(PpgSource.Device, (byte)code, data);

    public static byte[] Measure(ushort first, int count, byte hr, byte error = 0)
    {
        var data = new List<byte>();
        for (int i = 0; i < count; ++i)
        {
            var v = (ushort)(first + i);
            data.Add((byte)(v >> 8));
            data.Add((byte)(v & 0xFF));
        }
        data.Add(hr);
        data.Add(98);
        data.Add(error);
        return FromDevice('m', [.. data]);
    }

    public static byte[] Info(string text) => FromDevice('i', Encoding.ASCII.GetBytes(text));

    // 받은 바이트를 모두 해석할 때까지 읽습니다. 가짜 포트는 받은 바이트를 기다리지 않고 돌려줍니다.
    public static PpgReading PumpUntil(PpgLink link, Func<PpgReading, bool> pred)
    {
        for (int i = 0; i < 1000 && !pred(link.Reading()); ++i)
        {
            if (!link.Pump(0)) break;
        }
        return link.Reading();
    }

    // 다른 스레드가 바꾸는 값을 기다립니다. 3초 안에 맞지 않으면 false입니다.
    public static bool WaitUntil(Func<bool> pred)
    {
        var watch = Stopwatch.StartNew();
        while (!pred())
        {
            if (watch.Elapsed > TimeSpan.FromSeconds(3)) return false;
            Thread.Sleep(2);
        }
        return true;
    }

    public static readonly TimeSpan Soon = TimeSpan.FromSeconds(3);

    public static SerialPortOpener Opener(FakeSerialLine line, string name = "COM6") => FakeSerialPort.Opener(name, line);

    // 포트에 쓴 명령이 하나 이상이고 모두 정보 요청('I')이면 true입니다.
    public static bool OnlyInfoRequests(IReadOnlyList<byte[]> writes)
    {
        foreach (byte[] w in writes)
        {
            if (w.Length < 5 || w[4] != 'I') return false;
        }
        return writes.Count > 0;
    }

    public static string TestFolder()
    {
        string dir = Path.Combine(Path.GetTempPath(), "hwlab_test_ppg");
        Directory.CreateDirectory(dir);
        return dir;
    }
}

// 시험용 PC 시계입니다. 테스트가 시각을 정합니다.
sealed class FakeClock
{
    long _now = 1_000_000;

    public long Now
    {
        get => Interlocked.Read(ref _now);
        set => Interlocked.Exchange(ref _now, value);
    }

    public Func<long> Fn => () => Now;
}

// 장치 스레드가 보낸 스냅숏과 파형을 모읍니다.
sealed class Received
{
    public readonly object Gate = new();
    public readonly List<PpgSnapshot> Snapshots = [];
    public int Waves;

    public PpgSensor.Callbacks Callbacks() => new()
    {
        State = s =>
        {
            lock (Gate) Snapshots.Add(s);
        },
        Wave = _ =>
        {
            lock (Gate) ++Waves;
        },
    };

    public PpgState Last()
    {
        lock (Gate) return Snapshots.Count == 0 ? PpgState.Off : Snapshots[^1].State;
    }

    public PpgSnapshot LastSnapshot()
    {
        lock (Gate) return Snapshots.Count == 0 ? new PpgSnapshot() : Snapshots[^1];
    }
}

public class PpgLinkTests
{
    // 열지 못하는 포트는 false와 이유를 돌려줍니다.
    [Fact]
    public void OpenFailsForUnknownPort()
    {
        var line = new FakeSerialLine();
        using var link = new PpgLink();
        Assert.False(link.Open(FakeSerialPort.Opener("COM6", line), "COM9", out string error));
        Assert.NotEqual("", error);
        Assert.Equal(error, link.Failure);
        Assert.Equal(PpgState.Off, link.Snapshot().State);
    }

    // 중지 뒤 시작 명령을 보내고, 상태와 측정 데이터를 받아 표본 수, 심박수, 측정 오류를 보입니다.
    [Fact]
    public void RestartsAndReadsMeasureData()
    {
        var line = new FakeSerialLine();
        var clock = new FakeClock();
        using var link = new PpgLink(clock.Fn);
        Assert.True(link.Open(PpgTestSupport.Opener(line), "COM6", out string error), error);
        Assert.Equal(PpgState.Checking, link.Snapshot().State);
        Assert.True(link.Restart(out error), error);
        Assert.Equal(2, line.Writes().Count);
        Assert.Equal((byte)'P', line.Writes()[0][4]);
        Assert.Equal((byte)'D', line.Writes()[1][4]);

        line.Feed(PpgTestSupport.FromDevice('s', (byte)'v'));
        line.Feed(PpgTestSupport.Measure(100, 24, 70));
        line.Feed(PpgTestSupport.Measure(124, 24, 71, (byte)'f'));
        PpgReading r = PpgTestSupport.PumpUntil(link, x => x.Samples == 48);
        Assert.Equal(48, r.Samples);
        Assert.Equal((byte)'v', r.Status);
        Assert.Equal(71, r.HeartRate);
        Assert.Equal((byte)'f', r.MeasureError);
        Assert.True(r.Open);
        Assert.True(r.Streaming);
        Assert.Equal(PpgState.Streaming, link.Snapshot().State);

        // 마지막 측정 데이터 뒤 0.5초가 지나면 표본이 멈춘 것으로 봅니다.
        clock.Now += PpgLink.StaleUs + 1;
        Assert.False(link.Reading().Streaming);
        Assert.Equal(PpgState.Stalled, link.Snapshot().State);
    }

    // 붙인 동안의 표본만 기록하고, 장치 시각은 표본 번호 / 500Hz이며 PC 수신 시각으로 공통 시각을 맞춥니다.
    [Fact]
    public void RecordsOnlyWhileAttached()
    {
        var line = new FakeSerialLine();
        var clock = new FakeClock();
        using var link = new PpgLink(clock.Fn);
        Assert.True(link.Open(PpgTestSupport.Opener(line), "COM6", out string error), error);
        Assert.True(link.Restart(out error));
        line.Feed(PpgTestSupport.Measure(0, 10, 60));
        PpgTestSupport.PumpUntil(link, x => x.Samples == 10);

        var record = new PpgShotRecord(new Timestamp(clock.Now));
        link.Attach(record);
        // 메시지 3개: 장치 시각으로 표본 10~19, 20~29, 30~39. PC는 마지막 표본 뒤 3ms에 받습니다.
        for (int m = 0; m < 3; ++m)
        {
            long lastDeviceUs = (10 + 10 * m + 9) * 2000;
            clock.Now = 5_000_000 + lastDeviceUs + 3000;
            line.Feed(PpgTestSupport.Measure((ushort)(10 + 10 * m), 10, 60));
            int expected = 20 + 10 * m;
            PpgTestSupport.PumpUntil(link, x => x.Samples == expected);
        }
        record.Close(new Timestamp(clock.Now));
        link.Detach();
        PpgRecord result = record.Finish();
        Assert.Equal(30, result.Values.Count);
        Assert.Equal(10, result.Values[0]);
        Assert.Equal(39, result.Values[^1]);
        Assert.Equal(10 * 2000, result.DeviceTime[0].Us);
        Assert.Equal(3, result.Anchors.Count);
        Assert.Equal(30, result.CommonTime.Count);
        Assert.Equal(5_000_000.0 + 39 * 2000 + 3000, result.CommonTime[^1].Us, 1.0);
        Assert.Equal(5_000_000.0 + 10 * 2000 + 3000, result.CommonTime[0].Us, 1.0);
        Assert.Null(result.DisconnectedAt);

        // 뗀 뒤 받은 표본은 기록에 들어가지 않습니다.
        line.Feed(PpgTestSupport.Measure(40, 10, 60));
        PpgTestSupport.PumpUntil(link, x => x.Samples == 50);
        Assert.Equal(30, record.Finish().Values.Count);
    }

    // 다시 시작 직후 측정 데이터 하나가 먼저 오고 다음 데이터가 평소보다 128ms 늦게 와도(실측 모양) 빈틈 뒤 표본의 공통 시각은
    // 수신 지연이 가장 짧은 경우의 PC 수신 시각과 1ms 안에서 맞고, 빈틈은 공통 시각에 그대로 남습니다.
    [Fact]
    public void RestartGapSplitsClockSegments()
    {
        var line = new FakeSerialLine();
        var clock = new FakeClock();
        using var link = new PpgLink(clock.Fn);
        Assert.True(link.Open(PpgTestSupport.Opener(line), "COM6", out string error), error);
        Assert.True(link.Restart(out error), error);
        var record = new PpgShotRecord(new Timestamp(clock.Now));
        link.Attach(record);

        // 메시지 m의 마지막 표본을 PC가 받는 시각입니다. 장치 시계는 0.5% 느리고 수신 지연은 3~7ms입니다.
        const int messages = 120;
        const long extraUs = 128_000;
        static long TrueUs(long deviceUs, long extra) => 5_000_000 + extra + (long)(deviceUs * 1.005) + 3_000;
        for (int m = 0; m < messages; ++m)
        {
            long lastDeviceUs = (24L * m + 23) * 2000;
            clock.Now = TrueUs(lastDeviceUs, m >= 1 ? extraUs : 0) + m % 5 * 1_000;
            line.Feed(PpgTestSupport.Measure((ushort)(24 * m), 24, 70));
            int expected = 24 * (m + 1);
            PpgTestSupport.PumpUntil(link, x => x.Samples == expected);
        }
        record.Close(new Timestamp(clock.Now));
        PpgRecord result = record.Finish();

        Assert.Equal(24 * messages, result.Values.Count);
        Assert.Equal(2, result.Clock.Count);
        Assert.Equal(23 * 2000, result.Clock[0].LastDevice.Us);
        for (int i = 24; i < result.CommonTime.Count; ++i)
        {
            Assert.Equal(TrueUs(result.DeviceTime[i].Us, extraUs), (double)result.CommonTime[i].Us, 1_000.0);
        }
        Assert.True(result.CommonTime[24].Us - result.CommonTime[23].Us > 100_000);

        // 사이드카에는 시계 구간 둘과 빈틈 하나(23번 표본 뒤)가 남습니다.
        string path = Path.Combine(PpgTestSupport.TestFolder(), "gap_ppg.json");
        Assert.True(WritePpgSidecar(path, result, "gap_ppg.csv", "COM6", null, out error), error);
        string json = File.ReadAllText(path);
        Assert.Contains("\"samples_file\": \"gap_ppg.csv\"", json);
        Assert.Contains("\"last_device_us\": 46000", json);
        Assert.Contains("\"count\": 2880,\n    \"messages\": 120", json);
        Assert.Contains("\"gaps\": [\n      {\n        \"after_sample\": 23,", json);
    }

    // 장치는 박동을 잡은 측정 데이터에만 심박수를 채웁니다. 0이 온 측정 데이터 사이에도 마지막 심박수를 HeartRateHoldUs 동안
    // 보이고, 그 뒤와 다시 시작한 뒤에는 0입니다.
    [Fact]
    public void HoldsHeartRateBetweenBeats()
    {
        var line = new FakeSerialLine();
        var clock = new FakeClock();
        using var link = new PpgLink(clock.Fn);
        Assert.True(link.Open(PpgTestSupport.Opener(line), "COM6", out string error), error);
        Assert.True(link.Restart(out error), error);
        line.Feed(PpgTestSupport.Measure(0, 24, 0));
        line.Feed(PpgTestSupport.Measure(24, 24, 84));
        line.Feed(PpgTestSupport.Measure(48, 24, 0));
        PpgTestSupport.PumpUntil(link, x => x.Samples == 72);
        Assert.Equal(84, link.Reading().HeartRate);
        Assert.Equal(98, link.Reading().Spo2);
        Assert.Equal(84, link.Snapshot().HeartRate);

        clock.Now += PpgLink.HeartRateHoldUs + 1;
        Assert.Equal(0, link.Reading().HeartRate);
        line.Feed(PpgTestSupport.Measure(72, 24, 86));
        PpgTestSupport.PumpUntil(link, x => x.Samples == 96);
        Assert.Equal(86, link.Reading().HeartRate);

        Assert.True(link.Restart(out error), error);
        Assert.Equal(0, link.Reading().HeartRate);
    }

    // 화면 파형용으로 최근 표본을 받은 순서대로 RecentCapacity개까지 보관하고, 다시 시작하면 비웁니다.
    [Fact]
    public void KeepsRecentValuesForWaveform()
    {
        var line = new FakeSerialLine();
        using var link = new PpgLink();
        Assert.True(link.Open(PpgTestSupport.Opener(line), "COM6", out string error), error);
        Assert.True(link.Restart(out error), error);
        line.Feed(PpgTestSupport.Measure(0, 24, 70));
        line.Feed(PpgTestSupport.Measure(24, 24, 70));
        PpgTestSupport.PumpUntil(link, x => x.Samples == 48);
        IReadOnlyList<ushort> last = link.RecentValues(30);
        Assert.Equal(30, last.Count);
        Assert.Equal(18, last[0]);
        Assert.Equal(47, last[^1]);
        Assert.Equal(48, link.RecentValues(1000).Count);

        // 210개 메시지(5040표본)를 받으면 가장 오래된 40개를 버립니다.
        for (int m = 2; m < 210; ++m) line.Feed(PpgTestSupport.Measure((ushort)(24 * m), 24, 70));
        PpgTestSupport.PumpUntil(link, x => x.Samples == 5040);
        IReadOnlyList<ushort> all = link.RecentValues(PpgLink.RecentCapacity + 100);
        Assert.Equal(PpgLink.RecentCapacity, all.Count);
        Assert.Equal(40, all[0]);
        Assert.Equal(5039, all[^1]);

        Assert.True(link.Restart(out error), error);
        Assert.Empty(link.RecentValues(10));
    }

    // 다른 연결이 장치를 측정 중으로 남겼어도, 다시 시작이 중지를 먼저 보내므로 장치가 새로 시작합니다.
    // 중지를 보내지 않으면 시작 명령이 오류 's'로 거부됩니다(2026-10-20 실측).
    [Fact]
    public void RestartRestartsDeviceLeftRunning()
    {
        var line = new FakeSerialLine();
        var device = new FakePpgDevice(line);
        {
            ISerialPort? other = PpgTestSupport.Opener(line)("COM6", new SerialConfig()).Port;
            Assert.NotNull(other);
            Assert.True(PpgWire.TryBuildStart(new PpgStartRequest(), out byte[]? start, out _));
            Assert.True(other.Write(start!, 100));
            device.HandleCommands();
            Assert.True(device.Running);
        }
        using var link = new PpgLink();
        Assert.True(link.Open(PpgTestSupport.Opener(line), "COM6", out string error), error);
        Assert.True(link.Restart(out error), error);
        device.HandleCommands();

        Assert.Equal(1, device.StopCount);
        Assert.Equal(2, device.StartCount);
        // 앞 연결의 'v', 중지의 'u', 새 시작의 'v'까지 받았을 때 오류가 없습니다.
        PpgReading r = PpgTestSupport.PumpUntil(link, x => x.Counts.Messages == 3);
        Assert.Equal((byte)'v', r.Status);
        Assert.Equal((byte)0, r.DeviceError);
    }

    // 시작 뒤 측정 데이터가 오지 않은 시간을 셉니다. 받은 적이 없으면 시작부터, 받은 뒤에는 마지막 측정 데이터부터입니다.
    [Fact]
    public void ReportsQuietTimeSinceLastData()
    {
        var line = new FakeSerialLine();
        var clock = new FakeClock();
        using var link = new PpgLink(clock.Fn);
        Assert.True(link.Open(PpgTestSupport.Opener(line), "COM6", out string error), error);
        Assert.Equal(0, link.Reading().QuietUs);

        Assert.True(link.Restart(out error), error);
        clock.Now += 1_500_000;
        Assert.Equal(1_500_000, link.Reading().QuietUs);

        line.Feed(PpgTestSupport.Measure(0, 24, 70));
        PpgTestSupport.PumpUntil(link, x => x.Samples == 24);
        Assert.Equal(0, link.Reading().QuietUs);
        clock.Now += 2_100_000;
        Assert.Equal(2_100_000, link.Reading().QuietUs);
    }

    // 실측(2026-10-21): 손가락 없이 미리보기가 끝나면 장치가 오류 'f'를 보내고 측정 데이터를 멈춥니다. 기록 중이 아니면
    // RestartIfStopped가 장치를 다시 시작하고, 장치가 미리보기로 돌아와 측정 데이터를 다시 보내며 장치 오류 표시도 지워집니다.
    [Fact]
    public void RecoversAfterDeviceStopsWithoutFinger()
    {
        var line = new FakeSerialLine();
        var device = new FakePpgDevice(line);
        var clock = new FakeClock();
        using var link = new PpgLink(clock.Fn);
        Assert.True(link.Open(PpgTestSupport.Opener(line), "COM6", out string error), error);
        Assert.True(link.Restart(out error), error);
        device.HandleCommands();
        device.EmitMeasurement(24, 0, 0, (byte)'f');
        PpgTestSupport.PumpUntil(link, x => x.Samples == 24);
        Assert.False(link.RestartIfStopped());

        device.EmitError((byte)'f');
        PpgTestSupport.PumpUntil(link, x => x.DeviceError == 'f');
        clock.Now += PpgRestartIntervalUs;
        Assert.False(link.Reading().Streaming);
        Assert.Equal((byte)'f', link.Snapshot().DeviceError);
        // 촬영 기록을 붙여 두었으면 다시 시작하지 않습니다.
        var record = new PpgShotRecord(new Timestamp(clock.Now));
        link.Attach(record);
        Assert.False(link.RestartIfStopped());
        link.Detach();
        Assert.True(link.RestartIfStopped());

        device.HandleCommands();
        Assert.True(device.Running);
        device.EmitMeasurement(24, 0, 0, (byte)'f');
        PpgReading r = PpgTestSupport.PumpUntil(link, x => x.Samples == 24);
        Assert.True(r.Streaming);
        Assert.Equal((byte)0, r.DeviceError);
        Assert.False(link.RestartIfStopped());
    }

    // 닫기와 소멸 때 시작한 장치에 중지 명령을 보냅니다.
    [Fact]
    public void CloseSendsStopCommand()
    {
        var line = new FakeSerialLine();
        string error;
        using (var link = new PpgLink())
        {
            Assert.True(link.Open(PpgTestSupport.Opener(line), "COM6", out error));
            Assert.True(link.Restart(out error));
            link.Close();
            Assert.False(link.IsOpen);
        }
        IReadOnlyList<byte[]> writes = line.Writes();
        Assert.Equal(3, writes.Count);
        Assert.Equal((byte)'P', writes[2][4]);

        using (var link = new PpgLink())
        {
            Assert.True(link.Open(PpgTestSupport.Opener(line), "COM6", out error));
            Assert.True(link.Restart(out error));
        }
        IReadOnlyList<byte[]> after = line.Writes();
        Assert.Equal(6, after.Count);
        Assert.Equal((byte)'P', after[5][4]);
    }

    // 녹화 중 장치가 빠지면 포트를 닫고, 그때까지 받은 표본은 촬영 기록에 남으며 끊긴 시각과 이유가 적힙니다.
    [Fact]
    public void UnplugDuringShotKeepsSamples()
    {
        var line = new FakeSerialLine();
        var clock = new FakeClock();
        using var link = new PpgLink(clock.Fn);
        Assert.True(link.Open(PpgTestSupport.Opener(line), "COM6", out string error), error);
        Assert.True(link.Restart(out error));
        var record = new PpgShotRecord(new Timestamp(clock.Now));
        link.Attach(record);
        line.Feed(PpgTestSupport.Measure(0, 24, 70));
        PpgTestSupport.PumpUntil(link, x => x.Samples == 24);

        clock.Now += 1_000_000;
        line.Unplug();
        Assert.False(link.Pump(0));
        Assert.False(link.IsOpen);
        Assert.NotEqual("", link.Failure);

        clock.Now += 2_000_000;
        record.Close(new Timestamp(clock.Now));
        PpgRecord result = record.Finish();
        Assert.Equal(24, result.Values.Count);
        Assert.NotNull(result.DisconnectedAt);
        Assert.Equal(2_000_000, result.DisconnectedAt.Value.Us);
        Assert.Equal(link.Failure, result.DisconnectReason);
        Assert.Equal(4_000_000, result.EndTime.Us);
    }

    // 장치를 빼면 읽기가 실패해 포트를 닫고 이유를 남깁니다.
    [Fact]
    public void ReportsUnplug()
    {
        var line = new FakeSerialLine();
        using var link = new PpgLink();
        Assert.True(link.Open(PpgTestSupport.Opener(line), "COM6", out _));
        line.Unplug();
        Assert.False(link.Pump(10));
        Assert.False(link.IsOpen);
        Assert.NotEqual("", link.Failure);
        Assert.Equal("COM6", link.PortName);
    }

    // 장치를 확인하기 전에는 정보 요청을 PpgInfoIntervalUs마다 보내고, 연 뒤 PpgPreparingAfterUs가 지나면 준비 중입니다.
    // 정보 응답을 받으면 장치를 확인하고 장치 정보를 남기며, 그 뒤로는 정보 요청을 보내지 않습니다.
    [Fact]
    public void AsksDeviceInfoUntilDeviceAnswers()
    {
        var line = new FakeSerialLine();
        var clock = new FakeClock();
        using var link = new PpgLink(clock.Fn);
        Assert.True(link.Open(PpgTestSupport.Opener(line), "COM6", out string error), error);
        Assert.False(link.Found);
        Assert.Equal(PpgState.Checking, link.Snapshot().State);
        link.AskDeviceInfo();
        link.AskDeviceInfo();
        Assert.Single(line.Writes());
        Assert.True(PpgTestSupport.OnlyInfoRequests(line.Writes()));

        clock.Now += PpgPreparingAfterUs;
        Assert.Equal(PpgState.Preparing, link.Snapshot().State);
        link.AskDeviceInfo();
        Assert.Equal(2, line.Writes().Count);

        line.Feed(PpgTestSupport.Info("100002280280320"));
        PpgTestSupport.PumpUntil(link, _ => link.Found);
        Assert.True(link.Found);
        Assert.Equal("100002280280320", link.DeviceInfo);
        Assert.Equal(PpgState.Waiting, link.Snapshot().State);
        clock.Now += PpgInfoIntervalUs;
        link.AskDeviceInfo();
        Assert.Equal(2, line.Writes().Count);

        // 촬영 기록을 붙이면 받아 둔 장치 정보가 기록에 남습니다.
        var record = new PpgShotRecord(new Timestamp(0));
        link.Attach(record);
        record.Close(new Timestamp(clock.Now));
        Assert.Equal("100002280280320", record.Finish().DeviceInfo);

        // 다시 열면 장치 확인을 처음부터 합니다.
        link.Detach();
        Assert.True(link.Open(PpgTestSupport.Opener(line), "COM6", out error), error);
        Assert.False(link.Found);
        Assert.Equal("", link.DeviceInfo);
    }

    // 앞 연결이 장치를 측정 중으로 남기면 측정 데이터로 장치를 확인합니다. 장치 정보를 받지 못했으므로 정보 요청을 한 번만 더 보냅니다.
    [Fact]
    public void AsksDeviceInfoOnceWhenFoundByMeasureData()
    {
        var line = new FakeSerialLine();
        var clock = new FakeClock();
        using var link = new PpgLink(clock.Fn);
        Assert.True(link.Open(PpgTestSupport.Opener(line), "COM6", out string error), error);
        line.Feed(PpgTestSupport.Measure(0, 24, 70));
        PpgTestSupport.PumpUntil(link, _ => link.Found);
        Assert.True(link.Found);
        Assert.Equal(0, link.Reading().Samples);  // 시작하기 전에 받은 표본은 세지 않음
        link.AskDeviceInfo();
        link.AskDeviceInfo();
        clock.Now += PpgInfoIntervalUs;
        link.AskDeviceInfo();
        Assert.Single(line.Writes());
        Assert.True(PpgTestSupport.OnlyInfoRequests(line.Writes()));
    }
}

public class PpgErrorShareTextTests
{
    // 측정 오류 값 하나의 양을 초와 비율로 씁니다. 1% 아래는 소수점 아래 한 자리, 여러 번 끊겨 나왔으면 횟수를 붙입니다.
    [Fact]
    public void WritesSecondsPercentAndRuns()
    {
        Assert.Equal("신호 나쁨 4.3초(14%)", PpgErrorShareText(new PpgErrorShare((byte)'s', 2150, 2150, 1), 15_000));
        Assert.Equal("손가락 빠짐 0.1초(0.3%, 2번)", PpgErrorShareText(new PpgErrorShare((byte)'f', 48, 24, 2), 15_000));
        Assert.Equal("센서 연결 안 됨 30.0초(100%)", PpgErrorShareText(new PpgErrorShare((byte)'p', 15_000, 15_000, 1), 15_000));
    }
}

public class PpgWavePointsTests
{
    // 화면 파형은 5개씩 평균한 점이고, 튀는 값 몇 개가 높이를 정하지 않도록 1~99% 범위를 0~1로 옮깁니다.
    [Fact]
    public void AveragesAndScalesWithoutOutliers()
    {
        var values = new ushort[1000];
        for (int i = 0; i < 1000; ++i) values[i] = (ushort)(1000 + i / 5 % 2 * 100);
        values[500] = 60_000;  // 손가락을 넣고 뺄 때처럼 튄 값
        IReadOnlyList<double> points = PpgWavePoints(values);
        Assert.Equal(200, points.Count);
        Assert.Equal(0.0, points[0]);
        Assert.Equal(1.0, points[1]);
        Assert.Equal(1.0, points[100]);  // 튄 값이 든 점은 1에 머물고 나머지 높이를 바꾸지 않음
        Assert.Empty(PpgWavePoints(Enumerable.Repeat((ushort)1000, 9).ToArray()));
    }
}

public class PpgNeedsRestartTests
{
    // 측정 구간 진입('s'), 측정 끝('p'), 측정 데이터가 2초 넘게 없음이면 다시 시작하고, 다시 시작한 뒤 5초 안이면 하지 않습니다.
    [Fact]
    public void Rules()
    {
        var r = new PpgReading { Open = true, Status = (byte)'v', QuietUs = 100_000 };
        Assert.False(PpgNeedsRestart(r, PpgRestartIntervalUs));
        r = r with { Status = (byte)'s' };
        Assert.True(PpgNeedsRestart(r, PpgRestartIntervalUs));
        r = r with { Status = (byte)'p' };
        Assert.True(PpgNeedsRestart(r, PpgRestartIntervalUs));
        r = r with { Status = (byte)'v', QuietUs = PpgRestartQuietUs + 1 };
        Assert.True(PpgNeedsRestart(r, PpgRestartIntervalUs));
        Assert.False(PpgNeedsRestart(r, PpgRestartIntervalUs - 1));
        r = r with { Open = false };
        Assert.False(PpgNeedsRestart(r, PpgRestartIntervalUs));
    }
}

public class PpgShotRecordTests
{
    // 촬영을 끝낸 뒤에는 표본과 끊긴 시각을 더하지 않습니다. 끊긴 시각은 처음 한 번만 적습니다.
    [Fact]
    public void IgnoresAdditionsAfterClose()
    {
        var record = new PpgShotRecord(new Timestamp(100));
        var measure = new PpgMeasure([1, 2, 3], 0, 97, 0);
        record.AddMeasure(measure, 0, new Timestamp(200));
        record.MarkDisconnected(new Timestamp(300), "첫 이유");
        record.MarkDisconnected(new Timestamp(400), "둘째 이유");
        record.Close(new Timestamp(500));
        Assert.True(record.Closed);
        record.AddMeasure(measure, 3, new Timestamp(600));
        record.Close(new Timestamp(700));

        PpgRecord result = record.Finish();
        Assert.Equal(3, result.Values.Count);
        Assert.Equal(3, result.CommonTime.Count);
        Assert.NotNull(result.DisconnectedAt);
        Assert.Equal(300, result.DisconnectedAt.Value.Us);
        Assert.Equal("첫 이유", result.DisconnectReason);
        Assert.Equal(100, result.BeginTime.Us);
        Assert.Equal(500, result.EndTime.Us);
    }
}

public class PpgSensorTests
{
    // 장치 스레드는 연결, 장치 확인, 시작, 표본 수신을 거치며 상태가 바뀔 때만 스냅숏을 보내고, 표본이 오는 동안 파형을 보냅니다.
    [Fact]
    public void SendsSnapshotsOnlyWhenStateChanges()
    {
        var line = new FakeSerialLine();
        var device = new FakePpgDevice(line);
        var received = new Received();
        using var sensor = new PpgSensor(PpgTestSupport.Opener(line), null, received.Callbacks());
        sensor.Connect("COM6");
        // 포트를 열면 정보 요청만 보내고, 장치가 답한 뒤 중지와 시작을 보냅니다.
        Assert.True(line.WaitForWrites(0, PpgTestSupport.Soon) >= 1);
        Assert.True(PpgTestSupport.OnlyInfoRequests(line.Writes()));
        device.HandleCommands();
        Assert.True(line.WaitForWrites(2, PpgTestSupport.Soon) >= 3);
        Assert.True(PpgTestSupport.WaitUntil(() => received.Last() == PpgState.Waiting));
        device.HandleCommands();
        for (int i = 0; i < 6; ++i) device.EmitMeasurement(24, 70, 98);
        Assert.True(PpgTestSupport.WaitUntil(() => received.Last() == PpgState.Streaming));
        Assert.True(PpgTestSupport.WaitUntil(() =>
        {
            lock (received.Gate) return received.Waves > 0;
        }));

        lock (received.Gate)
        {
            List<PpgSnapshot> snapshots = received.Snapshots;
            Assert.True(snapshots.Count >= 5, $"{snapshots.Count}");
            Assert.Equal(PpgState.Off, snapshots[0].State);
            Assert.Equal(PpgState.Opening, snapshots[1].State);
            Assert.Equal("COM6", snapshots[1].Port);
            Assert.Equal(PpgState.Checking, snapshots[2].State);
            Assert.Equal(PpgState.Waiting, snapshots[3].State);
            Assert.Equal(70, snapshots[^1].HeartRate);
            for (int i = 1; i < snapshots.Count; ++i) Assert.False(snapshots[i] == snapshots[i - 1], $"{i}");
        }
    }

    // 장치 스레드가 촬영 기록에 표본을 더하고, 녹화 중 장치가 빠져도 기록은 남아 끊긴 시각이 적히며 상태는 Failed가 됩니다.
    [Fact]
    public void KeepsShotRecordWhenUnpluggedDuringShot()
    {
        var line = new FakeSerialLine();
        var device = new FakePpgDevice(line);
        var received = new Received();
        using var sensor = new PpgSensor(PpgTestSupport.Opener(line), null, received.Callbacks());
        sensor.Connect("COM6");
        Assert.True(line.WaitForWrites(0, PpgTestSupport.Soon) >= 1);
        device.HandleCommands();
        Assert.True(line.WaitForWrites(2, PpgTestSupport.Soon) >= 3);
        device.HandleCommands();
        Assert.True(PpgTestSupport.WaitUntil(() => received.Last() == PpgState.Waiting));

        var record = new PpgShotRecord(new Timestamp(0));
        sensor.BeginShot(record);
        // 촬영 버튼은 장치를 다시 시작합니다(중지와 시작).
        Assert.True(line.WaitForWrites(4, PpgTestSupport.Soon) >= 5);
        device.HandleCommands();
        device.EmitMeasurement(24, 70, 98);
        device.EmitMeasurement(24, 70, 98);
        Assert.True(PpgTestSupport.WaitUntil(() => record.Finish().Values.Count == 48));

        line.Unplug();
        Assert.True(PpgTestSupport.WaitUntil(() => received.Last() == PpgState.Failed));
        record.Close(new Timestamp(long.MaxValue));
        sensor.EndShot();
        PpgRecord result = record.Finish();
        Assert.Equal(48, result.Values.Count);
        Assert.NotNull(result.DisconnectedAt);
        Assert.NotEqual("", result.DisconnectReason);
        Assert.Equal("200000270270320", result.DeviceInfo);
    }

    // 직렬 드라이버가 멈춰 중지 명령 쓰기가 돌아오지 않아도, Dispose는 QuitWaitUs 뒤 장치 스레드를 놓아 두고 돌아옵니다.
    [Fact]
    public void DestructorDoesNotWaitForStuckPort()
    {
        var line = new FakeSerialLine();
        var device = new FakePpgDevice(line);
        var sensor = new PpgSensor(PpgTestSupport.Opener(line), null, new PpgSensor.Callbacks());
        sensor.Connect("COM6");
        Assert.True(line.WaitForWrites(0, PpgTestSupport.Soon) >= 1);
        device.HandleCommands();
        Assert.True(line.WaitForWrites(2, PpgTestSupport.Soon) >= 3);
        device.HandleCommands();

        line.HoldWrites();
        var watch = Stopwatch.StartNew();
        sensor.Dispose();
        TimeSpan waited = watch.Elapsed;
        Assert.True(waited >= TimeSpan.FromMicroseconds(PpgSensor.QuitWaitUs - 50_000), $"{waited}");
        Assert.True(waited < TimeSpan.FromMicroseconds(PpgSensor.QuitWaitUs + 1_500_000), $"{waited}");

        // 놓아 둔 장치 스레드가 끝나도록 풀어 줍니다. 장치 스레드는 자기 상태만 참조합니다.
        line.ReleaseWrites();
        Assert.True(PpgTestSupport.WaitUntil(() => line.Writes().Count == 4));
        Assert.Equal((byte)'P', line.Writes()[3][4]);
    }

    // 전원을 켠 직후처럼 장치가 답하지 않는 동안에는 정보 요청만 보내고 준비 중을 보입니다. 장치가 초기화를 끝내고 장치 정보와
    // 상태 'o'를 보내면 그때 중지와 시작을 보내 파형을 받습니다.
    [Fact]
    public void StartsAfterDeviceFinishesBooting()
    {
        var line = new FakeSerialLine();
        var device = new FakePpgDevice(line);
        device.StartBooting();
        var clock = new FakeClock();
        var received = new Received();
        using var sensor = new PpgSensor(PpgTestSupport.Opener(line), null, received.Callbacks(), clock.Fn);
        sensor.Connect("COM6");
        Assert.True(line.WaitForWrites(0, PpgTestSupport.Soon) >= 1);
        Assert.True(PpgTestSupport.WaitUntil(() => received.Last() == PpgState.Checking));

        clock.Now += PpgPreparingAfterUs;
        Assert.True(PpgTestSupport.WaitUntil(() => received.Last() == PpgState.Preparing));
        Assert.True(line.WaitForWrites(1, PpgTestSupport.Soon) >= 2);
        clock.Now += PpgInfoIntervalUs;
        Assert.True(line.WaitForWrites(2, PpgTestSupport.Soon) >= 3);
        device.HandleCommands();
        Assert.True(PpgTestSupport.OnlyInfoRequests(line.Writes()));
        Assert.Equal(0, device.StartCount);
        Assert.Equal(0, device.StopCount);

        int asked = line.Writes().Count;
        device.FinishBooting();
        Assert.True(line.WaitForWrites(asked + 1, PpgTestSupport.Soon) >= asked + 2);
        device.HandleCommands();
        Assert.Equal(1, device.StartCount);
        for (int i = 0; i < 6; ++i) device.EmitMeasurement(24, 70, 98);
        Assert.True(PpgTestSupport.WaitUntil(() => received.Last() == PpgState.Streaming));
    }

    // 조명 보드처럼 PPG 장치가 아닌 포트에는 정보 요청만 보냅니다. PpgFindTimeoutUs 동안 답이 없으면 중지와 시작 없이 포트를
    // 닫고 찾지 못했다고 알립니다.
    [Fact]
    public void ClosesPortWithoutStartWhenNoDeviceAnswers()
    {
        var line = new FakeSerialLine();
        var clock = new FakeClock();
        var received = new Received();
        using var sensor = new PpgSensor(PpgTestSupport.Opener(line, "COM5"), null, received.Callbacks(), clock.Fn);
        sensor.Connect("COM5");
        Assert.True(line.WaitForWrites(0, PpgTestSupport.Soon) >= 1);
        Assert.True(PpgTestSupport.WaitUntil(() => received.Last() == PpgState.Checking));

        clock.Now += PpgFindTimeoutUs;
        Assert.True(PpgTestSupport.WaitUntil(() => received.Last() == PpgState.Failed));
        PpgSnapshot last = received.LastSnapshot();
        Assert.Equal("COM5", last.Port);
        Assert.Equal("이 포트에서 PPG 장치를 찾지 못했습니다", last.Problem);
        Assert.True(PpgTestSupport.OnlyInfoRequests(line.Writes()));
    }

    // 장치를 확인하기 전에 촬영 버튼을 누르고 녹화 중에 장치가 답하면, 녹화 중인 파형을 바꾸지 않도록 시작하지 않고 장치 정보만
    // 기록에 남깁니다. 촬영을 끝내면 시작합니다.
    [Fact]
    public void StartsAfterShotWhenDeviceAnswersDuringShot()
    {
        var line = new FakeSerialLine();
        var device = new FakePpgDevice(line);
        device.StartBooting();
        var received = new Received();
        using var sensor = new PpgSensor(PpgTestSupport.Opener(line), null, received.Callbacks());
        sensor.Connect("COM6");
        Assert.True(line.WaitForWrites(0, PpgTestSupport.Soon) >= 1);

        var record = new PpgShotRecord(new Timestamp(0));
        sensor.BeginShot(record);
        Assert.True(PpgTestSupport.WaitUntil(() => sensor.PendingSinceUs == 0));
        device.FinishBooting();
        Assert.True(PpgTestSupport.WaitUntil(() => record.Finish().DeviceInfo == "200000270270320"));
        Assert.True(PpgTestSupport.WaitUntil(() => received.Last() == PpgState.Waiting));
        Assert.True(PpgTestSupport.OnlyInfoRequests(line.Writes()));

        int before = line.Writes().Count;
        record.Close(new Timestamp(long.MaxValue));
        sensor.EndShot();
        Assert.True(line.WaitForWrites(before + 1, PpgTestSupport.Soon) >= before + 2);
        device.HandleCommands();
        Assert.Equal(1, device.StartCount);
        Assert.Empty(record.Finish().Values);
    }

    // 기록을 CSV로 쓰고 다시 읽으면 같은 값이고, 열 이름이 다른 파일은 읽지 않습니다.
    [Fact]
    public void CsvRoundTrip()
    {
        var record = new PpgRecord
        {
            Values = Enumerable.Range(0, 5).Select(i => (ushort)(1000 + i)).ToArray(),
            DeviceTime = Enumerable.Range(0, 5).Select(i => new Timestamp(i * 2000)).ToArray(),
            CommonTime = Enumerable.Range(0, 5).Select(i => new Timestamp(7_000_000 + i * 2000)).ToArray(),
            MeasureError = Enumerable.Range(0, 5).Select(i => (byte)(i == 4 ? 'f' : 0)).ToArray(),
            HeartRate = Enumerable.Repeat((byte)72, 5).ToArray(),
            Spo2 = Enumerable.Repeat((byte)98, 5).ToArray(),
        };
        string dir = PpgTestSupport.TestFolder();
        string path = Path.Combine(dir, "a_ppg.csv");
        Assert.True(WritePpgCsv(path, record, out string error), error);
        PpgCsv? csv = ReadPpgCsv(path, out error);
        Assert.True(csv is not null, error);
        Assert.Equal(5, csv.Value.Count);
        Assert.Equal(1002, csv.Value[2]);
        Assert.Equal(7_008_000, csv.CommonTimeUs[4]);
        Assert.Equal((int)'f', csv.MeasureError[4]);
        Assert.Equal(72, csv.HeartRate[0]);
        Assert.Equal(98, csv.Spo2[3]);

        string bad = Path.Combine(dir, "bad_ppg.csv");
        File.WriteAllText(bad, "t,v\n1,2\n");
        Assert.Null(ReadPpgCsv(bad, out error));
        Assert.Null(ReadPpgCsv(Path.Combine(dir, "none.csv"), out _));
    }
}
