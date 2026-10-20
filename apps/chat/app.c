#include "esp_system.h"
#include "esp_log.h"
#include <lvgl.h>

#include "app_manager.h"
#include "ui_utils.h"

#include "app.h"
#include "view.h"
#include "controller.h"
#include "model.h"

static const char *TAG = "chat_app";

EPOS_LV_IMG_DECLARE(app_chat_logo);

ChatApp g_chat_app;

static void chat_app_start(lv_obj_t *root, lv_group_t *group) {
    ESP_LOGI(TAG, "into chat_app_start");

    chat_model_init(&g_chat_app);
    chat_controller_init(&g_chat_app);
    chat_view_init(&g_chat_app);

    g_chat_app.controller->model = g_chat_app.model;
    g_chat_app.controller->view = g_chat_app.view;

    PAGE_NAVIGATE_TO((&g_chat_app), PAGE_CHAT_MAIN, NULL);

    ESP_LOGI(TAG, "chat_app_start end");
}

static void chat_app_stop(void) {
    ESP_LOGI(TAG, "chat_app_stop");

    chat_controller_deinit(&g_chat_app);
    chat_view_deinit(&g_chat_app);
    chat_model_deinit(&g_chat_app);

    memset(&g_chat_app, 0, sizeof(ChatApp));
}

static bool chat_app_back(void) {
    return page_navigator_navigate_pop(&g_chat_app.view->page_nav, &g_chat_app);
}

static application_t chat_application = {
    .name = "Chat",
    .icon = EPOS_LV_IMG_USE(app_chat_logo),
    .start_func = chat_app_start,
    .stop_func = chat_app_stop,
    .back_func = chat_app_back,
    .category = APP_CATEGORY_SYSTEM,
};

void chat_init(void) {
    app_manager_add_application(&chat_application);
    ESP_LOGI(TAG, "Chat app registered");
}

