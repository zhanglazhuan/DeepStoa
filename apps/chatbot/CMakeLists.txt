idf_component_register(
    SRCS
        "app.c"
        "model.c"
        "view.c"
        "controller.c"
        "storage.c"
        "service/chatbot_audio_i2s.c"
        "service/chatbot_svc_mock.c"
        "subpages/view_main.c"
        "subpages/view_history.c"
    INCLUDE_DIRS "." "subpages" "service"
    REQUIRES lvgl
    PRIV_REQUIRES audio appmgr uilv controller wifi
)
