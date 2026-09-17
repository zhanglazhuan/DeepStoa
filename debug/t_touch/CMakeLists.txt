# debug/t_touch/CMakeLists.txt
# FT6336U touch controller test
cmake_minimum_required(VERSION 3.16)

set(ENV{IDF_TARGET} esp32s3)

# Point to drivers/ft6336
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/../../drivers/ft6336
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(t_touch)
