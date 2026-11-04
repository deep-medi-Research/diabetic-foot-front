using System.Globalization;

namespace DiabeticFoot.Platform;

/// <summary>
/// 시스템에 있는 직렬 포트 하나입니다. 사람이 포트를 고를 때 보여 줍니다.
/// </summary>
/// <param name="Name">열 때 쓰는 이름입니다(예: "COM3").</param>
/// <param name="Description">장치 관리자에 보이는 이름입니다. 어느 변환기인지 사람이 구분하는 데 씁니다.</param>
/// <param name="UsbVendorId">USB 변환기면 VID입니다(예: 0x10C4).</param>
/// <param name="UsbProductId">USB 변환기면 PID입니다(예: 0xEA60).</param>
/// <param name="SerialNumber">
/// USB 일련번호입니다. 장치가 일련번호를 내지 않거나 USB가 아니면 빈 문자열입니다.
/// COM 번호는 꽂는 단자에 따라 바뀌므로 같은 변환기를 다시 찾을 때 VID:PID와 이 값을 씁니다.
/// </param>
public sealed record SerialPortInfo(
    string Name,
    string Description,
    ushort? UsbVendorId,
    ushort? UsbProductId,
    string SerialNumber);

/// <summary>
/// Windows 장치 정보 문자열에서 USB 식별값을 꺼내는 순수 함수입니다. 포트를 열지 않으므로 모든 OS에서 씁니다.
/// </summary>
public static class UsbDeviceId
{
    /// <summary>
    /// 장치 인스턴스 ID에서 USB 일련번호를 꺼냅니다.
    /// <list type="bullet">
    /// <item><c>FTDIBUS\VID_0403+PID_6001+A50285BIA\0000</c>은 두 번째 <c>+</c> 뒤부터 <c>\</c> 앞까지(A50285BIA)입니다.</item>
    /// <item><c>USB\VID_10C4&amp;PID_EA60\0001</c>은 마지막 <c>\</c> 뒤(0001)입니다.</item>
    /// <item><c>USB\VID_1A86&amp;PID_7523\5&amp;2C9A7A1&amp;0&amp;3</c>처럼 마지막 부분에 <c>&amp;</c>가 있으면 Windows가 만든 값이라 빈 문자열입니다.</item>
    /// </list>
    /// FTDI 드라이버는 장치 일련번호 뒤에 포트 문자(A, B 등)를 더한 값을 인스턴스 ID에 넣습니다. 같은 변환기를 다시 찾는
    /// 비교에만 쓰므로 포트 문자를 떼지 않고 그대로 돌려줍니다.
    /// </summary>
    /// <returns>일련번호입니다. 꺼낼 수 없으면 빈 문자열입니다.</returns>
    public static string SerialNumberFromInstanceId(string instanceId)
    {
        if (instanceId.StartsWith("FTDIBUS\\", StringComparison.Ordinal))
        {
            // VID_xxxx+PID_xxxx+<일련번호><포트 문자>\0000 형식이라 두 번째 + 뒤를 씁니다.
            int first = instanceId.IndexOf('+');
            if (first < 0) return "";
            int second = instanceId.IndexOf('+', first + 1);
            if (second < 0) return "";
            int end = instanceId.IndexOf('\\', second + 1);
            return end < 0 ? instanceId[(second + 1)..] : instanceId[(second + 1)..end];
        }

        if (instanceId.StartsWith("USB\\", StringComparison.Ordinal))
        {
            int last = instanceId.LastIndexOf('\\');
            if (last < 4 || last + 1 >= instanceId.Length) return "";
            string tail = instanceId[(last + 1)..];
            // 일련번호가 없는 장치는 Windows가 위치로 만든 값(예: 5&2C9A7A1&0&3)을 넣으므로 같은 장치를 가리키지 않습니다.
            return tail.Contains('&') ? "" : tail;
        }

        return "";
    }

    /// <summary>하드웨어 ID(예: <c>USB\VID_10C4&amp;PID_EA60&amp;REV_0100</c>)에서 key("VID_" 또는 "PID_") 뒤 16진수 4자리를 읽습니다.</summary>
    /// <returns>읽은 값입니다. key가 없거나 4자리가 16진수가 아니면 null입니다.</returns>
    public static ushort? ReadHexAfter(string hardwareId, string key)
    {
        int at = hardwareId.IndexOf(key, StringComparison.Ordinal);
        if (at < 0 || at + key.Length + 4 > hardwareId.Length) return null;
        string digits = hardwareId.Substring(at + key.Length, 4);
        return ushort.TryParse(digits, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out ushort value)
            ? value
            : null;
    }

    /// <summary>"COM12"에서 12를 읽습니다. 번호가 없으면 0입니다. 포트 목록을 번호 순서로 정렬하는 데 씁니다.</summary>
    public static int PortNumber(string name)
    {
        if (name.Length <= 3) return 0;
        int number = 0;
        for (int i = 3; i < name.Length && char.IsAsciiDigit(name[i]); i++) number = number * 10 + (name[i] - '0');
        return number;
    }
}
