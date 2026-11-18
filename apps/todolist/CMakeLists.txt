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
        "pages/controller_done.c"
        "pages/controller_settings.c"
        "pages/controller_timer.c"
        "pages/controller_todo.c"
        "pages/view_done.c"
        "pages/view_timer.c"
        "pages/view_todo.c"
        "pages/view_about.c"
        "pages/view_home.c"
        "pages/view_launch.c"
        "pages/view_settings.c"
        "pages/view_task_form.c"
    INCLUDE_DIRS "." "pages"
    REQUIRES lvgl
    PRIV_REQUIRES uilv appmgr controller esp_timer esp_app_format
)

# test.c 是开发期的手势探针和 mock 任务生成器，没有任何调用方，不进量产固件。
# 需要用的时候把它加回上面的 SRCS（或者 idf.py -DTODOLIST_WITH_TEST=1 时自行挂上）。
if(TODOLIST_WITH_TEST)
    target_sources(${COMPONENT_LIB} PRIVATE "test.c")
endif()
