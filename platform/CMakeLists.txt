# platform: 장비 구현, 촬영 파이프라인, 실기 확인 도구입니다. core를 함께 빌드합니다.
# OS와 무관한 부분(파이프라인, 가짜 카메라)은 macOS에서도 빌드하고 테스트합니다.
#
#   cmake -S platform -B build/platform
#   cmake -S platform -B build/platform -G Ninja -DHWLAB_VENDOR_DLL_DIR=<eCAMFwSw.dll 폴더>   (Windows)
cmake_minimum_required(VERSION 3.24)
project(hwlab_platform LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  set(HWLAB_WARNINGS /W4 /utf-8 /permissive-)
else()
  set(HWLAB_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# 레포 루트에서 구성하면 core가 이미 들어와 있습니다. platform만 구성할 때는 core를 시험 없이 넣습니다.
if(NOT TARGET hwlab_vocabulary)
  set(HWLAB_BUILD_TESTS OFF)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core core)
endif()

# 앱이 따르는 분석 API의 버전입니다. 분석 API 저장소의 같은 태그(v<버전>)를 시험에서 받고, 분석 프로그램이 준비 줄로 알린
# 버전의 주 버전과 견줍니다.
set(HWLAB_ANALYSIS_API_VERSION_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../ANALYSIS_API_VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${HWLAB_ANALYSIS_API_VERSION_FILE})
file(STRINGS ${HWLAB_ANALYSIS_API_VERSION_FILE} HWLAB_ANALYSIS_API_VERSION LIMIT_COUNT 1)

add_library(hwlab_platform STATIC
  src/capture.cc
  src/fake_camera.cc
  src/frame_pool.cc
  src/avi_file.cc
  src/reconcile.cc
  src/rgb_camera_message.cc
  src/video_writer.cc)
target_include_directories(hwlab_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(hwlab_platform PUBLIC hwlab_flow hwlab_record hwlab_pixel hwlab_timing)
target_compile_options(hwlab_platform PRIVATE ${HWLAB_WARNINGS})
if(MSVC)
  target_compile_definitions(hwlab_platform PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# FFV1 인코더(libavcodec, libavformat). 제품 빌드는 LGPL 공유 라이브러리를 씁니다.
# macOS의 Homebrew ffmpeg는 GPL 빌드라 테스트에만 씁니다.
set(HWLAB_FFMPEG_DIR "" CACHE PATH "FFmpeg 개발 파일(include, lib, bin) 폴더")
# 환경 변수로 받은 Windows 경로(C:\...)는 /로 바꿔 캐시에 둡니다. app의 설치 규칙도 이 값을 씁니다.
file(TO_CMAKE_PATH "${HWLAB_FFMPEG_DIR}" hwlab_ffmpeg_dir)
set_property(CACHE HWLAB_FFMPEG_DIR PROPERTY VALUE "${hwlab_ffmpeg_dir}")
# 틀린 폴더를 주면 다른 곳의 FFmpeg를 찾거나 FFV1 없이 빌드되므로 먼저 모양을 봅니다.
if(HWLAB_FFMPEG_DIR AND NOT EXISTS "${HWLAB_FFMPEG_DIR}/include/libavcodec/avcodec.h")
  message(FATAL_ERROR "HWLAB_FFMPEG_DIR에 include/libavcodec/avcodec.h가 없습니다: ${HWLAB_FFMPEG_DIR}\n"
    "FFmpeg 공유 빌드 zip을 푼 폴더(include, lib, bin이 바로 아래에 있는 폴더)를 줍니다.")
endif()
find_path(HWLAB_AV_INCLUDE libavcodec/avcodec.h HINTS ${HWLAB_FFMPEG_DIR}/include /opt/homebrew/opt/ffmpeg/include)
find_library(HWLAB_AVCODEC avcodec HINTS ${HWLAB_FFMPEG_DIR}/lib /opt/homebrew/opt/ffmpeg/lib)
find_library(HWLAB_AVFORMAT avformat HINTS ${HWLAB_FFMPEG_DIR}/lib /opt/homebrew/opt/ffmpeg/lib)
find_library(HWLAB_AVUTIL avutil HINTS ${HWLAB_FFMPEG_DIR}/lib /opt/homebrew/opt/ffmpeg/lib)
if(HWLAB_AV_INCLUDE AND HWLAB_AVCODEC AND HWLAB_AVFORMAT AND HWLAB_AVUTIL)
  set(HWLAB_HAS_FFV1 ON)
  target_sources(hwlab_platform PRIVATE src/ffv1_file.cc)
  # 외부 헤더라 경고 검사에서 뺍니다.
  target_include_directories(hwlab_platform SYSTEM PRIVATE ${HWLAB_AV_INCLUDE})
  target_link_libraries(hwlab_platform PRIVATE ${HWLAB_AVFORMAT} ${HWLAB_AVCODEC} ${HWLAB_AVUTIL})
  target_compile_definitions(hwlab_platform PUBLIC HWLAB_HAS_FFV1)
  if(WIN32)
    file(GLOB dlls "${HWLAB_FFMPEG_DIR}/bin/*.dll")
    if(NOT dlls)
      message(FATAL_ERROR "FFmpeg DLL이 없습니다: ${HWLAB_FFMPEG_DIR}/bin\n"
        "빌드한 실행 파일은 이 DLL을 옆에 두어야 뜹니다. 공유(shared) 빌드를 받아 HWLAB_FFMPEG_DIR에 줍니다.")
    endif()
    # app이 platform을 하위 폴더로 넣어도 보이도록 전역 속성에 둡니다.
    set_property(GLOBAL PROPERTY HWLAB_FFMPEG_DLLS ${dlls})
  endif()
  message(STATUS "FFV1 사용: ${HWLAB_AVCODEC}")
elseif(WIN32)
  # specs/의 촬영 선언은 모두 ffv1입니다. FFmpeg 없이 빌드한 앱은 뜨지만 녹화가 "저장 실패"로 끝나므로 구성에서 멈춥니다.
  message(FATAL_ERROR "FFmpeg를 찾지 못했습니다. HWLAB_FFMPEG_DIR: '${HWLAB_FFMPEG_DIR}'\n"
    "Windows 앱은 FFV1로 녹화하므로 FFmpeg LGPL 공유 빌드가 있어야 합니다. "
    "CMakePresets.json으로 구성하면 환경 변수 HWLAB_FFMPEG_DIR에, 직접 구성하면 -DHWLAB_FFMPEG_DIR=<폴더>로 "
    "include, lib, bin이 있는 폴더를 줍니다. 받는 곳은 README.md의 준비물 표에 있습니다.")
else()
  message(WARNING "FFV1 없음: libavcodec을 찾지 못했습니다. FFV1 시험을 빼고, 앱은 FFV1 선언(specs/의 선언 모두)으로 녹화하지 못합니다. "
    "macOS는 brew install ffmpeg로 넣습니다.")
endif()

# 실행 파일 옆에 FFmpeg DLL을 복사합니다.
function(hwlab_copy_ffmpeg_dlls target)
  get_property(dlls GLOBAL PROPERTY HWLAB_FFMPEG_DLLS)
  foreach(dll ${dlls})
    add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:${target}>")
  endforeach()
endfunction()

# 실행 파일 옆에 열화상 SDK DLL을 복사합니다. SDK 폴더를 주지 않은 빌드에서는 아무것도 하지 않습니다.
function(hwlab_copy_thermal_dlls target)
  if(WIN32 AND HWLAB_THERMAL_SDK_DIR)
    foreach(dll i3system_TE_x64.dll i3system_USB_DLL_V2_2_x64.dll i3system_imgproc_impl_x64.dll)
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${HWLAB_THERMAL_SDK_DIR}/bin/x64/${dll}" "$<TARGET_FILE_DIR:${target}>")
    endforeach()
  endif()
endfunction()

if(WIN32)
  set(HWLAB_VENDOR_DLL_DIR "" CACHE PATH "제조사 SDK DLL(eCAMFwSw.dll)이 있는 폴더. 레포에는 vendor/see3cam_24cug_sdk/Win32/Binary/64Bit/HIDLibraries에 있습니다")
  add_library(hwlab_platform_windows STATIC src/windows/rgb_camera_windows.cc)
  target_link_libraries(hwlab_platform_windows PUBLIC hwlab_platform
                        PRIVATE mfplat mf mfreadwrite mfuuid strmiids ole32)
  target_compile_definitions(hwlab_platform_windows PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(hwlab_platform_windows PRIVATE ${HWLAB_WARNINGS})

  add_executable(hwlab_capture tools/hwlab_capture.cc)
  target_link_libraries(hwlab_capture PRIVATE hwlab_platform_windows)
  target_compile_options(hwlab_capture PRIVATE ${HWLAB_WARNINGS})
  target_compile_definitions(hwlab_capture PRIVATE _CRT_SECURE_NO_WARNINGS)
  hwlab_copy_ffmpeg_dlls(hwlab_capture)
  if(HWLAB_VENDOR_DLL_DIR)
    add_custom_command(TARGET hwlab_capture POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${HWLAB_VENDOR_DLL_DIR}/eCAMFwSw.dll" "$<TARGET_FILE_DIR:hwlab_capture>")
  endif()
endif()

option(HWLAB_PLATFORM_TESTS "platform 테스트" ON)
if(HWLAB_PLATFORM_TESTS)
  enable_testing()
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  include(GoogleTest)
  add_executable(test_capture test/test_capture.cc)
  target_link_libraries(test_capture PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_capture PRIVATE ${HWLAB_WARNINGS})
  if(HWLAB_HAS_FFV1)
    target_include_directories(test_capture SYSTEM PRIVATE ${HWLAB_AV_INCLUDE})
    target_link_libraries(test_capture PRIVATE ${HWLAB_AVFORMAT} ${HWLAB_AVCODEC} ${HWLAB_AVUTIL})
    hwlab_copy_ffmpeg_dlls(test_capture)
  endif()
  gtest_discover_tests(test_capture)
endif()

# 촬영 폴더와 매니페스트 쓰기
target_sources(hwlab_platform PRIVATE src/shot_folder.cc)
if(HWLAB_PLATFORM_TESTS)
  add_executable(test_shot_folder test/test_shot_folder.cc)
  target_link_libraries(test_shot_folder PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_shot_folder PRIVATE ${HWLAB_WARNINGS})
  gtest_discover_tests(test_shot_folder)
endif()

# RGB 카메라를 열지 못한 이유를 사용자에게 말하는 문구. 장치 없이 모든 OS에서 테스트합니다.
if(HWLAB_PLATFORM_TESTS)
  add_executable(test_rgb_camera_message test/test_rgb_camera_message.cc)
  target_link_libraries(test_rgb_camera_message PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_rgb_camera_message PRIVATE ${HWLAB_WARNINGS})
  gtest_discover_tests(test_rgb_camera_message)
endif()

# 자식 프로세스 실행(관류 분석 프로그램)과 관류 분석 프로그램 출력 줄 해석입니다. 가짜 관류 분석 프로그램으로 macOS와 Windows에서 시험합니다.
target_sources(hwlab_platform PRIVATE src/perfusion_analysis.cc)
if(WIN32)
  target_sources(hwlab_platform PRIVATE src/windows/child_process_windows.cc)
else()
  target_sources(hwlab_platform PRIVATE src/child_process_posix.cc)
endif()
find_package(Threads REQUIRED)
target_link_libraries(hwlab_platform PUBLIC Threads::Threads)

if(HWLAB_PLATFORM_TESTS)
  add_executable(fake_analyzer test/fake_analyzer.cc)
  target_compile_options(fake_analyzer PRIVATE ${HWLAB_WARNINGS})
  target_compile_definitions(fake_analyzer PRIVATE HWLAB_ANALYSIS_API_VERSION="${HWLAB_ANALYSIS_API_VERSION}")
  if(MSVC)
    target_compile_definitions(fake_analyzer PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()

  add_executable(test_child_process test/test_child_process.cc)
  target_link_libraries(test_child_process PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_child_process PRIVATE ${HWLAB_WARNINGS})
  target_compile_definitions(test_child_process PRIVATE HWLAB_FAKE_ANALYZER="$<TARGET_FILE:fake_analyzer>")
  add_dependencies(test_child_process fake_analyzer)
  hwlab_copy_ffmpeg_dlls(test_child_process)
  gtest_discover_tests(test_child_process)

  add_executable(test_perfusion_analysis test/test_perfusion_analysis.cc)
  target_link_libraries(test_perfusion_analysis PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_perfusion_analysis PRIVATE ${HWLAB_WARNINGS})
  hwlab_copy_ffmpeg_dlls(test_perfusion_analysis)
  gtest_discover_tests(test_perfusion_analysis)
endif()

# 열화상: 장치 인터페이스, 가짜 열화상, 원시값 파일과 확인용 영상과 .npy 저장
target_sources(hwlab_platform PRIVATE src/thermal_camera.cc src/fake_thermal_camera.cc src/thermal_writer.cc)
if(HWLAB_PLATFORM_TESTS)
  add_executable(test_thermal test/test_thermal.cc)
  target_link_libraries(test_thermal PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_thermal PRIVATE ${HWLAB_WARNINGS})
  if(HWLAB_HAS_FFV1)
    hwlab_copy_ffmpeg_dlls(test_thermal)
  endif()
  gtest_discover_tests(test_thermal)
endif()
# 열화상 수신 스레드와 쓰기 스레드(Capture 안에서 RGB와 함께 녹화)
target_sources(hwlab_platform PRIVATE src/thermal_stream.cc)
if(HWLAB_PLATFORM_TESTS)
  add_executable(test_capture_thermal test/test_capture_thermal.cc)
  target_link_libraries(test_capture_thermal PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_capture_thermal PRIVATE ${HWLAB_WARNINGS})
  hwlab_copy_ffmpeg_dlls(test_capture_thermal)
  gtest_discover_tests(test_capture_thermal)
endif()
if(NOT WIN32)
  # 가짜 장치로만 실행하는 촬영 도구입니다(--fake). 실제 카메라는 Windows 빌드에만 있습니다.
  add_executable(hwlab_capture tools/hwlab_capture.cc)
  target_link_libraries(hwlab_capture PRIVATE hwlab_platform)
  target_compile_options(hwlab_capture PRIVATE ${HWLAB_WARNINGS})
endif()

# LED 조명과 직렬 포트. 가짜 포트로 macOS에서도 테스트합니다.
target_sources(hwlab_platform PRIVATE src/light.cc src/fake_serial_port.cc)
if(WIN32)
  target_sources(hwlab_platform_windows PRIVATE src/windows/serial_port_windows.cc)
  target_link_libraries(hwlab_platform_windows PRIVATE setupapi)

  add_executable(hwlab_serial_ports tools/hwlab_serial_ports.cc)
  target_link_libraries(hwlab_serial_ports PRIVATE hwlab_platform_windows)
  target_compile_options(hwlab_serial_ports PRIVATE ${HWLAB_WARNINGS})
endif()
if(HWLAB_PLATFORM_TESTS)
  add_executable(test_light test/test_light.cc)
  target_link_libraries(test_light PRIVATE hwlab_platform GTest::gtest_main)
  if(WIN32)
    target_link_libraries(test_light PRIVATE hwlab_platform_windows)
  endif()
  target_compile_options(test_light PRIVATE ${HWLAB_WARNINGS})
  gtest_discover_tests(test_light)
endif()

# 열화상 카메라(i3 SDK)와 실기 확인 도구. SDK 폴더(include, bin)를 -DHWLAB_THERMAL_SDK_DIR로 줍니다. 레포에는 vendor/thermal_expert_sdk/c++에 있습니다.
# SDK의 클래스 함수는 가상 함수이고 생성 함수는 GetProcAddress로 찾으므로 가져오기 라이브러리를 링크하지 않습니다.
if(WIN32)
  set(HWLAB_THERMAL_SDK_DIR "" CACHE PATH "열화상 SDK 폴더(include, bin/x64). 레포에는 vendor/thermal_expert_sdk/c++에 있습니다")
  if(HWLAB_THERMAL_SDK_DIR)
    target_sources(hwlab_platform_windows PRIVATE src/windows/thermal_camera_windows.cc)
    # 외부 헤더라 경고 검사에서 뺍니다.
    target_include_directories(hwlab_platform_windows SYSTEM PRIVATE ${HWLAB_THERMAL_SDK_DIR}/include)

    add_executable(hwlab_thermal tools/hwlab_thermal.cc)
    target_link_libraries(hwlab_thermal PRIVATE hwlab_platform_windows)
    target_compile_options(hwlab_thermal PRIVATE ${HWLAB_WARNINGS})
    target_compile_definitions(hwlab_thermal PRIVATE _CRT_SECURE_NO_WARNINGS)
    hwlab_copy_ffmpeg_dlls(hwlab_thermal)
    hwlab_copy_thermal_dlls(hwlab_thermal)
    message(STATUS "열화상 SDK: ${HWLAB_THERMAL_SDK_DIR}")
  else()
    message(STATUS "열화상 SDK 없음: HWLAB_THERMAL_SDK_DIR를 주지 않아 실제 열화상 카메라를 빌드하지 않습니다")
  endif()
endif()

# SDK가 없는 Windows 빌드에서도 MakeDeviceThermalCamera가 정의되도록 SDK 유무를 thermal_camera.cc에 알립니다.
if(WIN32 AND HWLAB_THERMAL_SDK_DIR)
  target_compile_definitions(hwlab_platform PRIVATE HWLAB_HAS_THERMAL_SDK)
endif()
if(WIN32)
  hwlab_copy_thermal_dlls(hwlab_capture)
endif()

# 좌우 분할 녹화(영역마다 영상 파일과 사이드카)
if(HWLAB_PLATFORM_TESTS)
  add_executable(test_capture_split test/test_capture_split.cc)
  target_link_libraries(test_capture_split PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_capture_split PRIVATE ${HWLAB_WARNINGS})
  if(HWLAB_HAS_FFV1)
    target_include_directories(test_capture_split SYSTEM PRIVATE ${HWLAB_AV_INCLUDE})
    target_link_libraries(test_capture_split PRIVATE ${HWLAB_AVFORMAT} ${HWLAB_AVCODEC} ${HWLAB_AVUTIL})
    hwlab_copy_ffmpeg_dlls(test_capture_split)
  endif()
  gtest_discover_tests(test_capture_split)
endif()

# 관류 분석 프로그램 프로세스와 요청 대기열. 가짜 관류 분석 프로그램으로 macOS와 Windows에서 시험합니다.
target_sources(hwlab_platform PRIVATE src/analyzer.cc)
set_source_files_properties(src/analyzer.cc PROPERTIES COMPILE_DEFINITIONS HWLAB_ANALYSIS_API_VERSION="${HWLAB_ANALYSIS_API_VERSION}")
if(HWLAB_PLATFORM_TESTS)
  add_executable(test_analyzer test/test_analyzer.cc)
  target_link_libraries(test_analyzer PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_analyzer PRIVATE ${HWLAB_WARNINGS})
  target_compile_definitions(test_analyzer PRIVATE HWLAB_FAKE_ANALYZER="$<TARGET_FILE:fake_analyzer>")
  add_dependencies(test_analyzer fake_analyzer)
  hwlab_copy_ffmpeg_dlls(test_analyzer)
  gtest_discover_tests(test_analyzer DISCOVERY_TIMEOUT 30)
endif()

# 분석 결과 읽기(요약, 촬영 폴더의 매니페스트와 사이드카, 원시값 파일)와 분석결과 폴더 복사, 녹화 영상 번호로 읽기
target_sources(hwlab_platform PRIVATE src/perfusion_summary.cc src/shot_review.cc src/video_reader.cc src/frame_timing.cc)
if(HWLAB_HAS_FFV1)
  target_sources(hwlab_platform PRIVATE src/ffv1_reader.cc)
endif()
if(HWLAB_PLATFORM_TESTS)
  add_executable(test_perfusion_summary test/test_perfusion_summary.cc)
  target_link_libraries(test_perfusion_summary PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_perfusion_summary PRIVATE ${HWLAB_WARNINGS})
  target_compile_definitions(test_perfusion_summary PRIVATE HWLAB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/data")
  hwlab_copy_ffmpeg_dlls(test_perfusion_summary)
  gtest_discover_tests(test_perfusion_summary)
  add_executable(test_frame_timing test/test_frame_timing.cc)
  target_link_libraries(test_frame_timing PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_frame_timing PRIVATE ${HWLAB_WARNINGS})
  hwlab_copy_ffmpeg_dlls(test_frame_timing)
  gtest_discover_tests(test_frame_timing)
  if(HWLAB_HAS_FFV1)
    add_executable(test_video_reader test/test_video_reader.cc)
    target_link_libraries(test_video_reader PRIVATE hwlab_platform GTest::gtest_main)
    target_compile_options(test_video_reader PRIVATE ${HWLAB_WARNINGS})
    hwlab_copy_ffmpeg_dlls(test_video_reader)
    gtest_discover_tests(test_video_reader)
  endif()
endif()

# LED를 촬영 흐름에 넣음: 조명 요청은 제어 스레드에서 보내고 설정 변경 사건으로 안정화를 다시 셉니다. USB 일련번호 해석은 모든 OS에서 빌드합니다.
target_sources(hwlab_platform PRIVATE src/serial_port.cc)
if(WIN32)
  target_link_libraries(hwlab_platform_windows PRIVATE cfgmgr32)
endif()
if(HWLAB_PLATFORM_TESTS)
  add_executable(test_capture_light test/test_capture_light.cc)
  target_link_libraries(test_capture_light PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_capture_light PRIVATE ${HWLAB_WARNINGS})
  hwlab_copy_ffmpeg_dlls(test_capture_light)
  gtest_discover_tests(test_capture_light)

  add_executable(test_light_port test/test_light_port.cc)
  target_link_libraries(test_light_port PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_light_port PRIVATE ${HWLAB_WARNINGS})
  gtest_discover_tests(test_light_port)
endif()

# PPG 장치 메시지(머리, 길이, CRC16) 해석. 직렬 포트 없이 바이트만 다루므로 모든 OS에서 테스트합니다.
target_sources(hwlab_platform PRIVATE src/ppg_wire.cc src/ppg_sensor.cc src/fake_ppg_device.cc)
if(HWLAB_PLATFORM_TESTS)
  add_executable(test_ppg_wire test/test_ppg_wire.cc)
  target_link_libraries(test_ppg_wire PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_ppg_wire PRIVATE ${HWLAB_WARNINGS})
  gtest_discover_tests(test_ppg_wire)

  add_executable(test_ppg_sensor test/test_ppg_sensor.cc)
  target_link_libraries(test_ppg_sensor PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_ppg_sensor PRIVATE ${HWLAB_WARNINGS})
  if(MSVC)
    target_compile_definitions(test_ppg_sensor PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  gtest_discover_tests(test_ppg_sensor)
endif()

# 분석 결과 배열 파일(.npz) 읽기와 표준 발 틀 PI 지도 그리기. 결과 창의 그래프와 지도 칸이 씁니다. 모든 OS에서 테스트합니다.
target_sources(hwlab_platform PRIVATE src/npy_archive.cc src/perfusion_detail.cc src/template_map.cc)
if(HWLAB_PLATFORM_TESTS)
  add_executable(test_npy_archive test/test_npy_archive.cc)
  target_link_libraries(test_npy_archive PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_npy_archive PRIVATE ${HWLAB_WARNINGS})
  target_compile_definitions(test_npy_archive PRIVATE HWLAB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/data")
  hwlab_copy_ffmpeg_dlls(test_npy_archive)
  gtest_discover_tests(test_npy_archive)

  add_executable(test_perfusion_detail test/test_perfusion_detail.cc)
  target_link_libraries(test_perfusion_detail PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_perfusion_detail PRIVATE ${HWLAB_WARNINGS})
  target_compile_definitions(test_perfusion_detail PRIVATE HWLAB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/data")
  hwlab_copy_ffmpeg_dlls(test_perfusion_detail)
  gtest_discover_tests(test_perfusion_detail)

  add_executable(test_template_map test/test_template_map.cc)
  target_link_libraries(test_template_map PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_template_map PRIVATE ${HWLAB_WARNINGS})
  target_compile_definitions(test_template_map PRIVATE HWLAB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/data")
  hwlab_copy_ffmpeg_dlls(test_template_map)
  gtest_discover_tests(test_template_map)
endif()

# 분석 API 저장소의 가짜 분석 프로그램(fake/fake_analyzer.py)을 앱과 같은 명령줄로 띄워 Analyzer와 결과 읽기까지 시험합니다.
# 저장소는 ANALYSIS_API_VERSION의 태그로 받습니다. 파이썬 3.9 이상이 없으면 시험을 건너뜁니다.
if(HWLAB_PLATFORM_TESTS)
  FetchContent_Declare(
    diabetic_foot_api
    GIT_REPOSITORY https://github.com/deep-medi-Research/diabetic-foot-api.git
    GIT_TAG v${HWLAB_ANALYSIS_API_VERSION}
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(diabetic_foot_api)
  find_package(Python3 3.9 COMPONENTS Interpreter)
  get_filename_component(HWLAB_FAKE_ANALYSIS_SCRIPT ${diabetic_foot_api_SOURCE_DIR}/fake/fake_analyzer.py ABSOLUTE)
  add_executable(test_fake_analysis test/test_fake_analysis.cc)
  target_link_libraries(test_fake_analysis PRIVATE hwlab_platform GTest::gtest_main)
  target_compile_options(test_fake_analysis PRIVATE ${HWLAB_WARNINGS})
  target_compile_definitions(test_fake_analysis PRIVATE
    HWLAB_FAKE_ANALYSIS_SCRIPT="${HWLAB_FAKE_ANALYSIS_SCRIPT}"
    HWLAB_FAKE_ANALYSIS_PYTHON="${Python3_EXECUTABLE}")
  hwlab_copy_ffmpeg_dlls(test_fake_analysis)
  gtest_discover_tests(test_fake_analysis DISCOVERY_TIMEOUT 30)
endif()

# 장당 길이(fps)가 빠진 이전 FFV1 MKV의 장을 그대로 옮겨 fps를 적는 도구입니다(2026-10-20 이전 녹화).
if(HWLAB_HAS_FFV1)
  add_executable(hwlab_mkv_rewrite tools/hwlab_mkv_rewrite.cc)
  target_include_directories(hwlab_mkv_rewrite SYSTEM PRIVATE ${HWLAB_AV_INCLUDE})
  target_link_libraries(hwlab_mkv_rewrite PRIVATE ${HWLAB_AVFORMAT} ${HWLAB_AVCODEC} ${HWLAB_AVUTIL})
  target_compile_options(hwlab_mkv_rewrite PRIVATE ${HWLAB_WARNINGS})
  hwlab_copy_ffmpeg_dlls(hwlab_mkv_rewrite)
endif()

# PPG 실기 확인 도구. 실제 직렬 포트는 Windows 빌드에만 있습니다.
if(WIN32)
  add_executable(hwlab_ppg tools/hwlab_ppg.cc)
  target_link_libraries(hwlab_ppg PRIVATE hwlab_platform_windows)
  target_compile_options(hwlab_ppg PRIVATE ${HWLAB_WARNINGS})
endif()
