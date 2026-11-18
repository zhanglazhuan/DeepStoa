# apps/settings/CMakeLists.txt
idf_component_register(
    SRCS
        "settings_app.c"
        "settings_model.c"
        "settings_controller.c"
        "settings_view.c"
        "pages/view_wifi_password.c"
        "pages/view_update_download.c"
        "pages/view_verify_passcode.c"
        "pages/view_create_passcode.c"
        "pages/view_folder_selector.c"
        "pages/view_about.c"
        "pages/view_datetime.c"
        "pages/view_wallpaper.c"
        "pages/view_home.c"
        "pages/view_general.c"
        "pages/view_datetime_edit.c"
        "pages/view_battery.c"
        "pages/view_storage.c"
        "pages/view_security.c"
        "pages/view_update.c"
        "pages/view_wifi.c"
        "pages/controller_wifi.c"
    INCLUDE_DIRS "." "pages"
    REQUIRES driver lvgl
    PRIV_REQUIRES appmgr uilv controller ota wifi time battery
)
