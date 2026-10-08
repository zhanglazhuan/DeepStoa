# debug/t_lvgl/CMakeLists.txt
# LVGL display test for DeepStoa v1 + GDEM0397T81P
# Uses same driver and flush callback as the main DeepStoa project
cmake_minimum_required(VERSION 3.16)

set(ENV{IDF_TARGET} esp32s3)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../drivers/aw9523"
    "${CMAKE_CURRENT_LIST_DIR}/../../drivers/ft6336"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(t_lvgl)
