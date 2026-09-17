# debug/t_lvgl/main/CMakeLists.txt
# Custom tabview test — no lv_tabview, no theme
set(GDEM_DRIVER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../drivers/gdem0397t81p")
set(UILV_WIDGETS     "${CMAKE_CURRENT_LIST_DIR}/../../../system/uilv/widgets")

idf_component_register(
    SRCS
        "t_lvgl.c"
        "${GDEM_DRIVER_DIR}/Display_EPD_W21.c"
        "${UILV_WIDGETS}/lv_tab.c"
    INCLUDE_DIRS
        "."
        "${GDEM_DRIVER_DIR}"
        "../../../boards/esp32s3"
        "${UILV_WIDGETS}"
    REQUIRES
        driver
        lvgl
    PRIV_REQUIRES
        ft6336
)
