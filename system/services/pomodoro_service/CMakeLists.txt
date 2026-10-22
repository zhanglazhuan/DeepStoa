idf_component_register(
    SRCS "pomodoro_service.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl esp_timer
    PRIV_REQUIRES events controller
)
