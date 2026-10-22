idf_component_register(
    SRCS "wifi_manager.c" "wifi_store.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event nvs_flash driver
    PRIV_REQUIRES controller   # FlashDB 的 g_kvdb
)
