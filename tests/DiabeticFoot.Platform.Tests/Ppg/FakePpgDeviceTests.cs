using System.Text;

namespace DiabeticFoot.Platform.Tests;

public class FakePpgDeviceTests
{
    sealed class Rig
    {
        public FakeSerialLine Line { get; } = new();
        public FakePpgDevice Device { get; }
        public ISerialPort Port { get; }

        public Rig()
        {
            Device = new FakePpgDevice(Line);
            Port = FakeSerialPort.Opener("COM6", Line)("COM6", new SerialConfig()).Port!;
        }

        public IReadOnlyList<PpgMessage> Received()
        {
            var buffer = new byte[4096];
            var parser = new PpgParser();
            var messages = new List<PpgMessage>();
            while (Port.Read(buffer, 0) is > 0 and var n) messages.AddRange(parser.Feed(buffer.AsSpan(0, n), 0));
            return messages;
        }

        public IReadOnlyList<PpgMessage> Exchange(byte[] command)
        {
            Assert.True(Port.Write(command, 100));
            Device.HandleCommands();
            return Received();
        }

        public byte[] Start(PpgStartRequest? request = null)
        {
            Assert.True(PpgWire.TryBuildStart(request ?? new PpgStartRequest(), out byte[]? command, out _));
            return command;
        }
    }

    // 시작에 상태 'v', 측정 중 다시 시작에 오류 's', 중지에 상태 'u', 정보 요청에 장치 정보로 답합니다(원본 시험과 같은 순서).
    [Fact]
    public void AnswersCommandsLikeDevice()
    {
        var rig = new Rig();

        PpgMessage started = Assert.Single(rig.Exchange(rig.Start()));
        Assert.Equal(PpgCode.Status, started.Code);
        Assert.Equal(new[] { PpgStatus.Preview }, started.Data);
        Assert.True(rig.Device.Running);
        Assert.Equal(1, rig.Device.StartCount);
        Assert.Equal(60, rig.Device.LastStartData[0]);

        PpgMessage again = Assert.Single(rig.Exchange(rig.Start()));
        Assert.Equal(PpgCode.Error, again.Code);
        Assert.Equal(new[] { (byte)'s' }, again.Data);
        Assert.Equal(1, rig.Device.StartCount);

        PpgMessage stopped = Assert.Single(rig.Exchange(PpgWire.BuildStop()));
        Assert.Equal(new[] { PpgStatus.Stopped }, stopped.Data);
        Assert.False(rig.Device.Running);
        Assert.Equal(1, rig.Device.StopCount);

        PpgMessage info = Assert.Single(rig.Exchange(PpgWire.BuildInfoRequest()));
        Assert.Equal(PpgCode.Info, info.Code);
        Assert.Equal("200000270270320", Encoding.ASCII.GetString(info.Data));
    }

    [Fact]
    public void Messages_AreFromDevice()
    {
        var rig = new Rig();

        PpgMessage message = Assert.Single(rig.Exchange(rig.Start()));

        Assert.Equal(PpgSource.Device, message.Source);
    }

    [Fact]
    public void Stop_WhenNotRunning_IsCountedWithoutAnswer()
    {
        var rig = new Rig();

        Assert.Empty(rig.Exchange(PpgWire.BuildStop()));

        Assert.Equal(1, rig.Device.StopCount);
        Assert.False(rig.Device.Running);
    }

    [Fact]
    public void LastStartData_KeepsRequestedValues()
    {
        var rig = new Rig();
        var request = new PpgStartRequest { PreviewS = 25, MeasureS = 150, Sensor = 'F', Gender = 'F', Age = 63, Reference = 'W' };

        rig.Exchange(rig.Start(request));

        Assert.Equal(new byte[] { 25, 150, (byte)'F', (byte)'F', 63, (byte)'W' }, rig.Device.LastStartData);
    }

    [Fact]
    public void HandleCommands_ReassemblesCommandSplitAcrossWrites()
    {
        var rig = new Rig();
        byte[] noisy = [0x00, 0xFF, 0x12, 0xFF, 0x55, 0xFF, .. rig.Start()];

        Assert.True(rig.Port.Write(noisy.AsSpan(0, 9), 100));
        Assert.Empty(rig.Device.HandleCommands());
        Assert.True(rig.Port.Write(noisy.AsSpan(9), 100));
        PpgMessage command = Assert.Single(rig.Device.HandleCommands());

        Assert.Equal((byte)'D', command.Code);
        Assert.Equal(1, rig.Device.StartCount);
    }

