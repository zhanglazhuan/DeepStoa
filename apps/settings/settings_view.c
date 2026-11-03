// apps/settings/settings_view.c
// Settings view — page_navigator setup + page registration

#include <string.h>
#include <stdlib.h>
#include <lvgl.h>
#include "esp_log.h"
#include "settings_app.h"
#include "settings_view.h"
#include "settings_controller.h"

// Pages
#include "pages/view_home.h"
#include "pages/view_general.h"
#include "pages/view_datetime_edit.h"
#include "pages/view_battery.h"
#include "pages/view_storage.h"
#include "pages/view_security.h"
#include "pages/view_wifi.h"
#include "pages/view_update.h"
#include "pages/view_wallpaper.h"
#include "pages/view_datetime.h"
#include "pages/view_about.h"
#include "pages/view_folder_selector.h"
#include "pages/view_create_passcode.h"
#include "pages/view_verify_passcode.h"
#include "pages/view_update_download.h"
#include "pages/view_wifi_password.h"

static const char *TAG = "settings_view";

void settings_view_init(struct SettingsApp *app)
{
    app->view = (SettingsView *)malloc(sizeof(SettingsView));
    memset(app->view, 0, sizeof(SettingsView));

    page_navigator_page_t *registry =
        (page_navigator_page_t *)malloc(sizeof(page_navigator_page_t) * SETTINGS_PAGE_ID_MAX);
    memset(registry, 0, sizeof(page_navigator_page_t) * SETTINGS_PAGE_ID_MAX);

    page_navigator_init(&app->view->page_nav, registry, SETTINGS_PAGE_ID_MAX, app);

    settings_view_wallpaper_init_registry(app);
    settings_view_datetime_init_registry(app);
    settings_view_about_init_registry(app);
    settings_view_folder_selector_init_registry(app);
    settings_view_create_passcode_init_registry(app);
    settings_view_verify_passcode_init_registry(app);
    settings_view_update_download_init_registry(app);
    settings_view_wifi_password_init_registry(app);
    settings_view_home_init_registry(app);
    settings_view_general_init_registry(app);
    settings_view_datetime_edit_init_registry(app);
    settings_view_battery_init_registry(app);
    settings_view_storage_init_registry(app);
    settings_view_security_init_registry(app);
    settings_view_update_init_registry(app);
    settings_view_wifi_init_registry(app);

    ESP_LOGI(TAG, "View pages initialized");
}

void settings_view_deinit(struct SettingsApp *app)
{
    if (app->view) {
        page_navigator_deinit(&app->view->page_nav);
        free(app->view->page_nav.registry);
        free(app->view);
        app->view = NULL;
    }
}
