// apps/settings/settings_app.c
// Settings app entry — app_manager registration + lifecycle
// Ported from D:\Codes\EPOS\epos\apps\settings\app.c

#include <string.h>
#include <lvgl.h>
#include "esp_log.h"
#include "app_manager.h"
#include "page_navigator.h"
#include "settings_app.h"
#include "settings_view.h"
#include "settings_controller.h"
#include "settings_model.h"

static const char *TAG = "settings_app";

SettingsApp g_settings_app;

// ── Lifecycle callbacks ────────────────────────────────────────────────

static void settings_app_start(lv_obj_t *root, lv_group_t *group)
{
    ESP_LOGI(TAG, "Starting settings app");

    settings_model_init(&g_settings_app);
    settings_controller_init(&g_settings_app);
    settings_view_init(&g_settings_app);

    g_settings_app.controller->model = g_settings_app.model;
    g_settings_app.controller->view  = g_settings_app.view;

    PAGE_NAVIGATE_TO((&g_settings_app), PAGE_HOME, NULL);
}

static void settings_app_stop(void)
{
    ESP_LOGI(TAG, "Stopping settings app");
    settings_controller_deinit(&g_settings_app);
    settings_view_deinit(&g_settings_app);
    settings_model_deinit(&g_settings_app);
    memset(&g_settings_app, 0, sizeof(SettingsApp));
}

static bool settings_app_back(void)
{
    return page_navigator_navigate_pop(&g_settings_app.view->page_nav,
                                       &g_settings_app);
}

static bool settings_app_factory_reset(void)
{
    ESP_LOGI(TAG, "Factory reset requested");
    return true;
}

// ── App descriptor ─────────────────────────────────────────────────────

#include "ui_utils.h"
EPOS_LV_IMG_DECLARE(app_settings_logo);

static application_t s_settings_app = {
    .name                = "Settings",
    .icon                = EPOS_LV_IMG_USE(app_settings_logo),
    .start_func          = settings_app_start,
    .stop_func           = settings_app_stop,
    .back_func           = settings_app_back,
    .factory_reset_func  = settings_app_factory_reset,
    .category            = APP_CATEGORY_SYSTEM,
};

void settings_init(void)
{
    app_manager_add_application(&s_settings_app);
    ESP_LOGI(TAG, "Settings app registered");
}
