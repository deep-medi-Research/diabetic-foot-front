<# :
@echo off
rem 이 파일은 배치 파일이면서 PowerShell 스크립트입니다. 배치 부분은 아래 PowerShell 부분을 읽어 실행하고, PowerShell은 이 부분을 주석으로 건너뜁니다.
chcp 65001 >nul
set "HWLAB_DIAG_SELF=%~f0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Invoke-Expression ([IO.File]::ReadAllText($env:HWLAB_DIAG_SELF, [Text.Encoding]::UTF8))"
set "HWLAB_DIAG_CODE=%errorlevel%"
if not defined HWLAB_DIAG_NOOPEN pause
exit /b %HWLAB_DIAG_CODE%
#>

# 당뇨발 촬영 진단입니다. 설치, 열화상 드라이버와 런타임, 장치, RGB 카메라 USB 연결 속도, 카메라 권한, COM 포트, 저장 공간을 읽기만 하고, 결과를 텍스트 파일 하나로 남깁니다.
# 아무것도 설치하거나 고치지 않습니다. 설치가 실패한 PC에서도 실행할 수 있게 설치 폴더의 파일에 기대지 않습니다.
# RGB 카메라는 열지 않습니다. 연결 속도와 형식 목록은 USB 허브에 묻고(카메라 장치 파일을 열지 않음), 다른 프로그램이 촬영 중이어도 영향이 없습니다.
# HWLAB_DIAG_OUT에 폴더를 주면 보고서를 그 폴더에 쓰고, HWLAB_DIAG_NOOPEN이 있으면 메모장을 열지 않고 멈추지 않습니다(CI용).

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8

$DiagVersion = '2026-11-05'
$AppDir = 'C:\hwlab\app'
$AppId = '{7F0D2A8B-B8E5-4618-AD01-1103026BB21C}_is1'
$AppName = '당뇨발 촬영'
$ShotBytes = 1.1GB
$LowBytes = 10GB
$ReserveBytes = 1GB

$checks = New-Object System.Collections.Generic.List[object]
$appendix = New-Object System.Collections.Generic.List[string]

function Add-Check([string]$Area, [string]$Item, [string]$State, [string]$Value, [string]$Hint = '') {
  $checks.Add([pscustomobject]@{ Area = $Area; Item = $Item; State = $State; Value = $Value; Hint = $Hint })
}

function Add-Appendix([string]$Title, $Lines) {
  $appendix.Add('')
  $appendix.Add("== $Title ==")
  foreach ($line in @($Lines)) { $appendix.Add([string]$line) }
}

function Invoke-Section([string]$Area, [scriptblock]$Block) {
  try { & $Block } catch { Add-Check $Area '진단 중 오류' '정보' $_.Exception.Message '이 항목은 확인하지 못했습니다' }
}

function Format-Bytes([double]$Bytes) { '{0:N1} GB' -f ($Bytes / 1GB) }

function Get-RegValue([string]$Path, [string]$Name) {
  $item = Get-ItemProperty -LiteralPath $Path -Name $Name -ErrorAction SilentlyContinue
  if ($item) { return $item.$Name }
  return $null
}

$shortcut = $null

Invoke-Section '시스템' {
  $os = Get-CimInstance Win32_OperatingSystem
  $build = [int]$os.BuildNumber
  $value = "$($os.Caption) $($os.Version) ($($os.OSArchitecture))"
  if (-not [Environment]::Is64BitOperatingSystem) {
    Add-Check '시스템' 'Windows' '문제' $value '64비트 Windows만 지원합니다'
  } elseif ($build -lt 22000) {
    Add-Check '시스템' 'Windows' '주의' $value 'Windows 10에서는 아직 확인하지 않았습니다. Windows 11에서 확인했습니다'
  } else {
    Add-Check '시스템' 'Windows' '정상' $value
  }
  $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
  $admin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
  Add-Check '시스템' 'PC와 사용자' '정보' "$env:COMPUTERNAME, $env:USERNAME, 관리자 권한으로 실행: $admin"
}

