idf_component_register(
    SRCS "lxj_legacy_apps.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
    PRIV_REQUIRES appmgr alarm controller wifi uilv battery timeservice esp_timer esp_http_client json chatbot lxj_fiction
)
