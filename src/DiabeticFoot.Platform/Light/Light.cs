using System.Diagnostics.CodeAnalysis;

namespace DiabeticFoot.Platform;

/// <summary>
/// 직렬 포트로 연결한 LED 조명 하나입니다.
/// 보드는 현재 밝기를 읽는 명령이 없고 명령에 응답하지 않습니다. 그래서 이 클래스가 아는 밝기는 장치에서 읽은 값이 아니라
/// 마지막으로 쓰기에 성공한 명령의 값입니다(<see cref="LastSentPercent"/>). 포트를 열면 그 자리에서 밝기를 한 번 보내
/// 이 값을 장치와 맞춥니다. 쓰기가 실패하거나 <see cref="LightProtocol.WriteTimeoutMs"/>를 넘으면 포트를 닫고 연결 끊김으로 둡니다.
/// 여러 스레드에서 불러도 됩니다.
/// </summary>
/// <remarks>
/// 녹화 중에는 밝기를 바꾸지 않습니다. 이 클래스는 막지 않으므로 부른 쪽(촬영 흐름)이 지킵니다.
/// </remarks>
public sealed class Light : IDisposable
{
    readonly SerialPortOpener _opener;
    readonly object _gate = new();
    ISerialPort? _port;
    int? _lastSentPercent;

    public Light(SerialPortOpener opener)
    {
        _opener = opener;
    }

    /// <summary>
    /// 포트를 열고 밝기를 보냅니다. 이미 열려 있으면 먼저 닫습니다.
    /// 열었지만 보내지 못했으면 포트를 닫고 false입니다.
    /// </summary>
    /// <param name="percent">열자마자 보낼 밝기(%)입니다. 0~100으로 자릅니다.</param>
    public bool Open(string portName, int percent, [NotNullWhen(false)] out string? error)
    {
        lock (_gate)
        {
            CloseLocked();
            SerialOpenResult opened = _opener(portName, LightProtocol.Serial);
            if (opened.Port is null)
            {
                error = string.IsNullOrEmpty(opened.Error) ? $"{portName} 포트를 열지 못했습니다" : opened.Error;
                return false;
            }
            _port = opened.Port;
            return SendLocked(percent, out error);
        }
    }

    /// <summary>
    /// 밝기를 보냅니다.
    /// 연결되어 있지 않거나 쓰기가 실패하면 false이고, 쓰기 실패면 연결을 끊습니다.
    /// </summary>
    /// <param name="percent">밝기(%)입니다. 0~100으로 자릅니다.</param>
    public bool SetPercent(int percent, [NotNullWhen(false)] out string? error)
    {
        lock (_gate)
        {
            if (_port is null)
            {
                error = "조명이 연결되어 있지 않습니다";
                return false;
            }
            return SendLocked(percent, out error);
        }
    }

    /// <summary>
    /// 마지막으로 보내는 데 성공한 밝기(%)입니다. 장치에서 읽은 값이 아닙니다. 연결되어 있지 않으면 null입니다.
    /// 사이드카에는 이 값을 남깁니다. 연결이 없을 때 화면에 보이는 값은 장치에 보내지 않은 값이라 남기지 않습니다.
    /// </summary>
    public int? LastSentPercent
    {
        get
        {
            lock (_gate) return _port is { IsOpen: true } ? _lastSentPercent : null;
        }
    }

    /// <summary>포트가 열려 있고 마지막 쓰기가 성공했으면 true입니다.</summary>
    public bool IsConnected
    {
        get
        {
            lock (_gate) return _port is { IsOpen: true };
        }
    }

    /// <summary>연 포트 이름입니다. 연결되어 있지 않으면 빈 문자열입니다.</summary>
    public string PortName
    {
        get
        {
            lock (_gate) return _port?.Name ?? "";
        }
    }

    /// <summary>포트를 닫습니다. 밝기는 바꾸지 않으므로 LED는 마지막 밝기로 남습니다.</summary>
    public void Close()
    {
        lock (_gate) CloseLocked();
    }

    public void Dispose() => Close();

    bool SendLocked(int percent, [NotNullWhen(false)] out string? error)
    {
        byte[] command = LightProtocol.MakeCommand(percent);
        if (!_port!.Write(command, LightProtocol.WriteTimeoutMs))
        {
            // 슬라이더를 끌면 연이어 불리므로, 빠진 장치를 연결된 채로 두면 호출마다 150ms씩 기다리게 됩니다.
            error = $"{_port.Name} 포트에 밝기를 보내지 못해 연결을 끊었습니다";
            CloseLocked();
            return false;
        }
        _lastSentPercent = command[1];
        error = null;
        return true;
    }

    void CloseLocked()
    {
        _port?.Close();
        _port = null;
        _lastSentPercent = null;
    }
}
