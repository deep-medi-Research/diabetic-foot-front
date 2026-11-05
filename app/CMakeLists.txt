# app: Qt Quick 화면입니다. 촬영 판단은 core와 platform(Capture)이 하고, 화면은 상태를 그리고 버튼을 넘기기만 합니다.
#
#   cmake -S app -B build/app -G Ninja -DCMAKE_PREFIX_PATH=<Qt 6 경로> -DHWLAB_VENDOR_DLL_DIR=<eCAMFwSw.dll 폴더>
cmake_minimum_required(VERSION 3.24)
project(hwlab_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(HWLAB_PLATFORM_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../platform platform)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(hwlab_app src/main.cc)
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(hwlab_app URI HwLab VERSION 1.0
  SOURCES src/capture_controller.h src/capture_controller.cc src/readback_model.h src/readback_model.cc
  QML_FILES qml/Main.qml qml/Theme.qml qml/Segmented.qml qml/Stepper.qml qml/ReadbackRow.qml qml/RecordButton.qml qml/SmoothValue.qml
  RESOURCES fonts/Pretendard-Regular.otf fonts/Pretendard-SemiBold.otf fonts/Pretendard-Bold.otf)
target_link_libraries(hwlab_app PRIVATE Qt6::Quick Qt6::QuickControls2 hwlab_platform)
target_include_directories(hwlab_app PRIVATE src ${CMAKE_CURRENT_BINARY_DIR})
# 릴리즈 버전입니다. release.ps1이 태그 이름(vX.Y.Z)을 넣고, 개발 빌드는 비워 둡니다. 창 제목과 설치 프로그램이 이 값을 씁니다.
# 헤더 하나로 넣어 버전이 바뀌어도 main.cc만 다시 컴파일합니다.
set(HWLAB_APP_VERSION "" CACHE STRING "앱 버전(vX.Y.Z). 비우면 개발 빌드")
# 빌드한 커밋은 구성할 때 git으로 읽습니다. deploy.ps1과 release.ps1은 빌드마다 다시 구성하므로 커밋이 바뀌면 같이 바뀝니다.
execute_process(COMMAND git rev-parse --short=7 HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE HWLAB_APP_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git status --porcelain --untracked-files=no WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE hwlab_app_changes OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(HWLAB_APP_COMMIT AND hwlab_app_changes)
  string(APPEND HWLAB_APP_COMMIT "-dirty")
endif()
configure_file(src/app_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/app_version.h @ONLY)
if(WIN32)
  target_link_libraries(hwlab_app PRIVATE hwlab_platform_windows)
  target_compile_options(hwlab_app PRIVATE /utf-8)
  target_compile_definitions(hwlab_app PRIVATE HWLAB_HAS_DEVICE_CAMERA _CRT_SECURE_NO_WARNINGS)
  set_target_properties(hwlab_app PROPERTIES WIN32_EXECUTABLE ON)
  # 실행 파일, 창, 작업 표시줄, 바로가기 아이콘(package/icon/README.md)
  target_sources(hwlab_app PRIVATE package/icon/app.rc)
  hwlab_copy_ffmpeg_dlls(hwlab_app)
  if(HWLAB_VENDOR_DLL_DIR)
    add_custom_command(TARGET hwlab_app POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${HWLAB_VENDOR_DLL_DIR}/eCAMFwSw.dll" "$<TARGET_FILE_DIR:hwlab_app>")
  endif()
endif()

# 배포: cmake --install <빌드 폴더> --prefix <폴더> 로 Qt 경로 없이 실행되는 폴더를 만듭니다.
# Qt DLL과 QML 모듈은 Qt 배포 스크립트(windeployqt)가 모읍니다.
include(GNUInstallDirs)
install(TARGETS hwlab_app BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
qt_generate_deploy_qml_app_script(TARGET hwlab_app OUTPUT_SCRIPT hwlab_deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${hwlab_deploy_script})
get_property(hwlab_ffmpeg_dlls GLOBAL PROPERTY HWLAB_FFMPEG_DLLS)
if(hwlab_ffmpeg_dlls)
  install(FILES ${hwlab_ffmpeg_dlls} DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES ${HWLAB_FFMPEG_DIR}/LICENSE.txt DESTINATION licenses RENAME FFmpeg-LICENSE.txt)
endif()
if(WIN32 AND HWLAB_VENDOR_DLL_DIR)
  install(FILES ${HWLAB_VENDOR_DLL_DIR}/eCAMFwSw.dll DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(FILES fonts/Pretendard-LICENSE.txt DESTINATION licenses)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../specs DESTINATION .)

# 환자 정보와 저장 위치 입력
target_sources(hwlab_app PRIVATE src/shot_form.h src/shot_form.cc src/remembered_values.h src/remembered_values.cc)
target_sources(hwlab_app PRIVATE src/shutdown_watch.h src/shutdown_watch.cc src/single_instance.h src/single_instance.cc)

# 명령줄 인자 표(검사, --help, 문서)와 콘솔 출력(Windows는 부모 콘솔에 붙음)
target_sources(hwlab_app PRIVATE src/cli_args.h src/cli_args.cc src/console_output.h src/console_output.cc)
qt_target_qml_sources(hwlab_app QML_FILES qml/Field.qml)

# 열화상 미리보기와 열화상 SDK DLL. SDK 폴더(-DHWLAB_THERMAL_SDK_DIR)를 주지 않은 빌드는 열화상 없이 동작합니다.
target_sources(hwlab_app PRIVATE src/thermal_view.h src/thermal_view.cc)
qt_target_qml_sources(hwlab_app QML_FILES qml/ThermalPane.qml)
hwlab_copy_thermal_dlls(hwlab_app)
if(WIN32 AND HWLAB_THERMAL_SDK_DIR)
  foreach(dll i3system_TE_x64.dll i3system_USB_DLL_V2_2_x64.dll i3system_imgproc_impl_x64.dll)
    install(FILES ${HWLAB_THERMAL_SDK_DIR}/bin/x64/${dll} DESTINATION ${CMAKE_INSTALL_BINDIR})
  endforeach()
  # 열화상 카메라 USB 드라이버입니다. 설치 프로그램이 드라이버 저장소에 넣고, 거절하면 install_thermal_driver.cmd로 나중에 넣습니다.
  install(FILES ../vendor/thermal_expert_driver/i3usbdrv.inf ../vendor/thermal_expert_driver/i3usbdrv.cat DESTINATION drivers/thermal)
  install(FILES ../vendor/thermal_expert_driver/amd64/i3usbdrv.sys DESTINATION drivers/thermal/amd64)
  install(PROGRAMS package/install_thermal_driver.cmd DESTINATION drivers)
  # 설치나 장치 연결이 안 될 때 상태를 텍스트 파일 하나로 모으는 진단 프로그램입니다. 릴리즈에도 따로 올립니다.
  install(PROGRAMS package/hwlab_diagnose.cmd DESTINATION .)
endif()

# 좌우 발 영역 지정과 열화상 안내 시험용 카메라
target_sources(hwlab_app PRIVATE src/foot_regions.h src/foot_regions.cc src/scripted_thermal_camera.h src/scripted_thermal_camera.cc)
qt_target_qml_sources(hwlab_app QML_FILES qml/FootBoxes.qml qml/ThermalPrompt.qml qml/LensShadingPrompt.qml
                      qml/ColdFootPrompt.qml)

# 미리보기 옆 RGB 패널: 두 발 상자와 관찰 영역의 채널별 평균, 포화 비율, 시간 그래프
target_sources(hwlab_app PRIVATE src/rgb_trace.h src/rgb_trace.cc)
set_source_files_properties(qml/RgbText.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_target_qml_sources(hwlab_app QML_FILES qml/RgbPanel.qml qml/RgbChart.qml qml/RgbAreaPicker.qml qml/RgbText.qml)

# 관류 분석 막대: 촬영이 끝나면 관류 분석 프로그램(dfr_worker.py)에 자동으로 분석을 요청하고 진행을 보입니다.
# 분석 판(analysis/variants)마다 하나씩 띄우고, 판 고르기와 판 순서 실행은 AnalysisVariants가 맡습니다.
target_sources(hwlab_app PRIVATE src/analysis_controller.h src/analysis_controller.cc src/analysis_variants.h
  src/analysis_variants.cc)
qt_target_qml_sources(hwlab_app QML_FILES qml/AnalysisBar.qml)

# 관류 분석 결과 창(판정, 관류지수, 같은 시각의 RGB 좌우, 열화상, 관류 지도)
target_sources(hwlab_app PRIVATE src/result_view.h src/result_view.cc)
qt_target_qml_sources(hwlab_app QML_FILES qml/ResultWindow.qml qml/TimeChart.qml)

# LED 조명 화면. 포트 선택, 사람 확인, 밝기 적용은 Capture의 제어 스레드로 보냅니다.
target_sources(hwlab_app PRIVATE src/light_controller.h src/light_controller.cc)
qt_target_qml_sources(hwlab_app QML_FILES qml/LightPanel.qml)
target_sources(hwlab_app PRIVATE src/ppg_controller.h src/ppg_controller.cc src/scripted_ppg_device.h src/scripted_ppg_device.cc)
qt_target_qml_sources(hwlab_app QML_FILES qml/PpgPanel.qml qml/PpgWave.qml)

# 앱 종료 때 녹화 중 확인과 카메라 복원 결과 안내
qt_target_qml_sources(hwlab_app QML_FILES qml/RestoreNotice.qml qml/CloseConfirm.qml)
target_sources(hwlab_app PRIVATE src/scripted_rgb_camera.h src/late_rgb_camera.h)

# 배포 패키지: 관류 분석 프로그램, VC++ 런타임, 실행 명령, 라이선스 고지입니다. 이 레포 밖의 배포 도구가 이 옵션을 채웁니다.
# 관류 분석 프로그램은 실행 파일 폴더 기준 python/python.exe와 analysis/dfr_worker.py로 찾으므로 둘 다 실행 파일 옆에 둡니다.
set(HWLAB_PYTHON_DIR "" CACHE PATH "관류 분석 프로그램용 임베디드 파이썬 폴더(python.exe, 고정 판 패키지 설치됨)")
# 관류 분석 프로그램은 이 레포에 없습니다. 기본값은 비어 있어 분석 없이 빌드하고 설치하며, 배포 패키지는 분석 프로그램 폴더를 이 값으로 넘깁니다.
set(HWLAB_ANALYZER_DIR "" CACHE PATH "관류 분석 프로그램 폴더(dfr_worker.py가 있는 폴더). 비우면 분석 프로그램을 설치하지 않습니다")
# 기본은 1280x720 60 fps, 고정 노출 -9단계입니다(2026-11-03 결정). 60 fps에서 한 장은 16.7 ms라 노출은 -6단계 이하만 됩니다.
# 관류 분석 판 v10이 60 fps 녹화를 두 장씩 평균해 30 fps로 분석합니다.
set(HWLAB_RUN_SPEC "see3cam_manual_720p60.json" CACHE STRING "run_app.cmd가 넘기는 specs/ 아래 촬영 선언 파일")
if(HWLAB_PYTHON_DIR)
  install(DIRECTORY ${HWLAB_PYTHON_DIR}/ DESTINATION ${CMAKE_INSTALL_BINDIR}/python
    PATTERN __pycache__ EXCLUDE PATTERN *.pyc EXCLUDE)
endif()
if(HWLAB_ANALYZER_DIR)
  if(NOT EXISTS "${HWLAB_ANALYZER_DIR}/dfr_worker.py")
    message(FATAL_ERROR "HWLAB_ANALYZER_DIR에 dfr_worker.py가 없습니다: ${HWLAB_ANALYZER_DIR}")
  endif()
  install(DIRECTORY ${HWLAB_ANALYZER_DIR}/ DESTINATION ${CMAKE_INSTALL_BINDIR}/analysis
    PATTERN __pycache__ EXCLUDE PATTERN *.pyc EXCLUDE PATTERN tests EXCLUDE PATTERN test_*.py EXCLUDE)
endif()
if(WIN32)
  # VC++ 런타임은 설치 프로그램(vc_redist) 대신 DLL로 넣습니다. Windows는 실행 파일 폴더에서 DLL을 찾으므로
  # 앱 폴더와 python.exe 폴더 두 곳에 넣습니다. scikit-learn은 OpenMP(vcomp140.dll)가 필요합니다.
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP ON)
  set(CMAKE_INSTALL_OPENMP_LIBRARIES ON)
  include(InstallRequiredSystemLibraries)
  if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(HWLAB_PYTHON_DIR)
      install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION ${CMAKE_INSTALL_BINDIR}/python)
    endif()
  else()
    message(WARNING "VC++ 런타임 DLL을 찾지 못해 배포 폴더에 넣지 않습니다. Visual Studio 개발자 명령 프롬프트에서 구성합니다")
  endif()
  configure_file(package/run_app.cmd.in ${CMAKE_CURRENT_BINARY_DIR}/run_app.cmd @ONLY NEWLINE_STYLE CRLF)
  install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/run_app.cmd DESTINATION .)
endif()
install(FILES package/licenses/NOTICE.txt package/licenses/Apache-2.0.txt package/licenses/LGPL-3.0.txt
  package/licenses/GPL-3.0.txt DESTINATION licenses)

# app 테스트(QtTest). ctest --test-dir <빌드 폴더>로 돌립니다. 배포 빌드(release.ps1, deploy.ps1)는 테스트를 빌드하지 않아도 되므로 끌 수 있습니다.
option(HWLAB_APP_TESTS "app 테스트" ON)
if(HWLAB_APP_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()
