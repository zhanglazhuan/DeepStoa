# debug/t_display/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Point to drivers/ directory for extra components (aw9523, gdem0397t81p)
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/../../drivers/aw9523
    ${CMAKE_SOURCE_DIR}/../../drivers/gdem0397t81p
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(t_display)
