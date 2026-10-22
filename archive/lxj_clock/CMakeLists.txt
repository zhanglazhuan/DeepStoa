idf_component_register(
    SRCS "lxj_clock_app.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl time
    PRIV_REQUIRES appmgr
)