Invoke-Section '설치' {
  $exe = Join-Path $AppDir 'bin\hwlab_app.exe'
  if (-not (Test-Path -LiteralPath $exe)) {
    Add-Check '설치' '앱' '문제' "$exe 없음" '설치 파일(HWLab-Setup-v*.exe)을 다시 실행합니다'
    return
  }
  $infoPath = Join-Path $AppDir 'package_info.json'
  if (Test-Path -LiteralPath $infoPath) {
    $info = Get-Content -LiteralPath $infoPath -Raw -Encoding UTF8 | ConvertFrom-Json
    Add-Check '설치' '앱' '정상' "$($info.version), 커밋 $($info.app.commit.Substring(0, 7)), 촬영 선언 $($info.build.run_spec)"
    Add-Appendix 'package_info.json' (Get-Content -LiteralPath $infoPath -Encoding UTF8)
  } else {
    Add-Check '설치' '앱' '주의' "$exe 있음, package_info.json 없음" '설치 파일을 다시 실행합니다'
  }

  $missing = @('bin\python\python.exe', 'bin\analysis', 'specs', 'drivers\thermal\i3usbdrv.inf', 'drivers\install_thermal_driver.cmd') |
    Where-Object { -not (Test-Path -LiteralPath (Join-Path $AppDir $_)) }
  if ($missing) { Add-Check '설치' '설치 파일 구성' '문제' ("없음: " + ($missing -join ', ')) '설치 파일을 다시 실행합니다' }
  else { Add-Check '설치' '설치 파일 구성' '정상' '분석 프로그램, 촬영 선언, 드라이버 파일 있음' }

  $uninstall = @("HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppId", "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppId") |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
  if ($uninstall) {
    $u = Get-ItemProperty -LiteralPath $uninstall
    Add-Check '설치' '설치 기록' '정보' "버전 $($u.DisplayVersion), 설치일 $($u.InstallDate)"
  } else {
    Add-Check '설치' '설치 기록' '주의' 'Windows 설치된 앱 목록에 없음' '설치 파일로 설치하지 않은 폴더일 수 있습니다'
  }

  $lnk = Join-Path ([Environment]::GetFolderPath('Desktop')) "$AppName.lnk"
  if (Test-Path -LiteralPath $lnk) {
    # WScript.Shell은 바로가기 이름을 시스템 ANSI 코드 페이지로 바꿔 열어, 영어 Windows에서는 한글 이름을 못 읽습니다. 영문 이름 사본으로 읽습니다.
    $copy = Join-Path $env:TEMP 'hwlab_diagnose_shortcut.lnk'
    Copy-Item -LiteralPath $lnk -Destination $copy -Force
    $s = (New-Object -ComObject WScript.Shell).CreateShortcut($copy)
    Remove-Item -LiteralPath $copy -Force
    $global:shortcut = $s
    $spec = if ($s.Arguments -match '--spec\s+"([^"]+)"') { $Matches[1] } else { $null }
    if ($spec -and -not (Test-Path -LiteralPath $spec)) {
      Add-Check '설치' '바탕화면 바로가기' '문제' "촬영 선언 파일 없음: $spec" '설치 파일을 다시 실행합니다'
    } else {
      Add-Check '설치' '바탕화면 바로가기' '정상' $s.Arguments
    }
  } else {
    Add-Check '설치' '바탕화면 바로가기' '주의' "$lnk 없음" '시작 메뉴의 바로가기를 쓰거나 설치 파일을 다시 실행합니다'
  }

  $running = @(Get-Process -Name hwlab_app -ErrorAction SilentlyContinue)
  Add-Check '설치' '실행 중인 앱' '정보' "$($running.Count)개"

  # Inno Setup으로 만든 다른 프로그램도 같은 이름(Setup Log*.txt)으로 남기므로, 앱 ID, 설치 파일 이름, 앱 실행 파일이 나오는 기록만 고릅니다.
  $appGuid = $AppId -replace '^\{|\}_is1$', ''
  $ours = "$([regex]::Escape($appGuid))|HWLab-Setup|\\hwlab_app\.exe"
  $logs = @(Get-ChildItem -LiteralPath $env:TEMP -Filter 'Setup Log*.txt' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending)
  $mine = @($logs | Where-Object { Select-String -LiteralPath $_.FullName -Pattern $ours -List -Quiet -ErrorAction SilentlyContinue })
  $others = $logs.Count - $mine.Count
  $skipped = if ($others -gt 0) { ", 다른 프로그램 기록 $($others)개는 뺌" } else { '' }
  if ($mine) {
    Add-Check '설치' '설치 프로그램 기록' '정보' "$($mine[0].Name) ($($mine[0].LastWriteTime))$skipped"
    Add-Appendix "설치 프로그램 기록 $($mine[0].Name)" (Get-Content -LiteralPath $mine[0].FullName -Tail 80)
  } else {
    Add-Check '설치' '설치 프로그램 기록' '정보' "없음$skipped"
  }
}

