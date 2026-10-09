idf_component_register(
    SRCS "lxj_audio_app.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl audio
    PRIV_REQUIRES appmgr controller
)
