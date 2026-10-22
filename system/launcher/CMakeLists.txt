idf_component_register(
    SRCS "launcher_home_ui.c" "launcher.c" "launcher_gesture.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl appmgr uilv controller wifi
)
