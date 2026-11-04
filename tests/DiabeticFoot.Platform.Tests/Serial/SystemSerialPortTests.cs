using System.Diagnostics;

namespace DiabeticFoot.Platform.Tests;

/// <summary>
/// System.IO.Ports로 연 포트를 가상 단말로 시험합니다. 가상 단말은 모뎀 제어선(DTR, RTS)이 없어 그 설정은 시험하지 못합니다.
/// </summary>
public class SystemSerialPortTests
{
    static ISerialPort Open(PseudoTerminal pty)
    {
        SerialOpenResult result = SystemSerialPort.Open(pty.SlavePath, new SerialConfig(), assertModemLines: false);
        Assert.True(result.Port is not null, result.Error);
        return result.Port;
    }

    static byte[] AllByteValues() => Enumerable.Range(0, 256).Select(i => (byte)i).ToArray();

    // 포트에 쓴 바이트가 장치 쪽에 그대로 나옵니다. 0x00~0xFF 모두 바뀌지 않습니다(8비트 raw).
    [UnixFact]
    public void Write_DeliversEveryByteValueUnchanged()
    {
        using var pty = new PseudoTerminal();
        using var port = Open(pty);

        Assert.True(port.Write(AllByteValues(), 1000));

        Assert.Equal(AllByteValues(), pty.Receive(256, 2000));
    }

    // 장치가 보낸 바이트를 그대로 읽습니다.
    [UnixFact]
    public void Read_ReturnsEveryByteValueUnchanged()
    {
        using var pty = new PseudoTerminal();
        using var port = Open(pty);
        pty.Send(AllByteValues());
        var received = new List<byte>();
        var buffer = new byte[300];

        var deadline = DateTime.UtcNow.AddSeconds(3);
        while (received.Count < 256 && DateTime.UtcNow < deadline)
        {
            int n = port.Read(buffer, 200) ?? throw new InvalidOperationException("포트가 닫혔습니다");
            received.AddRange(buffer.AsSpan(0, n).ToArray());
        }

        Assert.Equal(AllByteValues(), received);
    }

    // 받은 바이트가 있으면 기다리지 않고 버퍼 크기까지 돌려주고, 나머지는 다음 읽기에 남깁니다.
    [UnixFact]
    public void Read_ReturnsAvailableBytesUpToBuffer()
    {
        using var pty = new PseudoTerminal();
        using var port = Open(pty);
        pty.Send([1, 2, 3, 4, 5]);
        Thread.Sleep(100);
        var buffer = new byte[3];

        var watch = Stopwatch.StartNew();
        Assert.Equal(3, port.Read(buffer, 2000));
        Assert.Equal(new byte[] { 1, 2, 3 }, buffer);
        Assert.Equal(2, port.Read(buffer, 2000));
        Assert.Equal(new byte[] { 4, 5, 3 }, buffer);
        Assert.True(watch.ElapsedMilliseconds < 500, $"{watch.ElapsedMilliseconds}ms");
    }

    // 받은 바이트가 없으면 시간 제한까지 기다린 뒤 0을 돌려주고, 제한이 0이면 기다리지 않습니다.
    [UnixFact]
    public void Read_TimesOutWithZero()
    {
        using var pty = new PseudoTerminal();
        using var port = Open(pty);
        var buffer = new byte[8];

        var immediate = Stopwatch.StartNew();
        Assert.Equal(0, port.Read(buffer, 0));
        Assert.True(immediate.ElapsedMilliseconds < 50, $"{immediate.ElapsedMilliseconds}ms");

        var watch = Stopwatch.StartNew();
        Assert.Equal(0, port.Read(buffer, 100));
        Assert.InRange(watch.ElapsedMilliseconds, 90, 400);
        Assert.True(port.IsOpen);
    }

    // 기다리는 동안 바이트가 오면 시간 제한 전에 돌려줍니다.
    [UnixFact]
    public void Read_WakesWhenBytesArrive()
    {
        using var pty = new PseudoTerminal();
        using var port = Open(pty);
        var buffer = new byte[8];
        var device = new Thread(() =>
        {
            Thread.Sleep(100);
            pty.Send([9]);
        });
        device.Start();

        var watch = Stopwatch.StartNew();
        Assert.Equal(1, port.Read(buffer, 5000));
        Assert.InRange(watch.ElapsedMilliseconds, 90, 400);
        Assert.Equal(9, buffer[0]);
        device.Join();
    }

    // WakeRead는 기다리는 Read를 바로 돌아오게 하고 포트는 닫지 않습니다.
    [UnixFact]
    public void WakeRead_ReturnsWaitingRead()
    {
        using var pty = new PseudoTerminal();
        using var port = Open(pty);
        var buffer = new byte[16];
        var waker = new Thread(() =>
        {
            Thread.Sleep(100);
            port.WakeRead();
        });
        waker.Start();

        var watch = Stopwatch.StartNew();
        Assert.Equal(0, port.Read(buffer, 5000));
        Assert.InRange(watch.ElapsedMilliseconds, 90, 1000);
        waker.Join();
        Assert.True(port.IsOpen);
    }

