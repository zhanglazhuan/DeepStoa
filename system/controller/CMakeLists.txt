# system/controller/CMakeLists.txt
# Ported from D:\Codes\EPOS\epos\drivers — FlashDB + display + touch + SD + vibration

get_filename_component(FDB_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../vendor/FlashDB" ABSOLUTE)

file(GLOB FDB_SRCS ${FDB_ROOT}/src/*.c)
file(GLOB FAL_SRCS ${FDB_ROOT}/port/fal/src/*.c)

idf_component_register(
    SRCS
        ${FDB_SRCS}
        ${FAL_SRCS}
        "fal_flash_esp32_port.c"
        "flash_control.c"
        "../../drivers/gdem0397t81p/Display_EPD_W21.c"
        "display_refresh_policy.c"
        "display_control.c"
        "touch_control.c"
        "sd_control.c"
        "fs_control.c"
        "vibration_control.c"
    INCLUDE_DIRS
        "${FDB_ROOT}/inc"
        "${FDB_ROOT}/port/fal/inc"
        "../../drivers/gdem0397t81p"
        "../../boards/esp32s3"
        "."
    REQUIRES
        driver
        lvgl
        spi_flash
        esp_partition
        esp_rom
        fatfs
        wear_levelling
    PRIV_REQUIRES
        ft6336
        esp_timer
)

idf_build_set_property(COMPILE_OPTIONS "-Wno-error=maybe-uninitialized" APPEND)
