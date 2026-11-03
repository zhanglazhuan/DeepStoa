idf_component_register(SRCS "t_wifi_host.c" "frame_parser.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_timer)
