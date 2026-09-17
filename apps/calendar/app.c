// apps/calendar/app.c
// Calendar app entry — app_manager registration + lifecycle

#include <string.h>
#include <lvgl.h>
#include "esp_log.h"
#include "app_manager.h"
#include "ui_utils.h"
#include "page_navigator.h"
#include "lv_epd_region.h"

#include "app.h"
#include "view.h"
#include "controller.h"
#include "model.h"

static const char *TAG = "calendar_app";

EPOS_LV_IMG_DECLARE(app_calendar_logo);

CalendarApp g_calendar_app;

static void calendar_app_start(lv_obj_t *root, lv_group_t *group)
{
    (void)root;
    (void)group;
    ESP_LOGI(TAG, "calendar_app_start");

    calendar_model_init(&g_calendar_app);
    calendar_controller_init(&g_calendar_app);
    calendar_view_init(&g_calendar_app);

    if (!g_calendar_app.model || !g_calendar_app.controller || !g_calendar_app.view) {
        ESP_LOGE(TAG, "init failed, aborting start");
        return;
    }

    g_calendar_app.controller->model = g_calendar_app.model;
    g_calendar_app.controller->view  = g_calendar_app.view;

    PAGE_NAVIGATE_TO((&g_calendar_app), PAGE_CALENDAR_MONTH, NULL);

    ESP_LOGI(TAG, "calendar_app_start end");
}

static void calendar_app_stop(void)
{
    ESP_LOGI(TAG, "calendar_app_stop");

    /* 必须先退出局部刷新模式：窗口是全局状态，带着它离开会让下一个页面
     * （launcher）只有窗口内那一块能推上面板。
     * 不加 is_active() 判断 —— begin() 是异步的，开完日历马上退出时基准帧
     * 可能还排在队列里；end() 清掉 s_screen 才能让它作废。 */
    epd_region_end();

    calendar_controller_deinit(&g_calendar_app);
    calendar_view_deinit(&g_calendar_app);
    calendar_model_deinit(&g_calendar_app);

    memset(&g_calendar_app, 0, sizeof(CalendarApp));
}

static bool calendar_app_back(void)
{
    if (!g_calendar_app.view) return false;
    return page_navigator_navigate_pop(&g_calendar_app.view->page_nav,
                                       &g_calendar_app);
}

static application_t calendar_app = {
    .name       = "Calendar",
    .icon       = EPOS_LV_IMG_USE(app_calendar_logo),
    .start_func = calendar_app_start,
    .stop_func  = calendar_app_stop,
    .back_func  = calendar_app_back,
    .category   = APP_CATEGORY_TOOLS,
};

void calendar_init(void)
{
    app_manager_add_application(&calendar_app);
    ESP_LOGI(TAG, "Calendar app registered");
}
