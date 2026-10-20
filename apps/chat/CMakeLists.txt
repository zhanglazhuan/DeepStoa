idf_component_register(
    SRCS
        "app.c"
        "model.c"
        "view.c"
        "controller.c"
        "storage.c"
        "service/chat_audio_i2s.c"
        "service/chat_svc_mock.c"
        "subpages/view_main.c"
        "subpages/view_history.c"
    INCLUDE_DIRS "." "subpages" "service"
    REQUIRES lvgl
    PRIV_REQUIRES audio appmgr uilv controller wifi
)
