using System.Diagnostics;

namespace DiabeticFoot.Platform.Tests;

public class FakeSerialPortTests
{
    static ISerialPort Open(FakeSerialLine line, string name = "COM3")
    {
        SerialOpenResult result = FakeSerialPort.Opener(name, line)(name, new SerialConfig());
        Assert.NotNull(result.Port);
        return result.Port;
    }

    // 받은 바이트가 있으면 기다리지 않고 버퍼 크기까지 돌려주고, 나머지는 다음 읽기에 남깁니다.
    [Fact]
    public void Read_ReturnsAvailableBytesUpToBuffer()
    {
        var line = new FakeSerialLine();
        using var port = Open(line);
        line.Feed([1, 2, 3, 4, 5]);
        var buffer = new byte[3];

        Assert.Equal(3, port.Read(buffer, 1000));
        Assert.Equal(new byte[] { 1, 2, 3 }, buffer);
        Assert.Equal(2, port.Read(buffer, 1000));
        Assert.Equal(4, buffer[0]);
        Assert.Equal(5, buffer[1]);
    }

    // 받은 바이트가 없으면 시간 제한까지 기다린 뒤 0을 돌려줍니다.
    [Fact]
    public void Read_TimesOutWithZero()
    {
        var line = new FakeSerialLine();
        using var port = Open(line);
        var buffer = new byte[8];

        Assert.Equal(0, port.Read(buffer, 0));
        var watch = Stopwatch.StartNew();
        Assert.Equal(0, port.Read(buffer, 20));
        Assert.True(watch.ElapsedMilliseconds >= 15, $"{watch.ElapsedMilliseconds}ms");
    }

    // 기다리는 동안 바이트가 오면 시간 제한 전에 돌려줍니다.
    [Fact]
    public void Read_WakesWhenBytesArrive()
    {
        var line = new FakeSerialLine();
        using var port = Open(line);
        var buffer = new byte[8];
        var device = new Thread(() =>
        {
            Thread.Sleep(30);
            line.Feed([9]);
        });
        device.Start();

        var watch = Stopwatch.StartNew();
        Assert.Equal(1, port.Read(buffer, 5000));
        Assert.True(watch.Elapsed < TimeSpan.FromSeconds(2), $"{watch.ElapsedMilliseconds}ms");
        device.Join();
    }

    // 장치가 빠지면 읽기는 null을 돌려주고 포트는 닫힙니다.
    [Fact]
    public void Read_ReportsUnplug()
    {
        var line = new FakeSerialLine();
        using var port = Open(line);
        var buffer = new byte[8];
        line.Unplug();

        Assert.Null(port.Read(buffer, 1000));
        Assert.False(port.IsOpen);
    }

    // WakeRead는 기다리는 Read를 바로 돌아오게 하고, 기다리는 Read가 없으면 다음 Read 한 번이 기다리지 않습니다.
    [Fact]
    public void WakeRead_ReturnsWaitingRead()
    {
        var line = new FakeSerialLine();
        using var port = Open(line);
        var buffer = new byte[16];

        var begin = Stopwatch.StartNew();
        var waker = new Thread(() =>
        {
            Thread.Sleep(50);
            port.WakeRead();
        });
        waker.Start();
        Assert.Equal(0, port.Read(buffer, 5000));
        waker.Join();
        Assert.True(begin.Elapsed < TimeSpan.FromSeconds(2), $"{begin.ElapsedMilliseconds}ms");
        Assert.True(port.IsOpen);

        port.WakeRead();
        var again = Stopwatch.StartNew();
        Assert.Equal(0, port.Read(buffer, 5000));
        Assert.True(again.Elapsed < TimeSpan.FromSeconds(1), $"{again.ElapsedMilliseconds}ms");
    }

