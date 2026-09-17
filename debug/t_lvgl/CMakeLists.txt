# debug/t_lvgl/CMakeLists.txt
# LVGL display test for ESP32-S3 DevKit + GDEM0397T81P
# Uses same driver and flush callback as the main DeepStoa project
cmake_minimum_required(VERSION 3.16)

set(ENV{IDF_TARGET} esp32s3)

# Point ESP-IDF to ft6336 component only
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../drivers/ft6336")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(t_lvgl)