Invoke-Section '열화상 드라이버' {
  $repo = Join-Path $env:SystemRoot 'System32\DriverStore\FileRepository'
  $stored = @(Get-ChildItem -LiteralPath $repo -Directory -Filter 'i3usbdrv.inf_amd64_*' -ErrorAction SilentlyContinue)
  $bundledCat = Join-Path $AppDir 'drivers\thermal\i3usbdrv.cat'
  $want = if (Test-Path -LiteralPath $bundledCat) { (Get-FileHash -LiteralPath $bundledCat -Algorithm SHA256).Hash } else { $null }
  if (-not $stored) {
    Add-Check '열화상 드라이버' '드라이버 저장소' '문제' 'i3usbdrv 없음' "$AppDir\drivers\install_thermal_driver.cmd를 실행하고 사용자 계정 컨트롤에서 ""예""를 누릅니다"
    return
  }
  $names = $stored | ForEach-Object {
    $cat = Join-Path $_.FullName 'i3usbdrv.cat'
    $same = $want -and (Test-Path -LiteralPath $cat) -and ((Get-FileHash -LiteralPath $cat -Algorithm SHA256).Hash -eq $want)
    if ($same) { "$($_.Name)(설치 파일과 같음)" } else { $_.Name }
  }
  Add-Check '열화상 드라이버' '드라이버 저장소' '정상' ($names -join ', ')
}

