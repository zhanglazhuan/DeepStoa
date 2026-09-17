# debug/t_display/CMakeLists.txt
# E-ink display test for ESP32-S3 DevKit + GDEM0397T81P
# Uses hardware SPI + direct GPIO (no AW9523 IO expander needed)
cmake_minimum_required(VERSION 3.16)

# Set target before project()
set(ENV{IDF_TARGET} esp32s3)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(t_display)
