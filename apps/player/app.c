#include <string.h>
#include <lvgl.h>
#include "esp_log.h"
#include "app_manager.h"
#include "page_navigator.h"
#include "ui_utils.h"

#include "app.h"
#include "view.h"
#include "controller.h"
#include "model.h"

static const char *TAG = "player_app";

EPOS_LV_IMG_DECLARE(app_player_logo);

PlayerApp g_player_app;

static void player_app_start(lv_obj_t *root, lv_group_t *group) {
    ESP_LOGI(TAG, "player_app_start");

    player_model_init(&g_player_app);
    player_controller_init(&g_player_app);
    player_view_init(&g_player_app);

    g_player_app.controller->model = g_player_app.model;
    g_player_app.controller->view = g_player_app.view;

    PAGE_NAVIGATE_TO((&g_player_app), PAGE_PLAYER_LIST, NULL);
}

static void player_app_stop(void) {
    ESP_LOGI(TAG, "player_app_stop");

    player_controller_deinit(&g_player_app);
    player_view_deinit(&g_player_app);
    player_model_deinit(&g_player_app);

    memset(&g_player_app, 0, sizeof(PlayerApp));
}

static bool player_app_back(void) {
    return page_navigator_navigate_pop(&g_player_app.view->page_nav, &g_player_app);
}

static application_t player_application = {
    .name = "Music Player",
    .icon = EPOS_LV_IMG_USE(app_player_logo),
    .start_func = player_app_start,
    .stop_func = player_app_stop,
    .back_func = player_app_back,
    .category = APP_CATEGORY_SYSTEM,
};

void player_init(void) {
    app_manager_add_application(&player_application);
    ESP_LOGI(TAG, "Player app registered");
}