    // 기다리는 Read가 없을 때 부른 WakeRead는 다음 Read 한 번이 기다리지 않게 하고, 그 다음 Read는 다시 기다립니다.
    [UnixFact]
    public void WakeRead_WithoutWaiterSkipsNextWaitOnly()
    {
        using var pty = new PseudoTerminal();
        using var port = Open(pty);
        var buffer = new byte[16];

        port.WakeRead();
        var first = Stopwatch.StartNew();
        Assert.Equal(0, port.Read(buffer, 5000));
        Assert.True(first.ElapsedMilliseconds < 500, $"{first.ElapsedMilliseconds}ms");

        var second = Stopwatch.StartNew();
        Assert.Equal(0, port.Read(buffer, 100));
        Assert.InRange(second.ElapsedMilliseconds, 90, 400);
    }

    // 이미 받은 바이트가 있으면 WakeRead가 있어도 그 바이트를 돌려줍니다.
    [UnixFact]
    public void WakeRead_DoesNotHideReceivedBytes()
    {
        using var pty = new PseudoTerminal();
        using var port = Open(pty);
        pty.Send([7, 8]);
        Thread.Sleep(100);
        var buffer = new byte[16];

        port.WakeRead();

        Assert.Equal(2, port.Read(buffer, 1000));
    }

    // 한 스레드가 Read로 기다리는 동안 다른 스레드가 Write를 할 수 있습니다.
    [UnixFact]
    public void Write_WhileAnotherThreadReads()
    {
        using var pty = new PseudoTerminal();
        using var port = Open(pty);
        int? read = null;
        var reader = new Thread(() => read = port.Read(new byte[8], 3000));
        reader.Start();
        Thread.Sleep(100);

        Assert.True(port.Write([0xAA, 0xBB], 1000));
        Assert.Equal(new byte[] { 0xAA, 0xBB }, pty.Receive(2, 2000));

        pty.Send([1]);
        Assert.True(reader.Join(3000));
        Assert.Equal(1, read);
    }

    // 닫으면 기다리던 Read가 바로 돌아오고, 이후 읽기와 쓰기는 실패합니다. 이름은 닫은 뒤에도 같습니다.
    [UnixFact]
    public void Close_ReturnsWaitingReadAndStopsIo()
    {
        using var pty = new PseudoTerminal();
        var port = Open(pty);
        string name = port.Name;
        int? read = -1;
        var reader = new Thread(() => read = port.Read(new byte[8], 10000));
        reader.Start();
        Thread.Sleep(100);

        var watch = Stopwatch.StartNew();
        port.Close();

        Assert.True(reader.Join(2000));
        Assert.True(watch.ElapsedMilliseconds < 2000, $"{watch.ElapsedMilliseconds}ms");
        Assert.True(read is null or 0, $"{read}");
        Assert.False(port.IsOpen);
        Assert.Null(port.Read(new byte[8], 0));
        Assert.False(port.Write([1], 100));
        Assert.Equal(name, port.Name);
        port.Close();
        port.WakeRead();
    }

    // 닫은 포트를 다시 열 수 있습니다. 앞 포트가 쥐고 있던 장치가 풀려 있어야 합니다.
    [UnixFact]
    public void Reopen_AfterClose()
    {
        using var pty = new PseudoTerminal();
        Open(pty).Close();
        using var port = Open(pty);

        Assert.True(port.Write([5], 1000));

        Assert.Equal(new byte[] { 5 }, pty.Receive(1, 2000));
    }

    // 없는 포트는 열지 못하고 이유를 돌려줍니다.
    [UnixFact]
    public void Open_FailsForMissingPort()
    {
        SerialOpenResult result = SystemSerialPort.Open("/dev/ttys999", new SerialConfig(), assertModemLines: false);

        Assert.Null(result.Port);
        Assert.Contains("/dev/ttys999", result.Error);
    }

    // 다른 프로그램이 쓰는 이름이거나 형식이 맞지 않는 이름도 예외 대신 열기 실패로 알립니다.
    [Fact]
    public void Open_FailsForInvalidName()
    {
        SerialOpenResult result = SystemSerialPort.Open("not-a-port", new SerialConfig());

        Assert.Null(result.Port);
        Assert.Contains("not-a-port", result.Error);
    }

    // 읽고 쓰는 중에 닫는 일을 시점을 바꿔 되풀이해도 멈추거나 예외가 나지 않습니다.
    // 장치 쪽이 읽지 않으면 System.IO.Ports의 Close가 보낼 바이트가 빠질 때까지 돌아오지 않으므로, 장치 쪽이 계속 읽습니다.
    [UnixFact]
    public void Close_RacesWithReadAndWrite()
    {
        for (int round = 0; round < 40; round++)
        {
            using var pty = new PseudoTerminal();
            var port = Open(pty);
            bool done = false;
            var drainer = new Thread(() => { while (!Volatile.Read(ref done)) pty.Receive(256, 20); });
            var reader = new Thread(() => { while (port.Read(new byte[16], 1000) is not null) { } });
            var writer = new Thread(() => { while (port.Write([1], 100)) { } });
            drainer.Start();
            reader.Start();
            writer.Start();
            Thread.Sleep(round % 8);

            port.Close();

            Assert.True(reader.Join(3000), $"round {round}: Read가 돌아오지 않았습니다");
            Assert.True(writer.Join(3000), $"round {round}: Write가 돌아오지 않았습니다");
            Volatile.Write(ref done, true);
            drainer.Join();
        }
    }
}
