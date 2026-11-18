idf_component_register(
    SRCS "app.c" "model.c" "storage.c" "controller.c" "view.c" "pages/view_main.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
    PRIV_REQUIRES appmgr uilv controller
)
