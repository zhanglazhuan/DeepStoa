# debug/t_quality/CMakeLists.txt
# E-ink image-quality test for ESP32-S3 DevKit + GDEM0397T81P
# Button (GPIO0) cycles through black / white / 32px checkerboard.
# Reuses the HW-SPI display driver from debug/t_display (no code copy)
cmake_minimum_required(VERSION 3.16)

# Set target before project()
set(ENV{IDF_TARGET} esp32s3)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(t_quality)
