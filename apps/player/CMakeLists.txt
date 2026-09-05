# apps/player/CMakeLists.txt
# 当前阶段：SD 卡和喇叭都没有，曲目库和音频输出都走 mock。
# 硬件到位后换掉对应实现即可，controller / view 不需要改动：
#   喇叭:   音频引擎已移到 system/audio（退出 app 不断音）
#   SD 卡:  player_library.c 里的 mock 表删掉，它会自动改读真实目录
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
