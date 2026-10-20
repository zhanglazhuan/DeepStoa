# debug/t_lvgl/main/CMakeLists.txt
# Custom tabview test — no lv_tabview, no theme
set(EPD_DRIVER_DIR  "${CMAKE_CURRENT_LIST_DIR}/../../t_display/main")
option(T_LVGL_PRODUCTION_DRIVER "Measure production EPD driver in the same LVGL harness" OFF)
set(EPD_DRIVER_SOURCE "${EPD_DRIVER_DIR}/epd_display.c")
if(T_LVGL_PRODUCTION_DRIVER)
    set(EPD_DRIVER_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../../drivers/gdem0397t81p/Display_EPD_W21.c")
endif()
set(UILV_WIDGETS     "${CMAKE_CURRENT_LIST_DIR}/../../../system/uilv/widgets")

idf_component_register(
    SRCS
        "t_lvgl.c"
        "${EPD_DRIVER_SOURCE}"
        "${UILV_WIDGETS}/lv_tab.c"
    INCLUDE_DIRS
        "."
        "${EPD_DRIVER_DIR}"
        "../../../boards/esp32s3"
        "${UILV_WIDGETS}"
    REQUIRES
        driver
        lvgl
    PRIV_REQUIRES
        esp_timer
        aw9523
        ft6336
)
