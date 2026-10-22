idf_component_register(
    SRCS "battery.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl events esp_adc esp_driver_gpio
)
