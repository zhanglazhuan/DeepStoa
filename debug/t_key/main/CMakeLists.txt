idf_component_register(
    SRCS "t_key.c"
    INCLUDE_DIRS "." "../../../boards/esp32s3"
    REQUIRES esp_driver_gpio esp_timer
)
