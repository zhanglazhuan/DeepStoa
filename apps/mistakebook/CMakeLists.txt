idf_component_register(
    SRCS "app.c" "model.c" "storage.c" "controller.c" "view.c" "pages/view_list.c" "pages/view_detail.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
    PRIV_REQUIRES appmgr uilv
)
