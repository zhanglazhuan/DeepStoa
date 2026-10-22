idf_component_register(
    SRCS "log_system.c" "log_capture.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl events time esp_timer
)
