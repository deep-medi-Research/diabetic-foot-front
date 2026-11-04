using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

namespace DiabeticFoot.Platform.Windows;

/// <summary>
/// 시스템의 직렬 포트 목록입니다. SetupAPI의 Ports 장치 클래스에서 포트 이름, 장치 이름, USB VID:PID, 일련번호를 읽습니다.
/// 포트를 열지 않습니다.
/// </summary>
/// <remarks>
/// LED 보드와 PPG 장치가 같은 종류의 USB 직렬 변환기를 쓸 수 있어, 목록에서 자동으로 고르지 않고 사람이 고릅니다.
/// 포트를 여는 것은 <see cref="SystemSerialPort.Open"/>입니다.
/// </remarks>
public static class WindowsSerialPorts
{
    static readonly Guid PortsClass = new("4D36E978-E325-11CE-BFC1-08002BE10318");
    static readonly IntPtr InvalidHandle = new(-1);

    const uint DigcfPresent = 0x2;
    const uint DicsFlagGlobal = 1;
    const uint DiregDev = 1;
    const uint KeyRead = 0x20019;
    const uint SpdrpDeviceDesc = 0x0;
    const uint SpdrpHardwareId = 0x1;
    const uint SpdrpFriendlyName = 0xC;
    const uint CrSuccess = 0;
    const int MaxDeviceIdLen = 200;

    /// <summary>시스템의 직렬 포트(COM)를 COM 번호 순서로 돌려줍니다. 목록을 읽지 못하면 빈 목록입니다.</summary>
    public static IReadOnlyList<SerialPortInfo> List()
    {
        var ports = new List<SerialPortInfo>();
        Guid portsClass = PortsClass;
        IntPtr devices = SetupDiGetClassDevsW(ref portsClass, null, IntPtr.Zero, DigcfPresent);
        if (devices == InvalidHandle) return ports;
        try
        {
            var device = new DeviceInfoData { CbSize = (uint)Marshal.SizeOf<DeviceInfoData>() };
            for (uint i = 0; SetupDiEnumDeviceInfo(devices, i, ref device); ++i)
            {
                string? name = ReadPortName(devices, ref device);
                // Ports 클래스에는 프린터 포트(LPT)도 있어 COM만 남깁니다.
                if (name is null || !name.StartsWith("COM", StringComparison.Ordinal)) continue;

                string description = RegistryText(devices, ref device, SpdrpFriendlyName);
                if (description.Length == 0) description = RegistryText(devices, ref device, SpdrpDeviceDesc);
                string hardwareId = RegistryText(devices, ref device, SpdrpHardwareId);
                ports.Add(new SerialPortInfo(
                    name,
                    description,
                    UsbDeviceId.ReadHexAfter(hardwareId, "VID_"),
                    UsbDeviceId.ReadHexAfter(hardwareId, "PID_"),
                    SerialNumberOf(device)));
            }
        }
        finally
        {
            SetupDiDestroyDeviceInfoList(devices);
        }
        return ports.OrderBy(port => UsbDeviceId.PortNumber(port.Name)).ToList();
    }

    static string? ReadPortName(IntPtr devices, ref DeviceInfoData device)
    {
        IntPtr handle = SetupDiOpenDevRegKey(devices, ref device, DicsFlagGlobal, 0, DiregDev, KeyRead);
        if (handle == InvalidHandle) return null;
        using RegistryKey key = RegistryKey.FromHandle(new SafeRegistryHandle(handle, ownsHandle: true));
        return key.GetValue("PortName") as string;
    }

    static string RegistryText(IntPtr devices, ref DeviceInfoData device, uint property)
    {
        var buffer = new byte[1024];
        if (!SetupDiGetDeviceRegistryPropertyW(devices, ref device, property, out _, buffer, (uint)buffer.Length - 2, out _))
        {
            return "";
        }
        // 하드웨어 ID는 문자열 여러 개가 이어진 값이라 첫 문자열만 씁니다.
        string text = Encoding.Unicode.GetString(buffer);
        int end = text.IndexOf('\0');
        return end < 0 ? text : text[..end];
    }

    static string InstanceIdOf(uint instance)
    {
        var id = new char[MaxDeviceIdLen + 1];
        if (CM_Get_Device_IDW(instance, id, MaxDeviceIdLen, 0) != CrSuccess) return "";
        int end = Array.IndexOf(id, '\0');
        return new string(id, 0, end < 0 ? id.Length : end);
    }

    // 포트 장치의 인스턴스 ID에 일련번호가 없으면 부모(USB 장치)에서 찾습니다. 복합 장치의 인터페이스(&MI_xx)는 부모에 일련번호가 있습니다.
    static string SerialNumberOf(DeviceInfoData device)
    {
        string serial = UsbDeviceId.SerialNumberFromInstanceId(InstanceIdOf(device.DevInst));
        if (serial.Length == 0 && CM_Get_Parent(out uint parent, device.DevInst, 0) == CrSuccess)
        {
            serial = UsbDeviceId.SerialNumberFromInstanceId(InstanceIdOf(parent));
        }
        return serial;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct DeviceInfoData
    {
        public uint CbSize;
        public Guid ClassGuid;
        public uint DevInst;
        public UIntPtr Reserved;
    }

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr SetupDiGetClassDevsW(ref Guid classGuid, string? enumerator, IntPtr hwndParent, uint flags);

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiEnumDeviceInfo(IntPtr deviceInfoSet, uint memberIndex, ref DeviceInfoData deviceInfoData);

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern IntPtr SetupDiOpenDevRegKey(
        IntPtr deviceInfoSet, ref DeviceInfoData deviceInfoData, uint scope, uint hwProfile, uint keyType, uint samDesired);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool SetupDiGetDeviceRegistryPropertyW(
        IntPtr deviceInfoSet, ref DeviceInfoData deviceInfoData, uint property, out uint propertyRegDataType,
        byte[] propertyBuffer, uint propertyBufferSize, out uint requiredSize);

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiDestroyDeviceInfoList(IntPtr deviceInfoSet);

    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)]
    static extern uint CM_Get_Device_IDW(uint devInst, [Out] char[] buffer, uint bufferLen, uint flags);

    [DllImport("cfgmgr32.dll")]
    static extern uint CM_Get_Parent(out uint parentDevInst, uint devInst, uint flags);
}
