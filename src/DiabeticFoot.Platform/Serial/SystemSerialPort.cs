using System.Diagnostics;
using System.IO.Ports;

namespace DiabeticFoot.Platform;

/// <summary>
/// System.IO.Ports로 연 직렬 포트입니다. 읽기는 한 스레드, 쓰기는 다른 한 스레드에서 동시에 불러도 됩니다.
/// 같은 쪽을 두 스레드에서 동시에 부르는 것은 지원하지 않습니다.
/// </summary>
/// <remarks>
/// System.IO.Ports에는 기다리는 읽기를 깨우는 방법이 없고 시간 초과를 예외로 알립니다. 그래서 Read는 받은 바이트가 있는지
/// (BytesToRead)를 보고, 없으면 DataReceived와 WakeRead 알림을 기다립니다.
/// </remarks>
public sealed class SystemSerialPort : ISerialPort
{
    // DataReceived 알림이 빠져도 새 바이트를 이 시간 안에 알아채도록 기다림을 나눕니다. 장치를 뽑았을 때도 이 간격으로
    // BytesToRead가 오류를 내어 읽기가 실패를 알게 됩니다.
    const int PollSliceMs = 10;

    readonly SerialPort _port;
    readonly SerialDataReceivedEventHandler _onData;

    readonly object _gate = new();
    int _inFlight;
    bool _open = true;
    bool _closing;

    readonly object _signal = new();
    bool _dataSignaled;
    bool _wakePending;

    byte[] _scratch = new byte[256];

    SystemSerialPort(string name, SerialPort port)
    {
        Name = name;
        _port = port;
        _onData = (_, _) => Signal(data: true);
        _port.DataReceived += _onData;
    }

    public string Name { get; }

    public bool IsOpen
    {
        get
        {
            lock (_gate) return _open;
        }
    }

    /// <summary>
    /// 포트를 엽니다. 속도와 8N1은 config와 고정값이고 흐름 제어는 쓰지 않습니다. 열기 전에 장치가 보낸 바이트는 버립니다.
    /// </summary>
    /// <remarks>
    /// DTR과 RTS는 켠 채로 둡니다. 원본은 흐름 제어만 끄고 드라이버 기본값을 그대로 두었고, Windows 직렬 드라이버는 열 때
    /// 두 신호를 켭니다. System.IO.Ports는 가만히 두면 둘을 끄므로 켜 줍니다.
    /// </remarks>
    public static SerialOpenResult Open(string name, SerialConfig config) => Open(name, config, assertModemLines: true);

    // 가상 단말(pty)은 모뎀 제어선을 지원하지 않아 DTR을 켜면 열기가 실패합니다.
    internal static SerialOpenResult Open(string name, SerialConfig config, bool assertModemLines)
    {
        SerialPort? serial = null;
        try
        {
            serial = new SerialPort(name, config.BaudRate, Parity.None, 8, StopBits.One)
            {
                Handshake = Handshake.None,
                DtrEnable = assertModemLines,
                RtsEnable = assertModemLines,
                ReceivedBytesThreshold = 1,
                // 받은 바이트가 있음을 확인하고 읽으므로 기다리지 않지만, 무한 대기로 남기지 않습니다.
                ReadTimeout = 100,
                WriteTimeout = 100,
            };
            serial.Open();
            serial.DiscardInBuffer();
            serial.DiscardOutBuffer();
            return SerialOpenResult.Opened(new SystemSerialPort(name, serial));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException
                                      or InvalidOperationException or PlatformNotSupportedException)
        {
            serial?.Dispose();
            return SerialOpenResult.Failed($"{name} 포트를 열지 못했습니다: {e.Message}");
        }
    }

