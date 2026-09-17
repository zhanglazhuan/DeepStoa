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

static const char *TAG = "news_app";

EPOS_LV_IMG_DECLARE(app_news_logo);

NewsApp g_news_app;

static void news_app_start(lv_obj_t *root, lv_group_t *group)
{
    (void)root;
    (void)group;
    ESP_LOGI(TAG, "news_app_start");

    news_model_init(&g_news_app);
    news_controller_init(&g_news_app);
    news_view_init(&g_news_app);

    if (!g_news_app.model || !g_news_app.controller || !g_news_app.view) {
        ESP_LOGE(TAG, "init failed, aborting start");
        return;
    }

    g_news_app.controller->model = g_news_app.model;
    g_news_app.controller->view  = g_news_app.view;

    /* 先上启动封面，首屏在后台拉；拉完了列表页构建时直接从 model 渲染 */
    PAGE_NAVIGATE_TO((&g_news_app), PAGE_NEWS_LAUNCH, NULL);
    news_controller_start(&g_news_app);

    ESP_LOGI(TAG, "news_app_start end");
}

static void news_app_stop(void)
{
    ESP_LOGI(TAG, "news_app_stop");

    news_controller_deinit(&g_news_app);
    news_view_deinit(&g_news_app);
    news_model_deinit(&g_news_app);

    memset(&g_news_app, 0, sizeof(NewsApp));
}

static bool news_app_back(void)
{
    if (!g_news_app.view) return false;
    return page_navigator_navigate_pop(&g_news_app.view->page_nav, &g_news_app);
}

static application_t news_app = {
    .name       = "News",
    .icon       = EPOS_LV_IMG_USE(app_news_logo),
    .start_func = news_app_start,
    .stop_func  = news_app_stop,
    .back_func  = news_app_back,
    .category   = APP_CATEGORY_TOOLS,
};

void news_app_init(void)
{
    app_manager_add_application(&news_app);
    ESP_LOGI(TAG, "News app registered");
}
