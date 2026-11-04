namespace DiabeticFoot.Platform.Tests;

public class LightCommandTests
{
    [Fact]
    public void PercentIsSecondByte()
    {
        Assert.Equal(new byte[] { 0xFE, 0x64 }, LightProtocol.MakeCommand(100));
        Assert.Equal(new byte[] { 0xFE, 0x00 }, LightProtocol.MakeCommand(0));
        Assert.Equal(new byte[] { 0xFE, 37 }, LightProtocol.MakeCommand(37));
    }

    [Fact]
    public void ClampsOutOfRange()
    {
        Assert.Equal(new byte[] { 0xFE, 0x64 }, LightProtocol.MakeCommand(150));
        Assert.Equal(new byte[] { 0xFE, 0x00 }, LightProtocol.MakeCommand(-5));
        Assert.Equal(new byte[] { 0xFE, 0x64 }, LightProtocol.MakeCommand(176));
    }

    // 0~100의 둘째 바이트에는 단계 명령(B0, B1)과 같은 값이 없습니다.
    [Fact]
    public void NeverProducesStepCommand()
    {
        for (int percent = -300; percent <= 300; ++percent)
        {
            byte[] command = LightProtocol.MakeCommand(percent);
            Assert.Equal(2, command.Length);
            Assert.Equal(0xFE, command[0]);
            Assert.True(command[1] <= 0x64, $"{percent}");
        }
    }
}

public class LightTests
{
    sealed class Rig
    {
        public FakeSerialLine Line { get; } = new();
        public Light Light { get; }

        public Rig() => Light = new Light(FakeSerialPort.Opener("COM7", Line));
    }

    [Fact]
    public void Open_SendsCurrentPercent()
    {
        var rig = new Rig();

        Assert.True(rig.Light.Open("COM7", 70, out string? error), error);

        Assert.Equal(new[] { new byte[] { 0xFE, 70 } }, rig.Line.Writes());
        Assert.True(rig.Light.IsConnected);
        Assert.Equal("COM7", rig.Light.PortName);
        Assert.Equal(70, rig.Light.LastSentPercent);
    }

    [Fact]
    public void Open_ClampsPercent()
    {
        var rig = new Rig();

        Assert.True(rig.Light.Open("COM7", 250, out _));

        Assert.Equal(new byte[] { 0xFE, 0x64 }, rig.Line.Written());
        Assert.Equal(100, rig.Light.LastSentPercent);
    }

    [Fact]
    public void SetPercent_SendsAbsoluteCommand()
    {
        var rig = new Rig();
        Assert.True(rig.Light.Open("COM7", 50, out _));

        Assert.True(rig.Light.SetPercent(20, out _));
        Assert.True(rig.Light.SetPercent(-1, out _));

        Assert.Equal(
            new[] { new byte[] { 0xFE, 50 }, new byte[] { 0xFE, 20 }, new byte[] { 0xFE, 0 } },
            rig.Line.Writes());
        Assert.Equal(0, rig.Light.LastSentPercent);
    }

    [Fact]
    public void Open_FailsForUnknownPort()
    {
        var rig = new Rig();

        Assert.False(rig.Light.Open("COM9", 50, out string? error));

        Assert.NotEmpty(error);
        Assert.False(rig.Light.IsConnected);
        Assert.Null(rig.Light.LastSentPercent);
        Assert.Empty(rig.Line.Written());
    }

    [Fact]
    public void SetPercent_FailsWhenNotConnected()
    {
        var rig = new Rig();

        Assert.False(rig.Light.SetPercent(30, out string? error));

        Assert.NotEmpty(error);
        Assert.Null(rig.Light.LastSentPercent);
    }

    [Fact]
    public void WriteFailure_Disconnects()
    {
        var rig = new Rig();
        Assert.True(rig.Light.Open("COM7", 50, out _));
        rig.Line.FailWritesAfter(0, false);

        Assert.False(rig.Light.SetPercent(60, out string? error));

        Assert.NotEmpty(error);
        Assert.False(rig.Light.IsConnected);
        Assert.Null(rig.Light.LastSentPercent);
        Assert.Equal("", rig.Light.PortName);
        Assert.False(rig.Light.SetPercent(70, out _));
        Assert.Equal(new[] { new byte[] { 0xFE, 50 } }, rig.Line.Writes());
    }

