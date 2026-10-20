# apps/player/CMakeLists.txt
# 曲目库读取 SD 卡上的 /sdcard/music。播放由 system/services/audio 持有，
# 退出 app 后不会中断音频。当前音频输出支持 PCM WAV。
idf_component_register(
    SRCS
        "app.c"
        "model.c"
        "view.c"
        "controller.c"
        "service/player_library.c"
        "subpages/view_list.c"
        "subpages/view_play.c"
    INCLUDE_DIRS "." "subpages" "service"
    REQUIRES lvgl
    PRIV_REQUIRES appmgr uilv controller audio
)
