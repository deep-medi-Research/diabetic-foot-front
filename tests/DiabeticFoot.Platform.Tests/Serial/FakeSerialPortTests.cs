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

    // 쓰기가 오면 바로, 시간이 지나면 그때, StopWaiting 뒤에는 기다리지 않고 돌아옵니다.
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
        Assert.Equal(1, line.WaitForWrites(0, far));

        Assert.Equal(1, line.WaitForWrites(1, TimeSpan.FromMilliseconds(30)));
        line.StopWaiting();
        var begin = Stopwatch.StartNew();
        Assert.Equal(1, line.WaitForWrites(1, far));
        Assert.True(begin.Elapsed < TimeSpan.FromSeconds(1), $"{begin.ElapsedMilliseconds}ms");
    }

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

    [Fact]
    public void Plug_ClearsInjectedFailure()
    {
        var line = new FakeSerialLine();
        line.FailWritesAfter(0, false);
        line.Plug();
        using var port = Open(line);

        Assert.True(port.Write([1], 100));
    }

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
