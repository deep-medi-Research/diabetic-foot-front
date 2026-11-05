hwlab_add_layer(timing SOURCES frame_gap.cc settle.cc)
target_sources(hwlab_timing PRIVATE clock_map.cc)
target_sources(hwlab_timing PRIVATE record_rate.cc)
target_sources(hwlab_timing PRIVATE window_index.cc)
