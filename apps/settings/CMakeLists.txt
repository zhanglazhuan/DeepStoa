# apps/settings/CMakeLists.txt
idf_component_register(
    SRCS
        "settings_app.c"
        "settings_model.c"
        "settings_controller.c"
        "settings_view.c"
        "subpages/view_home.c"
        "subpages/view_general.c"
        "subpages/view_battery.c"
        "subpages/view_storage.c"
        "subpages/view_security.c"
        "subpages/view_update.c"
        "subpages/view_wifi.c"
        "submodules/controller_wifi.c"
    INCLUDE_DIRS "." "subpages" "submodules"
    REQUIRES driver lvgl
    PRIV_REQUIRES appmgr uilv controller ota wifi timeservice
)
