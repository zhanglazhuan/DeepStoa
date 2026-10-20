idf_component_register(
    SRCS "app.c" "model.c" "controller.c" "view.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
    PRIV_REQUIRES appmgr uilv weather_service wifi
)
