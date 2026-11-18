# apps/calendar/CMakeLists.txt
# Ported from D:\Codes\EPOS\epos\apps\calendar
idf_component_register(
    SRCS
        "app.c"
        "model.c"
        "controller.c"
        "view.c"
        "pages/view_month.c"
    INCLUDE_DIRS "." "pages"
    REQUIRES lvgl
    PRIV_REQUIRES uilv appmgr
)
