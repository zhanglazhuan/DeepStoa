idf_component_register(
    SRCS "time_service.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl events flash lwip esp_netif
)
