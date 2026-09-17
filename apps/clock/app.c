// apps/clock/app.c
// Clock app entry — app_manager registration + lifecycle
// Ported from D:\Codes\EPOS\epos\apps\clock\app.c

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include <lvgl.h>

#include "app_manager.h"
#include "ui_utils.h"

#include "clock_app.h"
#include "view.h"
#include "controller.h"
#include "model.h"
#include "alarm_service.h"

static const char *TAG = "clock_app";

EPOS_LV_IMG_DECLARE(app_clock_logo);

ClockApp g_clock_app;

static void clock_app_start(lv_obj_t *root, lv_group_t *group)
{
    ESP_LOGI(TAG, "clock_app_start");

    clock_model_init(&g_clock_app);
    clock_controller_init(&g_clock_app);
    clock_view_init(&g_clock_app);

    g_clock_app.controller->model = g_clock_app.model;
    g_clock_app.controller->view  = g_clock_app.view;

    PAGE_NAVIGATE_TO((&g_clock_app), PAGE_CLOCK_MAIN, NULL);

    ESP_LOGI(TAG, "clock_app_start end");
}

static void clock_app_stop(void)
{
    ESP_LOGI(TAG, "clock_app_stop");

    clock_controller_deinit(&g_clock_app);
    clock_view_deinit(&g_clock_app);
    clock_model_deinit(&g_clock_app);

    memset(&g_clock_app, 0, sizeof(ClockApp));
}

static bool clock_app_back(void)
{
    return page_navigator_navigate_pop(&g_clock_app.view->page_nav, &g_clock_app);
}

static application_t clock_app = {
    .name       = "Clock",
    .icon       = EPOS_LV_IMG_USE(app_clock_logo),
    .start_func = clock_app_start,
    .stop_func  = clock_app_stop,
    .back_func  = clock_app_back,
    /* 闹钟数据在 system/alarm 里，恢复出厂交给它 */
    .factory_reset_func = alarm_service_factory_reset,
    .category   = APP_CATEGORY_TOOLS,
};

void clock_app_init(void)
{
    app_manager_add_application(&clock_app);
    ESP_LOGI(TAG, "Clock app registered");
}
