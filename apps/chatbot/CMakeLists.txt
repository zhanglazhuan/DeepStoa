# apps/chatbot/CMakeLists.txt
# 阶段 0：音频与云端都走 mock。硬件到位后换成对应的 real 实现即可，
# controller / view 不需要改动。
#   阶段 1: chatbot_svc_mock.c   -> service/chatbot_svc_http.c
#   阶段 2: chatbot_audio_mock.c -> service/chatbot_audio_i2s.c
idf_component_register(
    SRCS
        "app.c"
        "model.c"
        "view.c"
        "controller.c"
        "storage.c"
        "service/chatbot_audio_mock.c"
        "service/chatbot_svc_mock.c"
        "subpages/view_main.c"
        "subpages/view_history.c"
    INCLUDE_DIRS "." "subpages" "service"
    REQUIRES lvgl
    PRIV_REQUIRES appmgr uilv controller wifi
)