    public bool Write(ReadOnlySpan<byte> bytes, int timeoutMs)
    {
        if (!Begin()) return false;
        bool ok = false;
        bool failed = false;
        try
        {
            _port.WriteTimeout = Math.Max(timeoutMs, 1);
            _port.Write(bytes.ToArray(), 0, bytes.Length);
            ok = true;
        }
        catch (TimeoutException)
        {
            // 시간 초과는 포트를 닫지 않습니다. 장치가 빠졌다고 볼지는 부른 쪽이 정합니다.
        }
        catch (Exception e) when (IsDeviceError(e))
        {
            failed = true;
        }
        End(failed);
        return ok;
    }

    public int? Read(Span<byte> buffer, int timeoutMs)
    {
        if (!Begin()) return null;
        int received = 0;
        bool failed = false;
        try
        {
            received = ReadCore(buffer, timeoutMs);
        }
        catch (Exception e) when (IsDeviceError(e))
        {
            failed = true;
        }
        End(failed);
        return failed ? null : received;
    }

    public void WakeRead() => Signal(data: false);

    public void Close()
    {
        lock (_gate)
        {
            if (_closing) return;
            _closing = true;
            _open = false;
            // 기다리는 읽기를 깨운 뒤, 입출력이 모두 돌아올 때까지 기다렸다가 닫습니다. 쓰기는 시간 제한 안에 돌아오지만,
            // 드라이버가 멈추면 돌아오지 않을 수 있어 ISerialPort.Close가 부르는 쪽의 대비를 요구합니다.
            Signal(data: false);
            while (_inFlight > 0) Monitor.Wait(_gate);
        }
        try
        {
            _port.DataReceived -= _onData;
            _port.Close();
        }
        catch (Exception e) when (IsDeviceError(e))
        {
            // 장치를 뽑은 뒤 닫으면 드라이버가 예외를 냅니다. 닫으려던 것이므로 무시합니다.
        }
        _port.Dispose();
    }

    public void Dispose() => Close();

    int ReadCore(Span<byte> buffer, int timeoutMs)
    {
        if (buffer.IsEmpty) return 0;
        long start = Stopwatch.GetTimestamp();
        while (true)
        {
            // 확인하기 전에 지워야, 확인한 뒤에 온 바이트의 알림이 남아 다음 기다림을 건너뜁니다.
            lock (_signal) _dataSignaled = false;
            int n = ReadAvailable(buffer);
            if (n > 0) return n;

            int remaining = (int)Math.Ceiling(timeoutMs - Stopwatch.GetElapsedTime(start).TotalMilliseconds);
            bool woken;
            lock (_signal)
            {
                if (remaining > 0 && !_wakePending && !_dataSignaled)
                {
                    Monitor.Wait(_signal, Math.Min(remaining, PollSliceMs));
                }
                woken = _wakePending;
                _wakePending = false;
            }
            if (woken) return ReadAvailable(buffer);
            if (remaining <= 0) return 0;
        }
    }

    int ReadAvailable(Span<byte> buffer)
    {
        int count = Math.Min(buffer.Length, _port.BytesToRead);
        if (count <= 0) return 0;
        if (_scratch.Length < count) _scratch = new byte[count];
        try
        {
            int n = _port.Read(_scratch, 0, count);
            _scratch.AsSpan(0, n).CopyTo(buffer);
            return n;
        }
        catch (TimeoutException)
        {
            return 0;
        }
    }

    void Signal(bool data)
    {
        lock (_signal)
        {
            if (data) _dataSignaled = true;
            else _wakePending = true;
            Monitor.PulseAll(_signal);
        }
    }

    bool Begin()
    {
        lock (_gate)
        {
            if (!_open) return false;
            ++_inFlight;
            return true;
        }
    }

    void End(bool failed)
    {
        lock (_gate)
        {
            if (failed) _open = false;
            --_inFlight;
            Monitor.PulseAll(_gate);
        }
    }

    // 장치가 빠지거나 드라이버가 오류를 낼 때 System.IO.Ports가 던지는 예외입니다. 쓰기 시간 초과(TimeoutException)는 뺍니다.
    static bool IsDeviceError(Exception e) =>
        e is IOException or UnauthorizedAccessException or InvalidOperationException;
}
