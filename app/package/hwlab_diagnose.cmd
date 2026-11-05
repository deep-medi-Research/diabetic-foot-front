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

# 당뇨발 촬영 진단입니다. 설치, 열화상 드라이버, 장치, 카메라 권한, COM 포트, 저장 공간을 읽기만 하고, 결과를 텍스트 파일 하나로 남깁니다.
# 아무것도 설치하거나 고치지 않습니다. 설치가 실패한 PC에서도 실행할 수 있게 설치 폴더의 파일에 기대지 않습니다.
# HWLAB_DIAG_OUT에 폴더를 주면 보고서를 그 폴더에 쓰고, HWLAB_DIAG_NOOPEN이 있으면 메모장을 열지 않고 멈추지 않습니다(CI용).

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8

$DiagVersion = '2026-11-04'
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

  $logs = @(Get-ChildItem -LiteralPath $env:TEMP -Filter 'Setup Log*.txt' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending)
  if ($logs) {
    Add-Check '설치' '설치 프로그램 기록' '정보' "$($logs[0].Name) ($($logs[0].LastWriteTime))"
    Add-Appendix "설치 프로그램 기록 $($logs[0].Name)" (Get-Content -LiteralPath $logs[0].FullName -Tail 80)
  } else {
    Add-Check '설치' '설치 프로그램 기록' '정보' '없음'
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

Invoke-Section '장치' {
  $all = @(Get-CimInstance Win32_PnPEntity)
  $devices = @(
    @{ Name = 'RGB 카메라(See3CAM)'; Pattern = '^USB\\VID_2560&PID_[0-9A-F]{4}&MI_00'; Hint = 'USB 3 포트에 꽂고 장치 관리자의 "카메라"에 See3CAM이 보이는지 봅니다' },
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
  Add-Check '저장 공간' '저장 위치' '정보' "$out (있음: $exists, 폴더 $shots개)"
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
