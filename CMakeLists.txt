# 레포 전체 빌드입니다. core, platform, app과 세 층의 시험을 한 빌드 폴더에서 만듭니다.
# Visual Studio의 폴더 열기와 CMakePresets.json(cmake --preset)이 이 파일로 구성합니다.
# 층마다 따로 구성하는 명령(cmake -S core, -S platform, -S app)도 그대로 됩니다. 그때는 아래층의 시험을 빌드하지 않습니다.
cmake_minimum_required(VERSION 3.24)
project(diabetic_foot_front LANGUAGES CXX)

enable_testing()
# 아래층부터 넣습니다. platform과 app은 아래층 대상이 이미 있으면 다시 넣지 않습니다.
add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(app)
