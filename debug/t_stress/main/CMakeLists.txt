idf_component_register(
    SRCS "t_stress.c"
    INCLUDE_DIRS "." "../../../boards/esp32s3"
    REQUIRES driver fatfs sdmmc esp_timer esp_driver_tsens aw9523 board_audio gdem0397t81p
)
