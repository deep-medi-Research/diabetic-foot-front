hwlab_add_layer(pixel SOURCES rect.cc brightness.cc crop.cc convert.cc)

# 영역의 채널별 RGB 합계와 포화 화소 수(화면 RGB 패널)
target_sources(hwlab_pixel PRIVATE rgb_sums.cc)

# 좌우 영역 기본값, 이동, 검사
target_sources(hwlab_pixel PRIVATE side_regions.cc)

# 셔터 교정 구간을 프레임 내용(같은 프레임 반복)으로 판정
target_sources(hwlab_pixel PRIVATE calibration_watch.cc)

# 미리보기 열화상의 발 대비로 발이 차가운지 판정(측정 품질 안내)
target_sources(hwlab_pixel PRIVATE foot_warmth.cc)
