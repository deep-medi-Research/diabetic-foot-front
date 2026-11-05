@echo off
rem 이 파일은 UTF-8입니다. 한글 문장이 깨지지 않게 코드 페이지를 바꿉니다.
chcp 65001 >nul
rem 열화상 카메라 USB 드라이버(i3usbdrv.inf)를 Windows 드라이버 저장소에 넣습니다. 관리자 권한이 없으면 사용자 계정 컨트롤로 다시 실행합니다.
rem 설치 프로그램에서 드라이버 설치를 거절했거나 실패했을 때 더블클릭해 실행합니다. 여러 번 실행해도 결과는 같습니다.
setlocal
set "INF=%~dp0thermal\i3usbdrv.inf"
if not exist "%INF%" (
  echo 드라이버 파일이 없습니다: %INF%
  pause
  exit /b 2
)
fltmc >nul 2>&1
if errorlevel 1 (
  powershell -NoProfile -Command "try { Start-Process -FilePath '%~f0' -Verb RunAs -ErrorAction Stop } catch { exit 1 }"
  if errorlevel 1 (
    echo 관리자 권한을 받지 못해 드라이버를 설치하지 않았습니다. 다시 실행해 사용자 계정 컨트롤에서 "예"를 누릅니다.
    pause
    exit /b 1
  )
  exit /b 0
)
rem 32비트 cmd에서 실행되면 System32가 SysWOW64로 바뀌므로 64비트 pnputil을 Sysnative로 찾습니다.
set "PNPUTIL=%SystemRoot%\System32\pnputil.exe"
if exist "%SystemRoot%\Sysnative\pnputil.exe" set "PNPUTIL=%SystemRoot%\Sysnative\pnputil.exe"
"%PNPUTIL%" /add-driver "%INF%" /install
"%PNPUTIL%" /enum-drivers | findstr /i /c:"i3usbdrv.inf" >nul
if errorlevel 1 (
  echo 열화상 카메라 드라이버를 설치하지 못했습니다. 위 pnputil 출력을 확인합니다.
  pause
  exit /b 1
)
echo 열화상 카메라 드라이버를 설치했습니다. 열화상 카메라를 뽑았다가 다시 꽂고 앱을 엽니다.
pause
exit /b 0
