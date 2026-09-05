#include "esp_system.h"
#include "esp_log.h"
#include <lvgl.h>

#include "app_manager.h"
#include "ui_utils.h"

#include "app.h"
#include "view.h"
#include "controller.h"
#include "model.h"

static const char *TAG = "chatbot_app";

EPOS_LV_IMG_DECLARE(app_chatbot_logo);

ChatbotApp g_chatbot_app;

static void chatbot_app_start(lv_obj_t *root, lv_group_t *group) {
    ESP_LOGI(TAG, "into chatbot_app_start");

    chatbot_model_init(&g_chatbot_app);
    chatbot_controller_init(&g_chatbot_app);
    chatbot_view_init(&g_chatbot_app);

    g_chatbot_app.controller->model = g_chatbot_app.model;
    g_chatbot_app.controller->view = g_chatbot_app.view;

    PAGE_NAVIGATE_TO((&g_chatbot_app), PAGE_CHATBOT_MAIN, NULL);

    ESP_LOGI(TAG, "chatbot_app_start end");
}

static void chatbot_app_stop(void) {
    ESP_LOGI(TAG, "chatbot_app_stop");

    chatbot_controller_deinit(&g_chatbot_app);
    chatbot_view_deinit(&g_chatbot_app);
    chatbot_model_deinit(&g_chatbot_app);

    memset(&g_chatbot_app, 0, sizeof(ChatbotApp));
}

static bool chatbot_app_back(void) {
    return page_navigator_navigate_pop(&g_chatbot_app.view->page_nav, &g_chatbot_app);
}

static application_t chatbot_application = {
    .name = "AI Chatbot",
    .icon = EPOS_LV_IMG_USE(app_chatbot_logo),
    .start_func = chatbot_app_start,
    .stop_func = chatbot_app_stop,
    .back_func = chatbot_app_back,
    .category = APP_CATEGORY_SYSTEM,
};

void chatbot_init(void) {
    app_manager_add_application(&chatbot_application);
    ESP_LOGI(TAG, "Chatbot app registered");
}
