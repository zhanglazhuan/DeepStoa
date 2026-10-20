# DeepStoa — ESP-IDF v5.5.3 project
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/drivers/aw9523
    ${CMAKE_SOURCE_DIR}/drivers/board_audio
    ${CMAKE_SOURCE_DIR}/drivers/qmi8658
    ${CMAKE_SOURCE_DIR}/drivers/shtc3
    ${CMAKE_SOURCE_DIR}/drivers/es8311
    ${CMAKE_SOURCE_DIR}/drivers/axp2101
    ${CMAKE_SOURCE_DIR}/drivers/pcf85063
    ${CMAKE_SOURCE_DIR}/drivers/ft6336
    ${CMAKE_SOURCE_DIR}/system/controller
    ${CMAKE_SOURCE_DIR}/system/appmgr
    ${CMAKE_SOURCE_DIR}/system/uilv
    ${CMAKE_SOURCE_DIR}/system/launcher
    ${CMAKE_SOURCE_DIR}/system/events
    ${CMAKE_SOURCE_DIR}/system/services/logs
    ${CMAKE_SOURCE_DIR}/system/services/monitor
    ${CMAKE_SOURCE_DIR}/system/services/battery
    ${CMAKE_SOURCE_DIR}/system/services/wifi
    ${CMAKE_SOURCE_DIR}/system/services/time
    ${CMAKE_SOURCE_DIR}/system/services/weather_service
    ${CMAKE_SOURCE_DIR}/system/services/alarm
    ${CMAKE_SOURCE_DIR}/system/services/flash
    ${CMAKE_SOURCE_DIR}/system/services/audio
    ${CMAKE_SOURCE_DIR}/system/services/ota
    ${CMAKE_SOURCE_DIR}/system/services/pomodoro_service
    ${CMAKE_SOURCE_DIR}/apps/settings
    ${CMAKE_SOURCE_DIR}/apps/wallpaper
    ${CMAKE_SOURCE_DIR}/apps/clock
    ${CMAKE_SOURCE_DIR}/apps/player
    ${CMAKE_SOURCE_DIR}/apps/anki
    ${CMAKE_SOURCE_DIR}/apps/calendar
    ${CMAKE_SOURCE_DIR}/apps/chat
    ${CMAKE_SOURCE_DIR}/apps/news
    ${CMAKE_SOURCE_DIR}/apps/reader
    ${CMAKE_SOURCE_DIR}/apps/todolist
    ${CMAKE_SOURCE_DIR}/apps/tovo
    ${CMAKE_SOURCE_DIR}/apps/pomodoro
    ${CMAKE_SOURCE_DIR}/apps/mistakebook
    ${CMAKE_SOURCE_DIR}/apps/weather
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(DeepStoa)

# Workaround GCC ICE (Segfault during IRA pass) in esp_lcd_panel_rgb.c
# triggered by SPIRAM=y inflating code paths. We don't use RGB LCD at all,
# so lower optimization for this file to avoid the compiler crash.
set_source_files_properties(
    "${IDF_PATH}/components/esp_lcd/rgb/esp_lcd_panel_rgb.c"
    PROPERTIES COMPILE_OPTIONS "-O0")
