# debug/t_aw9523/main/CMakeLists.txt
idf_component_register(
    SRCS "t_aw9523.c"
    INCLUDE_DIRS "." "../../../boards/esp32s3"
    REQUIRES driver
    PRIV_REQUIRES aw9523
)
