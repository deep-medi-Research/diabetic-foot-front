# app 테스트: 화면 없이 ViewModel(QObject)을 QtTest로 시험합니다. 실행 파일 하나가 시험 클래스 하나입니다.
#
#   cmake -S app -B build/app -DCMAKE_PREFIX_PATH=<Qt 6 경로>
#   cmake --build build/app
#   ctest --test-dir build/app --output-on-failure
find_package(Qt6 6.5 REQUIRED COMPONENTS Test Qml Quick)

# 시험 실행 파일을 만들고 ctest에 등록합니다. SOURCES에는 시험 파일과 시험할 app/src 파일을 함께 줍니다.
# app/src는 hwlab_app 실행 파일에만 들어가므로 시험마다 필요한 파일을 다시 컴파일합니다.
function(hwlab_add_app_test name)
  cmake_parse_arguments(arg "" "" "SOURCES;LIBRARIES" ${ARGN})
  qt_add_executable(${name} ${arg_SOURCES})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
  target_link_libraries(${name} PRIVATE Qt6::Test Qt6::Qml hwlab_platform ${arg_LIBRARIES})
  set_target_properties(${name} PROPERTIES MACOSX_BUNDLE OFF WIN32_EXECUTABLE OFF)
  add_test(NAME ${name} COMMAND ${name})
  # 창을 띄우지 않으므로 화면 세션이 없는 SSH와 CI에서도 돕니다.
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  if(WIN32)
    target_compile_options(${name} PRIVATE /utf-8)
    hwlab_copy_ffmpeg_dlls(${name})
    # Windows는 실행 파일 폴더와 PATH에서 DLL을 찾으므로 Qt DLL 폴더를 PATH 앞에 둡니다.
    set_tests_properties(${name} PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
endfunction()

hwlab_add_app_test(test_rgb_trace SOURCES test_rgb_trace.cc ../src/rgb_trace.h ../src/rgb_trace.cc)

# 명령줄 인자 표: 검사, --help, 로그에 남기는 인자 가리기, docs/debugging.md의 인자 표와 같은지
hwlab_add_app_test(test_cli_args
  SOURCES test_cli_args.cc ../src/cli_args.h ../src/cli_args.cc ../src/app_profile.h ../src/app_profile.cc)
target_compile_definitions(test_cli_args PRIVATE HWLAB_DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../docs")

# 프로필: 어떤 실행이 설치본과 설정을 나누는지, 앱 이름과 창 제목 버전이 어떻게 바뀌는지
hwlab_add_app_test(test_app_profile
  SOURCES test_app_profile.cc ../src/app_profile.h ../src/app_profile.cc ../src/remembered_values.h ../src/remembered_values.cc)

# 로그 파일: 날짜별 파일 이름과 보관 기간, 줄 서식, 느린 콘솔에도 메시지를 내는 쪽이 기다리지 않는 대기열
hwlab_add_app_test(test_app_log
  SOURCES test_app_log.cc ../src/app_log.h ../src/app_log.cc ../src/console_output.h ../src/console_output.cc)

# 환자 정보 입력의 혈압 검사와, 환자를 바꾸면 혈압 칸을 비우는 동작을 시험합니다.
hwlab_add_app_test(test_shot_form
  SOURCES test_shot_form.cc
    ../src/shot_form.h ../src/shot_form.cc ../src/remembered_values.h ../src/remembered_values.cc)

# 열화상 칸의 예열 단계와 렌즈 보정 판정 문장을 시험합니다.
hwlab_add_app_test(test_thermal_view
  SOURCES test_thermal_view.cc ../src/thermal_view.h ../src/thermal_view.cc
  LIBRARIES Qt6::Gui Qt6::Quick)

# 촬영 화면의 종료 순서(녹화 중 닫기, 프레임이 멈춘 카메라)를 가짜 카메라로 시험합니다.
hwlab_add_app_test(test_capture_controller
  SOURCES test_capture_controller.cc
    ../src/capture_controller.h ../src/capture_controller.cc ../src/foot_regions.h ../src/foot_regions.cc
    ../src/light_controller.h ../src/light_controller.cc ../src/ppg_controller.h ../src/ppg_controller.cc
    ../src/readback_model.h ../src/readback_model.cc ../src/remembered_values.h ../src/remembered_values.cc
    ../src/rgb_trace.h ../src/rgb_trace.cc ../src/shot_form.h ../src/shot_form.cc
    ../src/shutdown_watch.h ../src/shutdown_watch.cc ../src/thermal_view.h ../src/thermal_view.cc
  LIBRARIES Qt6::Gui Qt6::Quick)
target_include_directories(test_capture_controller PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/..)
target_compile_definitions(test_capture_controller PRIVATE HWLAB_SPEC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../specs")

# 분석 판 묶음: 판 순서 실행(앞 판이 분석 중이면 뒤 판 보류), 판 켜고 끄기, 기억한 값을 시험합니다.
hwlab_add_app_test(test_analysis_variants
  SOURCES test_analysis_variants.cc
    ../src/analysis_controller.h ../src/analysis_controller.cc ../src/analysis_variants.h ../src/analysis_variants.cc
    ../src/remembered_values.h ../src/remembered_values.cc
  LIBRARIES Qt6::Gui)
