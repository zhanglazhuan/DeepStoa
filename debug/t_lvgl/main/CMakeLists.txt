# debug/t_lvgl/main/CMakeLists.txt
# Custom tabview test — no lv_tabview, no theme
set(EPD_DRIVER_DIR  "${CMAKE_CURRENT_LIST_DIR}/../../t_display/main")
set(UILV_WIDGETS     "${CMAKE_CURRENT_LIST_DIR}/../../../system/uilv/widgets")

idf_component_register(
    SRCS
        "t_lvgl.c"
        "${EPD_DRIVER_DIR}/epd_display.c"
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
        aw9523
        ft6336
)
