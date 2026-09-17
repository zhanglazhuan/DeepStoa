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

static const char *TAG = "reader_app";

EPOS_LV_IMG_DECLARE(app_reader_logo);

ReaderApp g_reader_app;

static void reader_app_start(lv_obj_t *root, lv_group_t *group) {
    ESP_LOGI(TAG, "into reader_app_start");

    reader_model_init(&g_reader_app);
    reader_controller_init(&g_reader_app);
    reader_view_init(&g_reader_app);

    g_reader_app.controller->model = g_reader_app.model;
    g_reader_app.controller->view = g_reader_app.view;

    page_navigator_navigate_to(&g_reader_app.view->page_nav, &g_reader_app, PAGE_LIBRARY, NULL);

    ESP_LOGI(TAG, "reader_app_start end");
}

static void reader_app_stop(void) {
    ESP_LOGI(TAG, "reader_app_stop");

    reader_controller_deinit(&g_reader_app);
    reader_view_deinit(&g_reader_app);
    reader_model_deinit(&g_reader_app);

    memset(&g_reader_app, 0, sizeof(ReaderApp));
}

static bool reader_app_back(void) {
    return page_navigator_navigate_pop(&g_reader_app.view->page_nav, &g_reader_app);
}

static application_t reader_app = {
    .name = "Reader",
    .icon = EPOS_LV_IMG_USE(app_reader_logo),
    .start_func = reader_app_start,
    .stop_func = reader_app_stop,
    .back_func = reader_app_back,
    .factory_reset_func = reader_app_factory_reset,
    .category = APP_CATEGORY_SYSTEM,
};

void reader_init(void) {
    app_manager_add_application(&reader_app);
    ESP_LOGI(TAG, "Reader app registered");
}
