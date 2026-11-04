namespace DiabeticFoot.Platform.Tests;

public class UsbDeviceIdTests
{
    // FTDI 포트는 두 번째 + 뒤부터 \ 앞까지가 일련번호(포트 문자 포함)입니다.
    [Fact]
    public void SerialNumber_ReadsFtdiBusInstanceId()
    {
        Assert.Equal("A50285BIA", UsbDeviceId.SerialNumberFromInstanceId(@"FTDIBUS\VID_0403+PID_6001+A50285BIA\0000"));
        Assert.Equal("A50285BIA", UsbDeviceId.SerialNumberFromInstanceId(@"FTDIBUS\VID_0403+PID_6001+A50285BIA"));
    }

    // USB 장치는 마지막 \ 뒤가 일련번호입니다.
    [Fact]
    public void SerialNumber_ReadsUsbInstanceId()
    {
        Assert.Equal("0001", UsbDeviceId.SerialNumberFromInstanceId(@"USB\VID_10C4&PID_EA60\0001"));
    }

    // 일련번호가 없는 장치에 Windows가 위치로 만든 값(&가 들어 있음)은 일련번호로 쓰지 않습니다.
    [Fact]
    public void SerialNumber_IgnoresGeneratedInstanceId()
    {
        Assert.Equal("", UsbDeviceId.SerialNumberFromInstanceId(@"USB\VID_1A86&PID_7523\5&2C9A7A1&0&3"));
        Assert.Equal("", UsbDeviceId.SerialNumberFromInstanceId(@"USB\VID_1A86&PID_7523"));
        Assert.Equal("", UsbDeviceId.SerialNumberFromInstanceId(@"ACPI\PNP0501\1"));
        Assert.Equal("", UsbDeviceId.SerialNumberFromInstanceId(@"FTDIBUS\VID_0403+PID_6001"));
        Assert.Equal("", UsbDeviceId.SerialNumberFromInstanceId(""));
    }

    // 복합 장치 인터페이스의 하드웨어 ID에서 VID와 PID를 읽습니다.
    [Theory]
    [InlineData(@"USB\VID_10C4&PID_EA60&REV_0100", "VID_", 0x10C4)]
    [InlineData(@"USB\VID_10C4&PID_EA60&REV_0100", "PID_", 0xEA60)]
    [InlineData(@"FTDIBUS\VID_0403+PID_6001+A50285BIA\0000", "VID_", 0x0403)]
    [InlineData(@"FTDIBUS\VID_0403+PID_6001+A50285BIA\0000", "PID_", 0x6001)]
    public void ReadHexAfter_ReadsFourHexDigits(string hardwareId, string key, int expected)
    {
        Assert.Equal((ushort)expected, UsbDeviceId.ReadHexAfter(hardwareId, key));
    }

    // 키가 없거나 4자리가 안 되거나 16진수가 아니면 값이 없습니다.
    [Theory]
    [InlineData(@"ACPI\PNP0501", "VID_")]
    [InlineData(@"USB\VID_10C", "VID_")]
    [InlineData(@"USB\VID_10CZ&PID_EA60", "VID_")]
    [InlineData("", "PID_")]
    public void ReadHexAfter_ReturnsNullForMalformedId(string hardwareId, string key)
    {
        Assert.Null(UsbDeviceId.ReadHexAfter(hardwareId, key));
    }

    // 포트 목록은 COM 번호 순서로 보이므로 COM10이 COM9 뒤에 옵니다.
    [Theory]
    [InlineData("COM3", 3)]
    [InlineData("COM12", 12)]
    [InlineData("COM", 0)]
    [InlineData("", 0)]
    public void PortNumber_ReadsDigitsAfterCom(string name, int expected)
    {
        Assert.Equal(expected, UsbDeviceId.PortNumber(name));
    }
}
