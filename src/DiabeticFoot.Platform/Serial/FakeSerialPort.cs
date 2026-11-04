using System.Diagnostics;

namespace DiabeticFoot.Platform;

/// <summary>
/// 가짜 직렬 포트의 장치 쪽입니다. 테스트가 쥐고 있으면서 쓴 바이트를 보고, 받을 바이트를 넣고, 실패를 주입합니다.
/// 포트를 닫거나 다시 열어도 같은 회선을 계속 씁니다. 여러 스레드에서 불러도 됩니다.
/// </summary>
public sealed class FakeSerialLine
{
    // 회선과 이 회선으로 연 모든 FakeSerialPort가 한 잠금을 씁니다. 대기 조건이 서로 다른 스레드가 한 잠금을 기다리므로
    // 상태가 바뀔 때마다 PulseAll로 모두 깨우고, 깨어난 쪽이 자기 조건을 다시 확인합니다.
    internal readonly object Gate = new();
    internal readonly Queue<byte> Inbound = new();
    internal bool Plugged = true;
    internal bool WritesHeld;
    internal int Opens;

    readonly List<byte[]> _writes = [];
    int _writesBeforeFailure = -1;
    bool _failureDisconnects;
    bool _stopWaiting;

    /// <summary>지금까지 포트에 쓴 모든 바이트입니다(열고 닫은 횟수와 무관하게 이어 붙임).</summary>
    public byte[] Written()
    {
        lock (Gate) return _writes.SelectMany(write => write).ToArray();
    }

    /// <summary>성공한 Write 호출마다 나눈 바이트입니다.</summary>
    public IReadOnlyList<byte[]> Writes()
    {
        lock (Gate) return _writes.ToArray();
    }

    /// <summary>장치가 보낸 바이트를 넣습니다. Read가 이 순서대로 돌려줍니다.</summary>
    public void Feed(ReadOnlySpan<byte> bytes)
    {
        lock (Gate)
        {
            foreach (byte b in bytes) Inbound.Enqueue(b);
            Monitor.PulseAll(Gate);
        }
    }

    /// <summary>
    /// 이 횟수만큼 Write가 더 성공한 뒤부터 쓰기를 실패시킵니다. -1이면 실패시키지 않습니다.
    /// </summary>
    /// <param name="writes">더 성공할 Write 횟수입니다. 0이면 다음 Write부터 실패합니다.</param>
    /// <param name="disconnect">true면 장치가 빠진 것처럼 포트를 닫고, false면 시간 초과처럼 false만 돌려줍니다.</param>
    public void FailWritesAfter(int writes, bool disconnect)
    {
        lock (Gate)
        {
            _writesBeforeFailure = writes;
            _failureDisconnects = disconnect;
        }
    }

    /// <summary>장치를 뺍니다. 열린 포트는 다음 Write나 Read에서 닫히고, 이후 열기는 실패합니다.</summary>
    public void Unplug()
    {
        lock (Gate)
        {
            Plugged = false;
            Monitor.PulseAll(Gate);
        }
    }

    /// <summary>장치를 다시 꽂습니다. 실패 주입도 지웁니다.</summary>
    public void Plug()
    {
        lock (Gate)
        {
            Plugged = true;
            _writesBeforeFailure = -1;
        }
    }

    /// <summary>이 회선으로 포트를 연 횟수입니다.</summary>
    public int OpenCount
    {
        get
        {
            lock (Gate) return Opens;
        }
    }

    /// <summary>
    /// 성공한 Write 수가 seen보다 많아지거나, timeout이 지나거나, StopWaiting을 부를 때까지 기다립니다.
    /// 가짜 장치가 명령을 반복 확인하지 않고 다음 메시지 시각까지 기다리게 하기 위함입니다.
    /// </summary>
    /// <param name="seen">부른 쪽이 이미 본 Write 수입니다.</param>
    /// <param name="timeout">기다리는 최대 시간입니다.</param>
    /// <returns>지금까지 성공한 Write 수입니다.</returns>
    public int WaitForWrites(int seen, TimeSpan timeout)
    {
        lock (Gate)
        {
            WaitUntil(() => _stopWaiting || _writes.Count > seen, timeout);
            return _writes.Count;
        }
    }

    /// <summary>WaitForWrites로 기다리는 쪽을 깨우고, 이후의 WaitForWrites는 기다리지 않습니다. 가짜 장치를 끝낼 때 씁니다.</summary>
    public void StopWaiting()
    {
        lock (Gate)
        {
            _stopWaiting = true;
            Monitor.PulseAll(Gate);
        }
    }

