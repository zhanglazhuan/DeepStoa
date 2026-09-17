# apps/todolist/CMakeLists.txt
# Ported from D:\Codes\EPOS\epos\apps\todolist
idf_component_register(
    SRCS
        "app.c"
        "model.c"
        "controller.c"
        "view.c"
        "storage.c"
        "utils.c"
        "modules/controller_done.c"
        "modules/controller_timer.c"
        "modules/controller_todo.c"
        "modules/view_done.c"
        "modules/view_timer.c"
        "modules/view_todo.c"
        "subpages/controller_settings.c"
        "subpages/view_about.c"
        "subpages/view_home.c"
        "subpages/view_launch.c"
        "subpages/view_settings.c"
        "subpages/view_task_form.c"
    INCLUDE_DIRS "." "modules" "subpages"
    REQUIRES lvgl
    PRIV_REQUIRES uilv appmgr controller esp_timer esp_app_format
)

# test.c 是开发期的手势探针和 mock 任务生成器，没有任何调用方，不进量产固件。
# 需要用的时候把它加回上面的 SRCS（或者 idf.py -DTODOLIST_WITH_TEST=1 时自行挂上）。
if(TODOLIST_WITH_TEST)
    target_sources(${COMPONENT_LIB} PRIVATE "test.c")
endif()
