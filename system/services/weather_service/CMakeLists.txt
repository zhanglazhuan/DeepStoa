idf_component_register(
    SRCS "weather_service.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_http_client json
)