# 열화상 SDK의 i3system DLL이 가져다 쓰는 런타임입니다. 없으면 열화상 카메라가 열리지 않습니다.
Invoke-Section '열화상 런타임' {
  $system = if ([Environment]::Is64BitProcess) { Join-Path $env:SystemRoot 'System32' } else { Join-Path $env:SystemRoot 'Sysnative' }
  $names = 'MSVCR100.dll', 'MSVCP100.dll', 'mfc100u.dll'
  $absent = @($names | Where-Object { -not (Test-Path -LiteralPath (Join-Path $system $_)) })
  # 재배포 패키지가 32비트 설치 프로그램이라 64비트 Windows에서는 WOW6432Node 아래에 등록됩니다(2026-11-05 노트북).
  $redist = @('HKLM:\SOFTWARE\Microsoft\VisualStudio\10.0\VC\VCRedist\x64', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\VisualStudio\10.0\VC\VCRedist\x64') |
    ForEach-Object { Get-ItemProperty -LiteralPath $_ -ErrorAction SilentlyContinue } | Where-Object { $_ } | Select-Object -First 1
  $registered = if ($redist -and $redist.Installed -eq 1) { "등록됨 $($redist.Version)" } else { '등록 없음' }
  if ($absent) {
    Add-Check '열화상 런타임' 'VC++ 2010 x64 런타임' '문제' ("없음: " + ($absent -join ', ') + ", 재배포 패키지 $registered") '열화상 SDK가 이 DLL을 씁니다. Microsoft Visual C++ 2010 x64 재배포 가능 패키지(vcredist_x64.exe)를 설치해야 열화상 카메라가 열립니다'
  } else {
    Add-Check '열화상 런타임' 'VC++ 2010 x64 런타임' '정상' ("$($names -join ', ') 있음, 재배포 패키지 $registered")
  }
}

Invoke-Section '장치' {
  $all = @(Get-CimInstance Win32_PnPEntity)
  $global:pnpDevices = $all
  $devices = @(
    @{ Name = 'RGB 카메라(See3CAM)'; Pattern = '^USB\\VID_2560&PID_[0-9A-F]{4}&MI_00'; Hint = 'USB 3 케이블로 허브 없이 PC의 USB 3 포트에 꽂고 장치 관리자의 "카메라"에 See3CAM이 보이는지 봅니다' },
    @{ Name = '열화상 카메라(TE-EV1)'; Pattern = '^USB\\VID_0547&PID_0080'; Hint = "꽂혀 있는데 문제로 나오면 $AppDir\drivers\install_thermal_driver.cmd를 실행합니다" },
    @{ Name = 'USB 직렬(FTDI, PPG와 LED)'; Pattern = '^USB\\VID_0403&'; Hint = '인터넷에 연결한 채 다시 꽂으면 Windows가 FTDI 드라이버를 받습니다' }
  )
  foreach ($d in $devices) {
    $found = @($all | Where-Object { $_.DeviceID -match $d.Pattern })
    if (-not $found) {
      Add-Check '장치' $d.Name '주의' '연결 안 됨' $d.Hint
      continue
    }
    foreach ($f in $found) {
      $value = "$($f.Name), 드라이버 $($f.Service), 상태 코드 $($f.ConfigManagerErrorCode)"
      if ($f.ConfigManagerErrorCode -eq 0) { Add-Check '장치' $d.Name '정상' $value }
      else { Add-Check '장치' $d.Name '문제' $value $d.Hint }
    }
  }
  $related = $all | Where-Object { $_.DeviceID -match 'VID_2560|VID_0547|VID_0403' }
  $broken = $all | Where-Object { $_.ConfigManagerErrorCode -ne 0 }
  Add-Appendix '관련 장치' ($related | ForEach-Object { "$($_.ConfigManagerErrorCode)`t$($_.PNPClass)`t$($_.Service)`t$($_.Name)`t$($_.DeviceID)" })
  Add-Appendix '상태 코드가 0이 아닌 장치' ($broken | ForEach-Object { "$($_.ConfigManagerErrorCode)`t$($_.PNPClass)`t$($_.Name)`t$($_.DeviceID)" })
}

# RGB 카메라의 USB 연결 속도와 형식 목록입니다. 카메라를 열지 않고 카메라가 꽂힌 허브의 포트에 묻습니다.
# USB 3 포트에 꽂아도 케이블, 허브, Type-C 방향 때문에 USB 2 속도로 연결되면 UYVY 1280x720이 최대 15fps로 보입니다(2026-11-04 현장 보고).
Invoke-Section 'RGB 카메라 USB' {
  $cameras = @($global:pnpDevices | Where-Object { $_.DeviceID -match '^USB\\VID_2560&PID_[0-9A-F]{4}\\[^\\]+$' })
  if (-not $cameras) { return }
  # 아래 C#은 Windows PowerShell 5.1의 컴파일러(C# 5)로 만들고, 컴파일러가 소스 파일 인코딩을 잘못 읽어도 되도록 ASCII만 씁니다.
  # ReadConnection은 IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX 결과에서 포트 0, 장치 설명자 4(18바이트), 속도 23, 연결 상태 31을 읽습니다(1바이트 정렬).
  # 이 속도는 SuperSpeed 장치도 High로 알립니다(2026-11-05 노트북, 장치 설명자는 bcdUSB 3.20인데 속도 2).
  # ReadPortInfo는 IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2로 SuperSpeed 여부를 정합니다. 입력의 프로토콜 8에 USB 3(비트 2)를 적어야 하고(안 적으면 오류 87), 결과의 플래그 12는 비트 0이 SuperSpeed 동작, 비트 1이 장치의 SuperSpeed 지원입니다.
  # 결과의 프로토콜 8(장치가 붙은 포트 하나가 지원하는 프로토콜)로는 꽂은 커넥터가 USB 3인지 가르지 않습니다. 같은 커넥터가 SuperSpeed 포트(값 4)와 High-Speed 포트(값 3)로 나뉘어, USB 2 속도로 연결된 장치는 값 3으로 읽힙니다(2026-11-05 노트북).
  # V2를 못 쓰는 허브에서는 장치 설명자의 bcdUSB가 3.0 이상이면 SuperSpeed로 봅니다. USB 3 장치는 USB 2 속도로 연결되면 bcdUSB 2.x 설명자를 내보냅니다.
  # ReadFormats는 설정 설명자에서 UVC 영상 스트리밍 인터페이스의 비압축, MJPEG 형식 설명자와 프레임 설명자의 가장 짧은 프레임 간격(100ns 단위)을 fps로 바꿉니다.
  $source = @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Win32.SafeHandles;

public class HwlabUsbFormat
{
    public string Name;
    public int Width;
    public int Height;
    public double MaxFps;
}

public class HwlabUsbLink
{
    public string Speed;
    public int Port;
    public string HubId;
    public int ExternalHubs;
    public int UsbVersion;
    public bool PortInfo;
    public int PortInfoError;
    public bool SuperSpeedCapable;
    public string FormatError;
    public List<HwlabUsbFormat> Formats = new List<HwlabUsbFormat>();
}

public static class HwlabUsb
{
    static readonly Guid UsbHubInterface = new Guid("f18a0e88-c30c-11d0-8815-00a0c906bed8");
    const uint IoctlConnectionInfoEx = 0x220448;
    const uint IoctlConnectionInfoExV2 = 0x22045C;
    const uint IoctlDescriptorFromNode = 0x220410;

    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)]
    static extern int CM_Locate_DevNodeW(out uint devInst, string deviceId, uint flags);
    [DllImport("cfgmgr32.dll")]
    static extern int CM_Get_Parent(out uint parent, uint devInst, uint flags);
    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)]
    static extern int CM_Get_Device_IDW(uint devInst, StringBuilder buffer, uint length, uint flags);
    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)]
    static extern int CM_Get_DevNode_Registry_PropertyW(uint devInst, uint property, out uint dataType, byte[] buffer, ref uint length, uint flags);
    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)]
    static extern int CM_Get_Device_Interface_List_SizeW(out uint length, ref Guid guid, string deviceId, uint flags);
    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)]
    static extern int CM_Get_Device_Interface_ListW(ref Guid guid, string deviceId, char[] buffer, uint length, uint flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern SafeFileHandle CreateFileW(string name, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool DeviceIoControl(SafeFileHandle handle, uint code, byte[] input, int inputLength, byte[] output, int outputLength, out int returned, IntPtr overlapped);

    public static HwlabUsbLink Query(string deviceId)
    {
        uint device;
        Check(CM_Locate_DevNodeW(out device, deviceId, 0), "CM_Locate_DevNode");
        HwlabUsbLink link = new HwlabUsbLink();
        byte[] address = new byte[4];
        uint type, length = 4;
        Check(CM_Get_DevNode_Registry_PropertyW(device, 0x1D, out type, address, ref length, 0), "CM_Get_DevNode_Registry_Property(address)");
        link.Port = (int)BitConverter.ToUInt32(address, 0);

        uint hub;
        Check(CM_Get_Parent(out hub, device, 0), "CM_Get_Parent");
        link.HubId = InstanceId(hub);
        uint up = hub;
        string id = link.HubId;
        for (int i = 0; i < 8 && !id.StartsWith(@"USB\ROOT_HUB", StringComparison.OrdinalIgnoreCase); i++)
        {
            link.ExternalHubs++;
            uint next;
            if (CM_Get_Parent(out next, up, 0) != 0) break;
            up = next;
            id = InstanceId(up);
        }

        using (SafeFileHandle handle = OpenHub(link.HubId))
        {
            ReadConnection(handle, link, deviceId);
            ReadPortInfo(handle, link);
            try { ReadFormats(handle, link); }
            catch (Exception e) { link.FormatError = e.Message; }
        }
        return link;
    }

    static void Check(int result, string what)
    {
        if (result != 0) throw new InvalidOperationException(what + " failed, CR code 0x" + result.ToString("X"));
    }

    static string InstanceId(uint device)
    {
        StringBuilder text = new StringBuilder(200);
        Check(CM_Get_Device_IDW(device, text, 200, 0), "CM_Get_Device_ID");
        return text.ToString();
    }

    static SafeFileHandle OpenHub(string hubId)
    {
        Guid guid = UsbHubInterface;
        uint chars;
        Check(CM_Get_Device_Interface_List_SizeW(out chars, ref guid, hubId, 0), "CM_Get_Device_Interface_List_Size");
        char[] list = new char[chars];
        Check(CM_Get_Device_Interface_ListW(ref guid, hubId, list, chars, 0), "CM_Get_Device_Interface_List");
        string path = new string(list).Split('\0')[0];
        if (path.Length == 0) throw new InvalidOperationException("no hub interface for " + hubId);
        SafeFileHandle handle = CreateFileW(path, 0x40000000, 2, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error(), "open hub " + hubId);
        return handle;
    }

    static void Control(SafeFileHandle handle, uint code, byte[] buffer, string what, out int returned)
    {
        if (!DeviceIoControl(handle, code, buffer, buffer.Length, buffer, buffer.Length, out returned, IntPtr.Zero))
            throw new Win32Exception(Marshal.GetLastWin32Error(), what);
    }

    static void PutUInt(byte[] buffer, int offset, uint value)
    {
        BitConverter.GetBytes(value).CopyTo(buffer, offset);
    }

    static void ReadConnection(SafeFileHandle handle, HwlabUsbLink link, string deviceId)
    {
        byte[] buffer = new byte[1024];
        int returned;
        PutUInt(buffer, 0, (uint)link.Port);
        Control(handle, IoctlConnectionInfoEx, buffer, "IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX", out returned);
        if (BitConverter.ToUInt32(buffer, 31) != 1) throw new InvalidOperationException("hub port " + link.Port + " has no device");
        Match ids = Regex.Match(deviceId, "VID_([0-9A-F]{4})&PID_([0-9A-F]{4})", RegexOptions.IgnoreCase);
        if (ids.Success && (BitConverter.ToUInt16(buffer, 12) != Convert.ToUInt16(ids.Groups[1].Value, 16) ||
                            BitConverter.ToUInt16(buffer, 14) != Convert.ToUInt16(ids.Groups[2].Value, 16)))
            throw new InvalidOperationException("hub port " + link.Port + " holds another device");
        link.UsbVersion = BitConverter.ToUInt16(buffer, 6);
        switch (buffer[23])
        {
            case 0: link.Speed = "Low"; break;
            case 1: link.Speed = "Full"; break;
            case 2: link.Speed = "High"; break;
            case 3: link.Speed = "Super"; break;
            default: link.Speed = "Unknown" + buffer[23]; break;
        }
    }

    static void ReadPortInfo(SafeFileHandle handle, HwlabUsbLink link)
    {
        byte[] buffer = new byte[16];
        int returned;
        PutUInt(buffer, 0, (uint)link.Port);
        PutUInt(buffer, 4, 16);
        PutUInt(buffer, 8, 4);
        if (!DeviceIoControl(handle, IoctlConnectionInfoExV2, buffer, 16, buffer, 16, out returned, IntPtr.Zero))
        {
            link.PortInfoError = Marshal.GetLastWin32Error();
            if (link.UsbVersion >= 0x0300) link.Speed = "Super";
            return;
        }
        uint flags = BitConverter.ToUInt32(buffer, 12);
        link.SuperSpeedCapable = (flags & 2) != 0;
        if ((flags & 1) != 0) link.Speed = "Super";
        link.PortInfo = true;
    }

    static byte[] ReadConfiguration(SafeFileHandle handle, int port, int length)
    {
        byte[] buffer = new byte[12 + length];
        int returned;
        PutUInt(buffer, 0, (uint)port);
        buffer[4] = 0x80;
        buffer[5] = 6;
        buffer[7] = 2;
        buffer[10] = (byte)(length & 0xFF);
        buffer[11] = (byte)(length >> 8);
        Control(handle, IoctlDescriptorFromNode, buffer, "IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION", out returned);
        byte[] data = new byte[Math.Max(0, returned - 12)];
        Array.Copy(buffer, 12, data, 0, data.Length);
        return data;
    }

    static void ReadFormats(SafeFileHandle handle, HwlabUsbLink link)
    {
        byte[] head = ReadConfiguration(handle, link.Port, 9);
        if (head.Length < 9) throw new InvalidOperationException("short configuration descriptor");
        byte[] data = ReadConfiguration(handle, link.Port, BitConverter.ToUInt16(head, 2));
        bool streaming = false;
        string format = null;
        for (int at = 0; at + 2 <= data.Length && data[at] >= 2 && at + data[at] <= data.Length; at += data[at])
        {
            int size = data[at];
            if (data[at + 1] == 4 && size >= 9)
            {
                streaming = data[at + 5] == 14 && data[at + 6] == 2;
            }
            else if (data[at + 1] == 0x24 && streaming && size >= 5)
            {
                int subtype = data[at + 2];
                if (subtype == 6) format = "MJPG";
                else if (subtype == 4 && size >= 21) format = FourCc(data, at + 5);
                else if ((subtype == 5 || subtype == 7) && format != null && size >= 30)
                {
                    uint shortest = BitConverter.ToUInt32(data, at + 26);
                    int count = data[at + 25];
                    for (int i = 1; i < count && at + 26 + 4 * i + 4 <= at + size; i++)
                        shortest = Math.Min(shortest, BitConverter.ToUInt32(data, at + 26 + 4 * i));
                    HwlabUsbFormat item = new HwlabUsbFormat();
                    item.Name = format;
                    item.Width = BitConverter.ToUInt16(data, at + 5);
                    item.Height = BitConverter.ToUInt16(data, at + 7);
                    item.MaxFps = shortest == 0 ? 0 : 10000000.0 / shortest;
                    link.Formats.Add(item);
                }
            }
        }
    }

    static string FourCc(byte[] data, int at)
    {
        StringBuilder text = new StringBuilder();
        for (int i = 0; i < 4; i++) text.Append(data[at + i] >= 0x20 && data[at + i] < 0x7F ? ((char)data[at + i]).ToString() : "?");
        return text.ToString();
    }
}
'@
  try {
    Add-Type -TypeDefinition $source -Language CSharp
  } catch {
    $reason = ($_.Exception.Message -split '\r?\n' | Where-Object { $_.Trim() } | Select-Object -First 1)
    Add-Check 'RGB 카메라 USB' '연결 속도' '주의' "확인하지 못함: C# 도구를 만들지 못했습니다. $reason" 'PC 보안 설정이 막았을 수 있습니다. USB 3 케이블로 허브 없이 PC의 USB 3 포트에 꽂았는지 직접 확인합니다'
    return
  }
  foreach ($camera in $cameras) {
    try {
      $link = [HwlabUsb]::Query($camera.DeviceID)
    } catch {
      $reason = (($(if ($_.Exception.InnerException) { $_.Exception.InnerException.Message } else { $_.Exception.Message })) -split '\r?\n' | Where-Object { $_.Trim() } | Select-Object -First 1)
      Add-Check 'RGB 카메라 USB' '연결 속도' '주의' "확인하지 못함: $reason" 'USB 3 케이블로 허브 없이 PC의 USB 3 포트에 꽂았는지 직접 확인합니다'
      continue
    }
    $speed = switch ($link.Speed) {
      'Super' { 'SuperSpeed(USB 3)' }
      'High' { 'High-Speed(USB 2, 480 Mbps)' }
      'Full' { 'Full-Speed(USB 1.1, 12 Mbps)' }
      'Low' { 'Low-Speed(USB 1.0, 1.5 Mbps)' }
      default { $link.Speed }
    }
    if ($link.Speed -eq 'Super') {
      Add-Check 'RGB 카메라 USB' '연결 속도' '정상' $speed
    } else {
      $capable = if ($link.PortInfo -and $link.SuperSpeedCapable) { '카메라는 USB 3를 지원하지만 ' } else { '' }
      Add-Check 'RGB 카메라 USB' '연결 속도' '문제' $speed "${capable}USB 2 이하 속도로 연결되어 촬영에 필요한 fps를 낼 수 없습니다. 포트가 USB 3여도 케이블, 허브, Type-C 방향 때문에 USB 2 속도가 됩니다. USB 3 케이블로 허브 없이 PC의 USB 3 포트에 바로 꽂고, Type-C 단자는 뒤집어 꽂은 뒤 다시 진단합니다"
    }
    $route = if ($link.ExternalHubs -eq 0) { "PC의 USB 포트에 바로 연결(포트 $($link.Port))" } else { "허브 $($link.ExternalHubs)개를 거침: $($link.HubId)" }
    if (-not $link.PortInfo) { $route += ", SuperSpeed 여부는 장치 설명자(bcdUSB)로 판단함(허브 IOCTL 오류 $($link.PortInfoError))" }
    Add-Check 'RGB 카메라 USB' '연결 경로' '정보' $route
    if ($link.FormatError) {
      Add-Check 'RGB 카메라 USB' '형식과 최대 fps' '주의' "확인하지 못함: $($link.FormatError)"
    } else {
      $uyvy = @($link.Formats | Where-Object { $_.Name -eq 'UYVY' } | Sort-Object Width -Descending | ForEach-Object { "$($_.Width)x$($_.Height) $([math]::Round($_.MaxFps))fps" })
      $value = if ($uyvy) { 'UYVY ' + ($uyvy -join ', ') } else { 'UYVY 형식 없음' }
      $more = @($link.Formats | Where-Object { $_.Name -ne 'UYVY' })
      if ($more) { $value += ", 그 밖의 형식 $($more.Count)개는 아래 목록" }
      Add-Check 'RGB 카메라 USB' '형식과 최대 fps' '정보' $value
      Add-Appendix 'RGB 카메라가 알리는 형식과 최대 fps(USB 설명자)' ($link.Formats | ForEach-Object { "$($_.Name) $($_.Width)x$($_.Height) 최대 $([math]::Round($_.MaxFps, 1))fps" })
    }
  }
}

