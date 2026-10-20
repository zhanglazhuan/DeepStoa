idf_component_register(
    SRCS "alarm_service.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl events time controller
    PRIV_REQUIRES esp_timer
)
