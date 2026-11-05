# 의존할 수 있는 층을 DEPENDS로 제한한 core 층 하나를 만듭니다.
#
#   hwlab_add_layer(timing SOURCES frame_gap.cc DEPENDS pixel)
#
# include 경로가 선언한 층으로만 열리므로, 선언하지 않은 층의 헤더를 include하면 빌드가 실패합니다.
function(hwlab_add_layer name)
  cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS" ${ARGN})
  set(target hwlab_${name})
  set(deps hwlab_vocabulary)
  foreach(dep ${ARG_DEPENDS})
    list(APPEND deps hwlab_${dep})
  endforeach()

  add_library(${target} STATIC ${ARG_SOURCES})
  target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(${target} PUBLIC ${deps})
  target_compile_options(${target} PRIVATE ${HWLAB_WARNINGS})
endfunction()
