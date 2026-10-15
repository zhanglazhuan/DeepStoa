cmake_minimum_required(VERSION 3.16)

# Standalone ESP32-S3 PSRAM diagnostic.
set(ENV{IDF_TARGET} esp32s3)

# Keep this diagnostic independent from unrelated application components.
set(COMPONENTS main esp_psram heap log)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(t_psram)