    // 가짜 장치는 쓰기 알림을 다음 메시지 시각까지 기다립니다. 쓰기가 오면 바로, StopWaiting 뒤에는 기다리지 않고 돌아옵니다.
    [Fact]
    public void WaitForWrites_WakesOnWrite()
    {
        var line = new FakeSerialLine();
        using var port = Open(line);
        var far = TimeSpan.FromSeconds(10);

        var writer = new Thread(() =>
        {
            Thread.Sleep(50);
            port.Write([0xFF], 100);
        });
        writer.Start();
        Assert.Equal(1, line.WaitForWrites(0, far));
        writer.Join();
        Assert.Equal(1, line.WaitForWrites(0, far)); // 이미 본 수보다 많으면 기다리지 않음

        Assert.Equal(1, line.WaitForWrites(1, TimeSpan.FromMilliseconds(30))); // 시간이 지나면 돌아옴
        line.StopWaiting();
        var begin = Stopwatch.StartNew();
        Assert.Equal(1, line.WaitForWrites(1, far));
        Assert.True(begin.Elapsed < TimeSpan.FromSeconds(1), $"{begin.ElapsedMilliseconds}ms");
    }

    // 쓴 바이트는 호출마다 나눠서도, 이어 붙여서도 볼 수 있고, 포트를 닫고 다시 열어도 이어집니다.
    [Fact]
    public void Writes_AreRecordedAcrossReopen()
    {
        var line = new FakeSerialLine();
        var first = Open(line);
        Assert.True(first.Write([1, 2], 100));
        first.Close();
        using var second = Open(line);
        Assert.True(second.Write([3], 100));

        Assert.Equal(new[] { new byte[] { 1, 2 }, new byte[] { 3 } }, line.Writes());
        Assert.Equal(new byte[] { 1, 2, 3 }, line.Written());
        Assert.Equal(2, line.OpenCount);
    }

    // 정해 둔 횟수만큼 쓰기가 성공한 뒤 실패합니다. 시간 초과 흉내는 포트를 닫지 않고, 장치 빠짐 흉내는 닫습니다.
    [Theory]
    [InlineData(false, true)]
    [InlineData(true, false)]
    public void FailWritesAfter_FailsAfterGivenSuccesses(bool disconnect, bool stillOpen)
    {
        var line = new FakeSerialLine();
        using var port = Open(line);
        line.FailWritesAfter(1, disconnect);

        Assert.True(port.Write([1], 100));
        Assert.False(port.Write([2], 100));

        Assert.Equal(stillOpen, port.IsOpen);
        Assert.Equal(new byte[] { 1 }, line.Written());
    }

    // 장치를 다시 꽂으면 실패 주입이 지워집니다.
    [Fact]
    public void Plug_ClearsInjectedFailure()
    {
        var line = new FakeSerialLine();
        line.FailWritesAfter(0, false);
        line.Plug();
        using var port = Open(line);

        Assert.True(port.Write([1], 100));
    }

    // 이름이 다르거나 장치가 빠져 있으면 열지 못하고 이유를 돌려줍니다.
    [Fact]
    public void Opener_FailsForOtherNameOrUnpluggedLine()
    {
        var line = new FakeSerialLine();
        var opener = FakeSerialPort.Opener("COM7", line);

        SerialOpenResult other = opener("COM9", new SerialConfig());
        Assert.Null(other.Port);
        Assert.NotEmpty(other.Error);

        line.Unplug();
        Assert.Null(opener("COM7", new SerialConfig()).Port);
        Assert.Equal(0, line.OpenCount);
    }

    // 닫은 포트는 쓰지도 읽지도 못합니다. 닫기를 두 번 불러도 됩니다.
    [Fact]
    public void Close_IsIdempotentAndStopsIo()
    {
        var line = new FakeSerialLine();
        var port = Open(line);

        port.Close();
        port.Close();

        Assert.False(port.IsOpen);
        Assert.False(port.Write([1], 100));
        Assert.Null(port.Read(new byte[1], 0));
        Assert.Equal("COM3", port.Name);
    }

    // HoldWrites 동안 Write는 시간 제한과 상관없이 돌아오지 않고, ReleaseWrites가 보내면 쓰기가 성공합니다.
    [Fact]
    public void HoldWrites_BlocksUntilReleased()
    {
        var line = new FakeSerialLine();
        using var port = Open(line);
        line.HoldWrites();

        bool written = false;
        var write = new Thread(() => written = port.Write([7], 10));
        write.Start();
        Assert.False(write.Join(200));

        line.ReleaseWrites();
        Assert.True(write.Join(TimeSpan.FromSeconds(5)));
        Assert.True(written);
        Assert.Equal(new byte[] { 7 }, line.Written());
        Assert.True(port.Write([8], 10));
    }
}