    /// <summary>
    /// 호출 뒤 Write가 ReleaseWrites를 부를 때까지 돌아오지 않습니다. 드라이버가 멈춰 쓰기가 끝나지 않는 경우를 시험합니다.
    /// 2026-10-20 기존 앱 실기에서 FTDI 드라이버가 멈춰 쓰기와 닫기가 돌아오지 않았습니다.
    /// </summary>
    public void HoldWrites()
    {
        lock (Gate) WritesHeld = true;
    }

    /// <summary>HoldWrites로 멈춘 Write를 모두 돌려보냅니다. 이후 Write는 멈추지 않습니다.</summary>
    public void ReleaseWrites()
    {
        lock (Gate)
        {
            WritesHeld = false;
            Monitor.PulseAll(Gate);
        }
    }

    // Gate를 잡고 부릅니다. 쓰기를 받아들였으면 true이고, 실패시키면 false와 함께 포트를 닫을지를 disconnect로 알립니다.
    internal bool TryAcceptWrite(ReadOnlySpan<byte> bytes, out bool disconnect)
    {
        disconnect = false;
        if (_writesBeforeFailure == 0)
        {
            disconnect = _failureDisconnects;
            return false;
        }
        if (_writesBeforeFailure > 0) --_writesBeforeFailure;
        _writes.Add(bytes.ToArray());
        Monitor.PulseAll(Gate);
        return true;
    }

    // Gate를 잡고 부릅니다. 조건이 참이 되거나 시간이 지나면 돌아오며, 돌아온 시점의 조건 값을 돌려줍니다.
    internal bool WaitUntil(Func<bool> condition, TimeSpan timeout)
    {
        long start = Stopwatch.GetTimestamp();
        while (!condition())
        {
            TimeSpan remaining = timeout - Stopwatch.GetElapsedTime(start);
            if (remaining <= TimeSpan.Zero) return condition();
            Monitor.Wait(Gate, remaining);
        }
        return true;
    }
}

/// <summary>장비 없이 직렬 포트 사용자를 시험하는 포트입니다. 장치 쪽 동작은 FakeSerialLine이 정합니다.</summary>
public sealed class FakeSerialPort : ISerialPort
{
    readonly FakeSerialLine _line;
    // 아래 둘은 _line.Gate 아래에서 읽고 씁니다. _woken은 WakeRead를 불렀고 아직 Read가 돌아오지 않았음을 뜻합니다.
    bool _open = true;
    bool _woken;

    public FakeSerialPort(string name, FakeSerialLine line)
    {
        Name = name;
        _line = line;
    }

    public string Name { get; }

    public bool IsOpen
    {
        get
        {
            lock (_line.Gate) return _open;
        }
    }

    public bool Write(ReadOnlySpan<byte> bytes, int timeoutMs)
    {
        lock (_line.Gate)
        {
            // 멈춘 드라이버처럼 시간 제한과 상관없이 풀어 줄 때까지 돌아오지 않습니다.
            while (_line.WritesHeld) Monitor.Wait(_line.Gate);
            if (!_line.Plugged) _open = false;
            if (!_open) return false;
            if (_line.TryAcceptWrite(bytes, out bool disconnect)) return true;
            if (disconnect) _open = false;
            return false;
        }
    }

    public int? Read(Span<byte> buffer, int timeoutMs)
    {
        lock (_line.Gate)
        {
            _line.WaitUntil(
                () => _woken || !_open || !_line.Plugged || _line.Inbound.Count > 0,
                TimeSpan.FromMilliseconds(Math.Max(timeoutMs, 0)));
            _woken = false;
            if (!_line.Plugged) _open = false;
            if (!_open) return null;
            int count = Math.Min(buffer.Length, _line.Inbound.Count);
            for (int i = 0; i < count; i++) buffer[i] = _line.Inbound.Dequeue();
            return count;
        }
    }

    public void WakeRead()
    {
        lock (_line.Gate)
        {
            _woken = true;
            Monitor.PulseAll(_line.Gate);
        }
    }

    public void Close()
    {
        lock (_line.Gate)
        {
            _open = false;
            Monitor.PulseAll(_line.Gate);
        }
    }

    public void Dispose() => Close();

    /// <summary>
    /// 이름이 name인 포트만 line으로 여는 함수를 만듭니다. 다른 이름이나 빠진 장치는 열기에 실패합니다.
    /// </summary>
    public static SerialPortOpener Opener(string name, FakeSerialLine line) =>
        (requested, config) =>
        {
            lock (line.Gate)
            {
                if (requested != name || !line.Plugged) return SerialOpenResult.Failed($"{requested} 포트가 없습니다");
                line.Opens++;
            }
            return SerialOpenResult.Opened(new FakeSerialPort(name, line));
        };
}
