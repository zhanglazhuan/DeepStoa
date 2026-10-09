# DeepStoa — ESP-IDF v5.5.3 project
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
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
    ${CMAKE_SOURCE_DIR}/system/logs
    ${CMAKE_SOURCE_DIR}/system/monitor
    ${CMAKE_SOURCE_DIR}/system/battery
    ${CMAKE_SOURCE_DIR}/system/wifi
    ${CMAKE_SOURCE_DIR}/system/timeservice
    ${CMAKE_SOURCE_DIR}/system/alarm
    ${CMAKE_SOURCE_DIR}/system/flash
    ${CMAKE_SOURCE_DIR}/system/audio
    ${CMAKE_SOURCE_DIR}/system/ota
    ${CMAKE_SOURCE_DIR}/apps/settings
    ${CMAKE_SOURCE_DIR}/apps/wallpaper
    ${CMAKE_SOURCE_DIR}/apps/clock
    ${CMAKE_SOURCE_DIR}/apps/player
    ${CMAKE_SOURCE_DIR}/apps/anki
    ${CMAKE_SOURCE_DIR}/apps/calendar
    ${CMAKE_SOURCE_DIR}/apps/chatbot
    ${CMAKE_SOURCE_DIR}/apps/news
    ${CMAKE_SOURCE_DIR}/apps/reader
    ${CMAKE_SOURCE_DIR}/apps/todolist
    ${CMAKE_SOURCE_DIR}/apps/lxj_clock
    ${CMAKE_SOURCE_DIR}/apps/lxj_todolist
    ${CMAKE_SOURCE_DIR}/apps/lxj_picture
    ${CMAKE_SOURCE_DIR}/apps/lxj_audio
    ${CMAKE_SOURCE_DIR}/apps/lxj_fiction
    ${CMAKE_SOURCE_DIR}/apps/lxj
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(DeepStoa)

# 把 fs_image/ 打成 FAT 镜像烧进 storage 分区（运行时挂载在 /sdcard）。
# 资源目录是可选的：开发分支可能暂时不携带音频/测试文件，但这不应阻塞
# 固件本体编译。目录恢复后，重新 configure 即会自动恢复镜像生成和烧录。
if(EXISTS "${CMAKE_SOURCE_DIR}/fs_image")
    fatfs_create_spiflash_image(storage fs_image FLASH_IN_PROJECT)
else()
    message(WARNING "fs_image/ is absent; storage image generation is disabled")
endif()

# Workaround GCC ICE (Segfault during IRA pass) in esp_lcd_panel_rgb.c
# triggered by SPIRAM=y inflating code paths. We don't use RGB LCD at all,
# so lower optimization for this file to avoid the compiler crash.
set_source_files_properties(
    "${IDF_PATH}/components/esp_lcd/rgb/esp_lcd_panel_rgb.c"
    PROPERTIES COMPILE_OPTIONS "-O0")
