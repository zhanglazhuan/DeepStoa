# debug/t_aw9523/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Set target before project()
set(ENV{IDF_TARGET} esp32s3)

# Point to drivers/ directory for extra component (aw9523)
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/../../drivers/aw9523
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(t_aw9523)
