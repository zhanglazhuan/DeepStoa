idf_component_register(
    SRCS "t_sd.c"
    INCLUDE_DIRS "." "../../../boards/esp32s3"
    REQUIRES driver fatfs sdmmc aw9523
)
