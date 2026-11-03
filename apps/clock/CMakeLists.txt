# apps/clock/CMakeLists.txt
# 闹钟/计时器的数据和逻辑在 system/services/alarm —— 这个 app 只是它的遥控器。
idf_component_register(
    SRCS
        "app.c"
        "model.c"
        "view.c"
        "controller.c"
        "pages/view_main.c"
        "pages/view_alarm.c"
        "pages/view_timer.c"
        "pages/view_alarm_edit.c"
        "pages/view_timer_preset.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl alarm
    PRIV_REQUIRES uilv appmgr controller time
)
