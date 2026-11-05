hwlab_add_layer(record SOURCES avi.cc sidecar.cc shot.cc DEPENDS timing pixel)
target_sources(hwlab_record PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/analysis_inputs.cc)

# 촬영 크기 추정, 디스크 여유 판정, 경로 길이
target_sources(hwlab_record PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shot_limits.cc)
