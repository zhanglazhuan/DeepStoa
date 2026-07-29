# debug/t_display/main/CMakeLists.txt
idf_component_register(
    SRCS "t_display.c"
    INCLUDE_DIRS "." "../../../boards/esp32s3"
    REQUIRES driver
)
