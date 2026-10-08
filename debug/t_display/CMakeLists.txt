# debug/t_display/CMakeLists.txt
# E-ink display test for DeepStoa v1 + GDEM0397T81P
# SPI is direct; DC, RESET and POWER are controlled through AW9523.
cmake_minimum_required(VERSION 3.16)

# Set target before project()
set(ENV{IDF_TARGET} esp32s3)

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/../../drivers/aw9523
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(t_display)
