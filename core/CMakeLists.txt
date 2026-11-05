cmake_minimum_required(VERSION 3.24)
project(hwlab_core LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(HWLAB_BUILD_TESTS "core 테스트" ON)

if(MSVC)
  set(HWLAB_WARNINGS /W4 /utf-8 /permissive-)
else()
  set(HWLAB_WARNINGS -Wall -Wextra -Wpedantic)
endif()

include(cmake/AddLayer.cmake)

# 루트 어휘. 모든 층이 의존합니다.
add_library(hwlab_vocabulary INTERFACE)
target_include_directories(hwlab_vocabulary INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_subdirectory(src/timing)
add_subdirectory(src/pixel)
add_subdirectory(src/record)
add_subdirectory(src/flow)

if(HWLAB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()
