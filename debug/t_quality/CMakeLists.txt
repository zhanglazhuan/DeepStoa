# debug/t_quality/CMakeLists.txt
# E-ink image-quality test for ESP32-S3 DevKit + GDEM0397T81P
# On-board BOOT button (GPIO0) cycles through black / white / 32px checkerboard.
# Tapping the FT6336U touch panel overlays the tap coordinates on the pattern.
# Pins: boards/esp32s3/esp32s3_devkit.h
# Reuses the HW-SPI display driver from debug/t_display (no code copy)
cmake_minimum_required(VERSION 3.16)

# Set target before project()
set(ENV{IDF_TARGET} esp32s3)

# Point to drivers/ft6336 (touch controller)
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/../../drivers/ft6336
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(t_quality)
