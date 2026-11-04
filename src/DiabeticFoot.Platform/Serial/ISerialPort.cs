namespace DiabeticFoot.Platform;

/// <summary>
/// 직렬 포트를 여는 조건입니다. 데이터 8비트, 패리티 없음, 정지 1비트, 흐름 제어 없음으로 고정합니다.
/// LED 보드와 PPG 장치가 모두 115200bps 8N1이라 다른 형식은 두지 않습니다.
/// </summary>
public sealed record SerialConfig(int BaudRate = 115200);

/// <summary>
/// 열린 직렬 포트 하나입니다.
/// 쓰기와 읽기는 각자 시간 제한을 받고, 한 스레드가 Read로 기다리는 동안 다른 스레드가 Write를 할 수 있습니다.
/// 쓰기나 읽기가 장치 오류로 실패하면 포트는 닫힌 상태가 되고 다시 열리지 않습니다. 다시 쓰려면 새로 엽니다.
/// </summary>
public interface ISerialPort : IDisposable
{
    /// <summary>연 포트 이름입니다(예: "COM3"). 닫힌 뒤에도 같은 값을 돌려줍니다.</summary>
    string Name { get; }

    /// <summary>포트가 열려 있으면 true입니다. Close를 불렀거나 장치 오류가 난 뒤에는 false입니다.</summary>
    bool IsOpen { get; }

    /// <summary>바이트를 모두 내보냅니다.</summary>
    /// <param name="bytes">보낼 바이트입니다.</param>
    /// <param name="timeoutMs">모두 내보낼 때까지 기다리는 최대 시간입니다.</param>
    /// <returns>
    /// 시간 안에 모두 내보냈으면 true입니다. 일부만 나갔거나 시간이 지났거나 장치 오류면 false입니다.
    /// 시간 초과는 포트를 닫지 않으며, 장치가 빠졌다고 볼지는 부른 쪽이 정합니다.
    /// </returns>
    bool Write(ReadOnlySpan<byte> bytes, int timeoutMs);

    /// <summary>
    /// 받은 바이트를 읽습니다. 이미 받은 바이트가 있으면 기다리지 않고 buffer 크기까지 돌려주고, 없으면 첫 바이트가
    /// 올 때까지 최대 timeoutMs 기다립니다. 정해진 길이가 다 찰 때까지 기다리지 않으므로 패킷을 맞추는 일은 부른 쪽이 합니다.
    /// </summary>
    /// <param name="buffer">받은 바이트를 넣을 곳입니다.</param>
    /// <param name="timeoutMs">첫 바이트를 기다리는 최대 시간입니다. 0이면 기다리지 않습니다.</param>
    /// <returns>읽은 바이트 수입니다(시간 초과면 0). 포트가 닫혔거나 장치 오류면 null입니다.</returns>
    int? Read(Span<byte> buffer, int timeoutMs);

    /// <summary>
    /// Read로 기다리는 쪽을 바로 돌아오게 합니다. 다른 스레드에서 부릅니다.
    /// 기다리는 Read가 없으면 다음 Read 한 번이 기다리지 않고 돌아옵니다. 깨운 Read는 그때까지 받은 바이트 수(없으면 0)를
    /// 돌려주고 포트는 닫지 않습니다.
    /// </summary>
    void WakeRead();

    /// <summary>포트를 닫습니다. 여러 번 불러도 됩니다.</summary>
    void Close();
}

/// <summary>직렬 포트를 연 결과입니다. 연 포트이거나 열지 못한 이유 중 하나입니다.</summary>
public sealed class SerialOpenResult
{
    SerialOpenResult(ISerialPort? port, string error)
    {
        Port = port;
        Error = error;
    }

    /// <summary>연 포트입니다. 열지 못했으면 null입니다.</summary>
    public ISerialPort? Port { get; }

    /// <summary>열지 못한 이유입니다. 열었으면 빈 문자열입니다.</summary>
    public string Error { get; }

    public static SerialOpenResult Opened(ISerialPort port) => new(port, "");

    public static SerialOpenResult Failed(string error) => new(null, error);
}

/// <summary>이름으로 직렬 포트를 엽니다. 테스트에서는 가짜 포트를 여는 함수로 바꿔 넣습니다.</summary>
public delegate SerialOpenResult SerialPortOpener(string name, SerialConfig config);
