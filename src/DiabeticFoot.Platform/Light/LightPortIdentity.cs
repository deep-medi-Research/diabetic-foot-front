namespace DiabeticFoot.Platform;

/// <summary>사람이 LED 보드로 확인한 직렬 변환기입니다. 다음 실행에서 같은 변환기를 찾을 때 씁니다.</summary>
/// <param name="UsbVendorId">변환기의 VID입니다.</param>
/// <param name="UsbProductId">변환기의 PID입니다.</param>
/// <param name="SerialNumber">비어 있으면 일련번호를 내지 않는 변환기입니다.</param>
public sealed record LightPortIdentity(ushort UsbVendorId, ushort UsbProductId, string SerialNumber)
{
    /// <summary>포트 정보에서 기억할 변환기 식별값을 만듭니다. USB 변환기가 아니면(VID나 PID가 없으면) null입니다.</summary>
    public static LightPortIdentity? Of(SerialPortInfo port) =>
        port.UsbVendorId is { } vendor && port.UsbProductId is { } product
            ? new LightPortIdentity(vendor, product, port.SerialNumber)
            : null;

    /// <summary>
    /// 포트 목록에서 이 변환기를 찾아 제안합니다. 포트를 열지 않습니다.
    /// 일련번호를 기억했으면 VID:PID와 일련번호가 모두 같아야 합니다. 일련번호가 없는 변환기는 같은 VID:PID가
    /// 하나뿐일 때만 제안합니다.
    /// </summary>
    /// <param name="ports">포트 목록입니다.</param>
    /// <returns>맞는 포트가 하나뿐이면 그 이름입니다. 없거나 둘 이상이면 null입니다.</returns>
    /// <remarks>
    /// PPG 장치도 같은 FTDI 변환기(0403:6001)를 쓸 수 있어 VID:PID만으로는 구분하지 못합니다. 제안한 포트도 사람이
    /// 연결하고 LED 밝기가 바뀌는 것을 확인합니다.
    /// </remarks>
    public string? FindPort(IEnumerable<SerialPortInfo> ports)
    {
        string? found = null;
        int matches = 0;
        foreach (SerialPortInfo port in ports)
        {
            if (Of(port) is not { } identity) continue;
            if (identity.UsbVendorId != UsbVendorId || identity.UsbProductId != UsbProductId) continue;
            if (SerialNumber.Length > 0 && identity.SerialNumber != SerialNumber) continue;
            found = port.Name;
            ++matches;
        }
        return matches == 1 ? found : null;
    }
}
