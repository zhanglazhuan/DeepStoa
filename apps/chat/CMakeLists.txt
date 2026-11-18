idf_component_register(
    SRCS
        "app.c"
        "model.c"
        "view.c"
        "controller.c"
        "storage.c"
        "service/chat_audio_i2s.c"
        "service/chat_svc_mock.c"
        "pages/view_main.c"
        "pages/view_history.c"
    INCLUDE_DIRS "." "pages" "service"
    REQUIRES lvgl
    PRIV_REQUIRES audio appmgr uilv controller wifi
)
