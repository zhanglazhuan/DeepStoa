# system/audio/CMakeLists.txt
# 系统级音频服务：播放会话比 app 活得久，退出播放器 app 不会断音。
# 喇叭 / codec 到位后把 audio_out_mock.c 换成 audio_out_i2s.c，
# audio_service 和所有 UI 都不需要改。
idf_component_register(
    SRCS
        "audio_service.c"
        "audio_out_mock.c"
        "audio_meta.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl esp_timer
    PRIV_REQUIRES controller events
)
