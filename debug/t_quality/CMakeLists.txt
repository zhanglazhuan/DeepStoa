# debug/t_quality/CMakeLists.txt
# E-ink image-quality test for ESP32-S3 DevKit + GDEM0397T81P
# Button (GPIO0) cycles through 32px grid / black / white.
# Keeps the DevKit display transport local to this test project.
cmake_minimum_required(VERSION 3.16)

# Set target before project()
set(ENV{IDF_TARGET} esp32s3)
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../../drivers/ft6336
)

# Point to the DeepStoa v1 I/O expander and touch controller drivers.
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../../drivers/ft6336
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(t_quality)
