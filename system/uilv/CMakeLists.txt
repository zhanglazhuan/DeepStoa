# system/uilv/CMakeLists.txt — LVGL UI layer (icons, imgs, theme, widgets, utils, framework)
# Ported from D:\Codes\EPOS\epos\epos_lv
#
# Uses file(GLOB_RECURSE) for image/icon sources so that new assets
# are picked up without editing this file.
#
# 注意：GLOB 只在 CMake configure 阶段求值，新增 .c 文件后必须先
#   idf.py reconfigure
# 否则新文件不会进入编译，表现为链接期 undefined reference。
# （不能用 CONFIGURE_DEPENDS —— ESP-IDF 解析组件依赖时以 script mode
#   执行本文件，那里该选项非法。）

file(GLOB_RECURSE SRC_ICONS     "icons/*.c")
file(GLOB_RECURSE SRC_IMGS_28   "imgs/icon_28x28/*.c")
file(GLOB_RECURSE SRC_IMGS_40   "imgs/icon_40x40/*.c")
file(GLOB_RECURSE SRC_LOGO      "imgs/logo_80x80/*.c")
file(GLOB_RECURSE SRC_COVER     "imgs/cover_800x480/*.c")
file(GLOB_RECURSE SRC_WIDGETS   "widgets/*.c")
file(GLOB_RECURSE SRC_THEME     "theme/*.c")
file(GLOB_RECURSE SRC_UTILS     "utils/*.c")
file(GLOB_RECURSE SRC_FRAMEWORK "framework/*.c")

idf_component_register(
    SRCS
        ${SRC_FRAMEWORK}
        ${SRC_WIDGETS}
        ${SRC_THEME}
        ${SRC_UTILS}
        ${SRC_ICONS}
        ${SRC_IMGS_28}
        ${SRC_IMGS_40}
        ${SRC_LOGO}
        ${SRC_COVER}
    INCLUDE_DIRS
        "."
        "framework"
        "widgets"
        "theme"
        "utils"
        "icons"
        "imgs/icon_28x28"
        "imgs/icon_40x40"
        "imgs/logo_80x80"
        "imgs/cover_800x480"
    REQUIRES
        lvgl
        timeservice
        audio
        controller
        events
        logs
)
