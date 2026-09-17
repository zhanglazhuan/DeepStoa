#include "esp_system.h"
#include "esp_log.h"
#include <lvgl.h>

#include "ui_utils.h"
#include "app_manager.h"
#include "app.h"
#include "view.h"
#include "controller.h"
#include "model.h"
#include "storage.h"

static const char *TAG = "anki_app";

EPOS_LV_IMG_DECLARE(app_anki_logo);

AnkiApp g_anki_app;

static void anki_app_start(lv_obj_t *root, lv_group_t *group) {
    ESP_LOGI(TAG, "Starting Anki App");

    anki_model_init(&g_anki_app);
    anki_controller_init(&g_anki_app);
    anki_view_init(&g_anki_app);

    g_anki_app.controller->model = g_anki_app.model;
    g_anki_app.controller->view = g_anki_app.view;

    PAGE_NAVIGATE_TO((&g_anki_app), PAGE_DECKS, NULL);
}

static void anki_app_stop(void) {
    ESP_LOGI(TAG, "Stopping Anki App");
    anki_controller_deinit(&g_anki_app);
    anki_view_deinit(&g_anki_app);
    anki_model_deinit(&g_anki_app);
    memset(&g_anki_app, 0, sizeof(AnkiApp));
}

static bool anki_app_back(void) {
    return page_navigator_navigate_pop(&g_anki_app.view->page_nav, &g_anki_app);
}

static application_t anki_application = {
    .name = "Anki",
    .icon = EPOS_LV_IMG_USE(app_anki_logo),
    .start_func = anki_app_start,
    .stop_func = anki_app_stop,
    .back_func = anki_app_back,
    .factory_reset_func = anki_app_factory_reset,
    .category = APP_CATEGORY_SYSTEM,
};

void anki_init(void) {
    app_manager_add_application(&anki_application);
    ESP_LOGI(TAG, "Anki app registered");
}
