# i3system 열화상 카메라 USB 드라이버

열화상 카메라(i3system TE-EV1, USB `VID_0547&PID_0080`)가 쓰는 커널 드라이버 패키지입니다. 이 드라이버가 없는 PC에서는 열화상 SDK가 카메라를 열지 못합니다.

| 항목 | 값 |
| --- | --- |
| 파일 | `i3usbdrv.inf`, `i3usbdrv.cat`, `amd64/i3usbdrv.sys` |
| 하드웨어 ID | `USB\VID_0547&PID_0080` |
| 제공자 | Devguru (i3system 장치용) |
| 버전 | 1.0.4.0 (2018-12-13) |
| 서명 | Microsoft Windows Hardware Compatibility Publisher |
| 출처 | 실험 노트북(Windows 11 Pro 26300)의 드라이버 저장소에서 `pnputil /export-driver oem70.inf`로 꺼낸 파일, 2026-11-03 |
| 재배포 | 2026-11-03 i3system의 재배포 허락을 받음 |

| 파일 | SHA-256 |
| --- | --- |
| `i3usbdrv.inf` | `46491234bddd8c5139e99b1cc9b8cbd06faaa2257fe2f917a92d449092466db0` |
| `i3usbdrv.cat` | `d4e1d034bf605e9193c7e8d211a11872b15c2ef1f85722b336f4b71bfefeb8e8` |
| `amd64/i3usbdrv.sys` | `f42ed01e8462bb01dc7d9e9adde22892aee7844b74583504e65019cae47d1013` |

카탈로그(`.cat`) 서명이 `.inf`와 `.sys`의 해시를 덮으므로 파일을 한 바이트라도 바꾸면 Windows가 설치하지 않습니다. 받은 그대로 둡니다. `.inf`에 x86 항목이 있지만 x86 파일은 내보내지 않았고, 설치 프로그램은 x64 PC만 지원합니다.

설치 프로그램(`tools/package/installer.iss`)이 이 폴더를 `C:\hwlab\app\drivers\thermal`에 복사하고 `pnputil /add-driver i3usbdrv.inf /install`로 드라이버 저장소에 넣습니다.
