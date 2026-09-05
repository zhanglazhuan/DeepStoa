# apps/wallpaper/CMakeLists.txt
# Ported from D:\Codes\EPOS\epos\apps\wallpaper
idf_component_register(
    SRCS
        "app.c"
        "model.c"
        "view.c"
        "controller.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
    PRIV_REQUIRES uilv
)
