#include "esp_system.h"
#include "esp_log.h"
#include <lvgl.h>

#include "app_manager.h"
#include "lv_epd_region.h"
#include "utils/ui_utils.h"

#include "app.h"
#include "view.h"
#include "controller.h"
#include "model.h"
#include "storage.h"

static const char *TAG = "todolist_app";

EPOS_LV_IMG_DECLARE(app_todolist_logo);

TodoListApp g_todolist_app;

static void todolist_app_start(lv_obj_t *root, lv_group_t *group) {
    ESP_LOGI(TAG, "into todolist_app_start");

    todolist_model_init(&g_todolist_app);
    todolist_controller_init(&g_todolist_app);
    todolist_view_init(&g_todolist_app);

    g_todolist_app.controller->model = g_todolist_app.model;
    g_todolist_app.controller->view = g_todolist_app.view;

    page_navigator_navigate_to(&g_todolist_app.view->page_nav, &g_todolist_app, PAGE_HOME, NULL);

    ESP_LOGI(TAG, "todolist_app_start end");
}

static void todolist_app_stop(void) {
    ESP_LOGI(TAG, "todolist_app_stop");

    /* 局部刷新窗口是钉在某个控件矩形上的，带着它退出 App，
     * launcher 的画面就推不到面板上 */
    if (epd_region_is_active()) epd_region_end();

    todolist_controller_deinit(&g_todolist_app);
    todolist_view_deinit(&g_todolist_app);
    todolist_model_deinit(&g_todolist_app);

    memset(&g_todolist_app, 0, sizeof(TodoListApp));
}

static bool todolist_app_back(void) {
    return page_navigator_navigate_pop(&g_todolist_app.view->page_nav, &g_todolist_app);
}

static application_t todolist_app = {
    .name = "TodoList",
    .icon = EPOS_LV_IMG_USE(app_todolist_logo),
    .start_func = todolist_app_start,
    .stop_func = todolist_app_stop,
    .back_func = todolist_app_back,
    .factory_reset_func = todolist_app_factory_reset,
    .category = APP_CATEGORY_SYSTEM,
};

void todolist_init(void) {
    app_manager_add_application(&todolist_app);
    ESP_LOGI(TAG, "TodoList app registered");
}
