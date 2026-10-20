# debug/t_touch/main/CMakeLists.txt
idf_component_register(
    SRCS "t_touch.c" "../../t_display/main/epd_display.c"
    INCLUDE_DIRS "." "../../t_display/main" "../../../boards/esp32s3"
    REQUIRES driver
    PRIV_REQUIRES aw9523 ft6336
)
