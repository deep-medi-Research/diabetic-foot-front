include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# MSVC에서 테스트 실행 파일과 같은 C 런타임을 쓰도록 맞춥니다.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(test_timing test_timing.cc)
target_link_libraries(test_timing PRIVATE hwlab_timing GTest::gtest_main)
target_compile_options(test_timing PRIVATE ${HWLAB_WARNINGS})
target_compile_definitions(test_timing PRIVATE HWLAB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
gtest_discover_tests(test_timing)

add_executable(test_pixel test_pixel.cc)
target_link_libraries(test_pixel PRIVATE hwlab_pixel GTest::gtest_main)
target_compile_options(test_pixel PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_pixel)

add_executable(test_record test_record.cc)
target_link_libraries(test_record PRIVATE hwlab_record GTest::gtest_main)
target_compile_options(test_record PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_record)

add_executable(test_sidecar test_sidecar.cc)
target_link_libraries(test_sidecar PRIVATE hwlab_record GTest::gtest_main)
target_compile_options(test_sidecar PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_sidecar)

add_executable(test_flow test_flow.cc)
target_link_libraries(test_flow PRIVATE hwlab_flow GTest::gtest_main)
target_compile_options(test_flow PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_flow)

add_executable(test_settings_plan test_settings_plan.cc)
target_link_libraries(test_settings_plan PRIVATE hwlab_flow GTest::gtest_main)
target_compile_options(test_settings_plan PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_settings_plan)

add_executable(test_capture_spec test_capture_spec.cc)
target_link_libraries(test_capture_spec PRIVATE hwlab_flow GTest::gtest_main)
target_compile_options(test_capture_spec PRIVATE ${HWLAB_WARNINGS})
target_compile_definitions(test_capture_spec PRIVATE HWLAB_SPEC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../specs")
gtest_discover_tests(test_capture_spec)

add_executable(test_shot test_shot.cc)
target_link_libraries(test_shot PRIVATE hwlab_record GTest::gtest_main)
target_compile_options(test_shot PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_shot)

add_executable(test_convert test_convert.cc)
target_link_libraries(test_convert PRIVATE hwlab_pixel GTest::gtest_main)
target_compile_options(test_convert PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_convert)

add_executable(test_clock_map test_clock_map.cc)
target_link_libraries(test_clock_map PRIVATE hwlab_timing GTest::gtest_main)
target_compile_options(test_clock_map PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_clock_map)

add_executable(test_flicker test_flicker.cc)
target_link_libraries(test_flicker PRIVATE hwlab_pixel GTest::gtest_main)
target_compile_options(test_flicker PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_flicker)

add_executable(test_record_rate test_record_rate.cc)
target_link_libraries(test_record_rate PRIVATE hwlab_timing GTest::gtest_main)
target_compile_options(test_record_rate PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_record_rate)

add_executable(test_analysis_inputs test_analysis_inputs.cc)
target_link_libraries(test_analysis_inputs PRIVATE hwlab_record GTest::gtest_main)
target_compile_options(test_analysis_inputs PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_analysis_inputs)

add_executable(test_side_regions test_side_regions.cc)
target_link_libraries(test_side_regions PRIVATE hwlab_pixel GTest::gtest_main)
target_compile_options(test_side_regions PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_side_regions)

add_executable(test_calibration_watch test_calibration_watch.cc)
target_link_libraries(test_calibration_watch PRIVATE hwlab_pixel GTest::gtest_main)
target_compile_options(test_calibration_watch PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_calibration_watch)

add_executable(test_foot_warmth test_foot_warmth.cc)
target_link_libraries(test_foot_warmth PRIVATE hwlab_pixel GTest::gtest_main)
target_compile_options(test_foot_warmth PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_foot_warmth)

add_executable(test_window_index test_window_index.cc)
target_link_libraries(test_window_index PRIVATE hwlab_timing GTest::gtest_main)
target_compile_options(test_window_index PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_window_index)

add_executable(test_shot_limits test_shot_limits.cc)
target_link_libraries(test_shot_limits PRIVATE hwlab_record GTest::gtest_main)
target_compile_options(test_shot_limits PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_shot_limits)

add_executable(test_rgb_sums test_rgb_sums.cc)
target_link_libraries(test_rgb_sums PRIVATE hwlab_pixel GTest::gtest_main)
target_compile_options(test_rgb_sums PRIVATE ${HWLAB_WARNINGS})
gtest_discover_tests(test_rgb_sums)