    [Fact]
    public void Booting_DoesNotAnswerUntilFinished()
    {
        var rig = new Rig();
        rig.Device.StartBooting();

        Assert.Empty(rig.Exchange(PpgWire.BuildInfoRequest()));
        Assert.Empty(rig.Exchange(rig.Start()));
        Assert.Equal(1, rig.Device.InfoRequestCount);
        Assert.False(rig.Device.Running);
        Assert.Equal(0, rig.Device.StartCount);

        rig.Device.FinishBooting();
        IReadOnlyList<PpgMessage> announced = rig.Received();
        Assert.Equal(new[] { PpgCode.Info, PpgCode.Status }, announced.Select(m => m.Code));
        Assert.Equal(new[] { PpgStatus.Boot }, announced[1].Data);

        Assert.Single(rig.Exchange(PpgWire.BuildInfoRequest()));
        Assert.Equal(PpgStatus.Preview, Assert.Single(rig.Exchange(rig.Start())).Data[0]);
    }

    [Fact]
    public void IgnoresBytesWithDeviceSource()
    {
        var rig = new Rig();
        byte[] fromDevice = rig.Start();
        fromDevice[1] = PpgSource.Device;
        ushort crc = PpgWire.Crc16(fromDevice.AsSpan(0, fromDevice.Length - 2));
        fromDevice[^2] = (byte)(crc & 0xFF);
        fromDevice[^1] = (byte)(crc >> 8);

        Assert.Empty(rig.Exchange(fromDevice));

        Assert.False(rig.Device.Running);
    }

    [Fact]
    public void Measurements_NumberSamplesFromEachStart()
    {
        var rig = new Rig();
        rig.Exchange(rig.Start());
        rig.Device.EmitMeasurement(24, 0, 0);
        rig.Device.EmitMeasurement(24, 72, 98, (byte)'f');
        PpgMeasure first = Assert.IsType<PpgMeasure>(PpgWire.DecodeMeasure(rig.Received().First(m => m.Code == PpgCode.Measure).Data));
        Assert.Equal(Enumerable.Range(0, 24).Select(i => FakePpgDevice.WaveAt(i)), first.Samples);

        rig.Exchange(PpgWire.BuildStop());
        rig.Exchange(rig.Start());
        rig.Device.EmitMeasurement(3, 70, 97);
        PpgMeasure restarted = Assert.IsType<PpgMeasure>(PpgWire.DecodeMeasure(rig.Received().Single(m => m.Code == PpgCode.Measure).Data));

        Assert.Equal(Enumerable.Range(0, 3).Select(i => FakePpgDevice.WaveAt(i)), restarted.Samples);
        Assert.Equal(70, restarted.HeartRate);
        Assert.Equal(97, restarted.Spo2);
    }

    [Fact]
    public void EmitMeasurement_CarriesVitalsAndError()
    {
        var rig = new Rig();

        rig.Device.EmitMeasurement(2, 72, 98, (byte)'f');

        PpgMeasure measure = Assert.IsType<PpgMeasure>(PpgWire.DecodeMeasure(Assert.Single(rig.Received()).Data));
        Assert.Equal(2, measure.Samples.Length);
        Assert.Equal(72, measure.HeartRate);
        Assert.Equal(98, measure.Spo2);
        Assert.Equal((byte)'f', measure.Error);
        Assert.False(rig.Device.Running);
    }

    [Theory]
    [InlineData((byte)'p', false)]
    [InlineData((byte)'u', true)]
    public void EmitStatus_EndsMeasurementOnlyForFinished(byte status, bool stillRunning)
    {
        var rig = new Rig();
        rig.Exchange(rig.Start());

        rig.Device.EmitStatus(status);

        Assert.Equal(stillRunning, rig.Device.Running);
        PpgMessage sent = Assert.Single(rig.Received());
        Assert.Equal(new[] { status }, sent.Data);
    }

    [Theory]
    [InlineData((byte)'f', false)]
    [InlineData((byte)'m', false)]
    [InlineData((byte)'x', true)]
    [InlineData((byte)'s', true)]
    public void EmitError_EndsMeasurementOnlyForFingerErrors(byte error, bool stillRunning)
    {
        var rig = new Rig();
        rig.Exchange(rig.Start());

        rig.Device.EmitError(error);

        Assert.Equal(stillRunning, rig.Device.Running);
        PpgMessage sent = Assert.Single(rig.Received());
        Assert.Equal(PpgCode.Error, sent.Code);
        Assert.Equal(new[] { error }, sent.Data);
    }

    [Fact]
    public void WaveAt_IsSineAroundMidScale()
    {
        Assert.Equal(2048, FakePpgDevice.WaveAt(0));
        Assert.Equal(2448, FakePpgDevice.WaveAt(104)); // 1/4주기 부근의 최대
        Assert.Equal(1648, FakePpgDevice.WaveAt(312)); // 3/4주기 부근의 최소
        for (long i = 0; i < 2000; i++)
        {
            ushort value = FakePpgDevice.WaveAt(i);
            Assert.InRange(value, 1648, 2448);
        }
    }
}