    [Fact]
    public void Reopen_AfterUnplug_SendsAgain()
    {
        var rig = new Rig();
        Assert.True(rig.Light.Open("COM7", 40, out _));
        rig.Line.Unplug();

        Assert.False(rig.Light.SetPercent(45, out _));
        Assert.False(rig.Light.IsConnected);
        Assert.False(rig.Light.Open("COM7", 45, out _));

        rig.Line.Plug();
        Assert.True(rig.Light.Open("COM7", 45, out _));
        Assert.Equal(new[] { new byte[] { 0xFE, 40 }, new byte[] { 0xFE, 45 } }, rig.Line.Writes());
        Assert.Equal(2, rig.Line.OpenCount);
    }

    [Fact]
    public void Open_FailsWhenFirstWriteFails()
    {
        var rig = new Rig();
        rig.Line.FailWritesAfter(0, true);

        Assert.False(rig.Light.Open("COM7", 50, out _));

        Assert.False(rig.Light.IsConnected);
        Assert.Null(rig.Light.LastSentPercent);
    }

    // 사이드카에 근거 없는 값을 남기지 않기 위해, 닫으면 마지막으로 보낸 값이 없어집니다.
    [Fact]
    public void Close_ForgetsLastSent()
    {
        var rig = new Rig();
        Assert.True(rig.Light.Open("COM7", 80, out _));

        rig.Light.Close();

        Assert.False(rig.Light.IsConnected);
        Assert.Null(rig.Light.LastSentPercent);
    }

    // 닫아도 밝기를 바꾸는 명령을 보내지 않으므로 LED는 마지막 밝기로 남습니다.
    [Fact]
    public void Close_SendsNothing()
    {
        var rig = new Rig();
        Assert.True(rig.Light.Open("COM7", 80, out _));

        rig.Light.Dispose();

        Assert.Equal(new[] { new byte[] { 0xFE, 80 } }, rig.Line.Writes());
    }

    [Fact]
    public void Open_ClosesPreviousPortFirst()
    {
        var rig = new Rig();
        Assert.True(rig.Light.Open("COM7", 80, out _));

        Assert.False(rig.Light.Open("COM9", 30, out string? error));

        Assert.NotEmpty(error);
        Assert.False(rig.Light.IsConnected);
        Assert.Null(rig.Light.LastSentPercent);
    }
}

public class LightPortIdentityTests
{
    static SerialPortInfo Port(string name, ushort vid, ushort pid, string serial) =>
        new(name, "USB Serial Port", vid, pid, serial);

    [Fact]
    public void FindPort_MatchesSerialAmongSameVidPid()
    {
        SerialPortInfo[] ports = [Port("COM6", 0x0403, 0x6001, "PPG0001A"), Port("COM9", 0x0403, 0x6001, "LED0001A")];

        Assert.Equal("COM9", new LightPortIdentity(0x0403, 0x6001, "LED0001A").FindPort(ports));
        Assert.Null(new LightPortIdentity(0x0403, 0x6001, "OTHER").FindPort(ports));
    }

    [Fact]
    public void FindPort_WithoutSerialRequiresSingleVidPid()
    {
        var remembered = new LightPortIdentity(0x1A86, 0x7523, "");
        SerialPortInfo[] one = [Port("COM3", 0x1A86, 0x7523, ""), Port("COM6", 0x0403, 0x6001, "X")];
        SerialPortInfo[] two = [Port("COM3", 0x1A86, 0x7523, ""), Port("COM4", 0x1A86, 0x7523, "")];

        Assert.Equal("COM3", remembered.FindPort(one));
        Assert.Null(remembered.FindPort(two));
    }

    [Fact]
    public void FindPort_SkipsNonUsbPorts()
    {
        var com1 = new SerialPortInfo("COM1", "Communications Port", null, null, "");

        Assert.Null(LightPortIdentity.Of(com1));
        Assert.Null(new LightPortIdentity(0, 0, "").FindPort([com1]));
        Assert.Equal(
            new LightPortIdentity(0x0403, 0x6001, "LED0001A"),
            LightPortIdentity.Of(Port("COM9", 0x0403, 0x6001, "LED0001A")));
    }
}