Invoke-Section 'COM 포트' {
  $ports = @(Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\(COM\d+\)' } | ForEach-Object { $_.Name })
  if ($ports) { Add-Check 'COM 포트' '포트 목록' '정보' ($ports -join ', ') }
  else { Add-Check 'COM 포트' '포트 목록' '주의' '없음' 'PPG와 LED 보드를 꽂았는지, 장치 관리자의 "포트(COM & LPT)"에 보이는지 확인합니다' }
  $ppg = Get-RegValue 'HKCU:\Software\deepmedi\hwlab\ppg' 'identity'
  if ($ppg) { Add-Check 'COM 포트' '앱이 기억한 PPG' '정보' $ppg }
}

Invoke-Section '카메라 권한' {
  $store = 'Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\webcam'
  $policy = Get-RegValue 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\AppPrivacy' 'LetAppsAccessCamera'
  $machine = Get-RegValue "HKLM:\$store" 'Value'
  $user = Get-RegValue "HKCU:\$store" 'Value'
  $desktop = Get-RegValue "HKCU:\$store\NonPackaged" 'Value'
  $value = "정책 $policy, PC $machine, 사용자 $user, 데스크톱 앱 $desktop"
  $hint = 'Windows 설정 > 개인 정보 및 보안 > 카메라에서 "카메라 액세스"와 "데스크톱 앱이 카메라에 액세스하도록 허용"을 켭니다'
  if ($policy -eq 2) { Add-Check '카메라 권한' '카메라 사용 허용' '문제' $value '회사 정책이 카메라를 막고 있습니다. PC 관리자에게 문의합니다' }
  elseif ($machine -eq 'Deny' -or $user -eq 'Deny' -or $desktop -eq 'Deny') { Add-Check '카메라 권한' '카메라 사용 허용' '문제' $value $hint }
  else { Add-Check '카메라 권한' '카메라 사용 허용' '정상' $value }
}

Invoke-Section '저장 공간' {
  $out = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'DiabeticFoot'
  if ($shortcut -and $shortcut.Arguments -match '--out\s+"([^"]+)"') { $out = $Matches[1] }
  $exists = Test-Path -LiteralPath $out
  $shots = if ($exists) { @(Get-ChildItem -LiteralPath $out -Directory -ErrorAction SilentlyContinue).Count } else { 0 }
  Add-Check '저장 공간' '저장 위치' '정보' "$out (있음: $exists, 폴더 ${shots}개)"
  $probe = if ($exists) { $out } else { Split-Path -Qualifier $out }
  $drive = New-Object IO.DriveInfo((Split-Path -Qualifier (Resolve-Path -LiteralPath $probe).ProviderPath))
  $free = $drive.AvailableFreeSpace
  $value = "$($drive.Name) 남은 공간 $(Format-Bytes $free) / 전체 $(Format-Bytes $drive.TotalSize), 촬영 약 $([math]::Floor($free / $ShotBytes))번 분량"
  if ($free -lt $ShotBytes + $ReserveBytes) { Add-Check '저장 공간' '남은 공간' '문제' $value '촬영 한 번을 저장할 공간이 없습니다. 파일을 옮기거나 지웁니다' }
  elseif ($free -lt $LowBytes) { Add-Check '저장 공간' '남은 공간' '주의' $value '10 GB 미만입니다. 촬영 파일을 다른 곳으로 옮기기를 권합니다' }
  else { Add-Check '저장 공간' '남은 공간' '정상' $value }
}

Invoke-Section '드라이버 설치 기록' {
  $log = Join-Path $env:SystemRoot 'INF\setupapi.dev.log'
  if (-not (Test-Path -LiteralPath $log)) { return }
  $lines = [IO.File]::ReadAllLines($log)
  $starts = @(for ($i = 0; $i -lt $lines.Length; $i++) { if ($lines[$i].StartsWith('>>>  [') -and $lines[$i] -match 'i3usbdrv|VID_0547&PID_0080') { $i } })
  if (-not $starts) { return }
  $picked = foreach ($start in ($starts | Select-Object -Last 2)) {
    $end = $start
    while ($end -lt $lines.Length - 1 -and -not $lines[$end].StartsWith('<<<  [Exit status')) { $end++ }
    $lines[$start..$end]
    ''
  }
  Add-Appendix 'setupapi.dev.log 열화상 드라이버 마지막 기록 2개' $picked
}

$order = @{ '문제' = 0; '주의' = 1; '정상' = 2; '정보' = 3 }
$problems = @($checks | Where-Object { $_.State -eq '문제' })
$warnings = @($checks | Where-Object { $_.State -eq '주의' })

$report = New-Object System.Collections.Generic.List[string]
$report.Add("$AppName 진단 보고서")
$report.Add("만든 시각 $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), 진단 프로그램 $DiagVersion")
$report.Add("문제 $($problems.Count)개, 주의 $($warnings.Count)개")
foreach ($c in ($checks | Where-Object { $order[$_.State] -le 1 } | Sort-Object { $order[$_.State] })) {
  $report.Add("  [$($c.State)] $($c.Area), $($c.Item): $($c.Value)")
  if ($c.Hint) { $report.Add("         $($c.Hint)") }
}
foreach ($area in ($checks | ForEach-Object { $_.Area } | Select-Object -Unique)) {
  $report.Add('')
  $report.Add("== $area ==")
  foreach ($c in ($checks | Where-Object { $_.Area -eq $area })) {
    $report.Add("[$($c.State)] $($c.Item): $($c.Value)")
    if ($c.Hint) { $report.Add("       $($c.Hint)") }
  }
}
$report.AddRange($appendix)

$outDir = if ($env:HWLAB_DIAG_OUT) { $env:HWLAB_DIAG_OUT } else { [Environment]::GetFolderPath('Desktop') }
$path = Join-Path $outDir ("hwlab-진단-{0}.txt" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
[IO.File]::WriteAllText($path, ($report -join "`r`n") + "`r`n", (New-Object Text.UTF8Encoding($true)))

foreach ($line in $report) { if (-not $line) { break }; Write-Host $line }
Write-Host ''
Write-Host "보고서를 저장했습니다: $path"
Write-Host '이 파일을 담당자에게 보내 주세요.'
if (-not $env:HWLAB_DIAG_NOOPEN) { Start-Process notepad.exe -ArgumentList "`"$path`"" }
