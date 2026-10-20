idf_component_register(
    SRCS "lxj_picture_app.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
    PRIV_REQUIRES appmgr controller
)
