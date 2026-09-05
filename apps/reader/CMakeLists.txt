idf_component_register(
    SRCS
        "app.c"
        "controller.c"
        "model.c"
        "storage.c"
        "view.c"
        "subpages/view_library.c"
        "subpages/view_reading.c"
    INCLUDE_DIRS "." "subpages"
    REQUIRES lvgl
    PRIV_REQUIRES uilv appmgr
)
