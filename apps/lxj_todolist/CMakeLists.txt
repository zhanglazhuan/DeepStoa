idf_component_register(
    SRCS "lxj_todolist_app.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
    PRIV_REQUIRES appmgr controller
)
