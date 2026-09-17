// apps/settings/settings_view.c
// Settings view — page_navigator setup + Phase 1 subpage registration

#include <string.h>
#include <stdlib.h>
#include <lvgl.h>
#include "esp_log.h"
#include "settings_app.h"
#include "settings_view.h"
#include "settings_controller.h"

// Phase 1 subpages
#include "subpages/view_home.h"
#include "subpages/view_general.h"
#include "subpages/view_battery.h"
#include "subpages/view_storage.h"
#include "subpages/view_security.h"
#include "subpages/view_wifi.h"
#include "subpages/view_update.h"

static const char *TAG = "settings_view";

void settings_view_init(struct SettingsApp *app)
{
    app->view = (SettingsView *)malloc(sizeof(SettingsView));
    memset(app->view, 0, sizeof(SettingsView));

    page_navigator_page_t *registry =
        (page_navigator_page_t *)malloc(sizeof(page_navigator_page_t) * SETTINGS_PAGE_ID_MAX);
    memset(registry, 0, sizeof(page_navigator_page_t) * SETTINGS_PAGE_ID_MAX);

    page_navigator_init(&app->view->page_nav, registry, SETTINGS_PAGE_ID_MAX, app);

    // Register Phase 1 subpages
    settings_view_home_init_registry(app);
    settings_view_general_init_registry(app);
    settings_view_battery_init_registry(app);
    settings_view_storage_init_registry(app);
    settings_view_security_init_registry(app);
    settings_view_update_init_registry(app);
    settings_view_wifi_init_registry(app);

    ESP_LOGI(TAG, "View initialized with 6 subpages");
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
