# debug/t_touch/main/CMakeLists.txt
idf_component_register(
    SRCS "t_touch.c"
    INCLUDE_DIRS "." "../../../boards/esp32s3"
    REQUIRES driver
    PRIV_REQUIRES ft6336
)
