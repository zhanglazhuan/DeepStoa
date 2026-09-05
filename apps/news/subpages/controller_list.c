#include <lvgl.h>
#include "esp_log.h"
#include "lv_toast.h"

#include "controller_list.h"
#include "../controller.h"
#include "../view.h"
#include "../model.h"
#include "news_svc.h"

static const char *TAG = "news_ctrl_list";

void news_controller_on_article_clicked(lv_event_t *e)
{
    NewsApp  *app  = lv_event_get_user_data(e);
    lv_obj_t *card = lv_event_get_current_target(e);
    uint8_t   idx  = (uint8_t)(uintptr_t)lv_obj_get_user_data(card);

    ESP_LOGI(TAG, "article %u clicked", (unsigned)idx);
    news_controller_open_article(app, idx);
}

void news_controller_on_refresh_clicked(lv_event_t *e)
{
    NewsApp *app = lv_event_get_user_data(e);
    ESP_LOGI(TAG, "refresh requested");
    news_controller_load_first(app);
}

void news_controller_on_load_more_clicked(lv_event_t *e)
{
    NewsApp *app = lv_event_get_user_data(e);
    ESP_LOGI(TAG, "load more requested");
    news_controller_load_more(app);
}

void news_controller_on_refresh_long_pressed(lv_event_t *e)
{
    (void)e;
    /* 只有 mock 数据源会真的记下这次注入；http 实现里是空函数 */
    news_svc_mock_inject(NEWS_SVC_ERR_NETWORK);
    lv_toast_show("Next request will fail", 1500);
}
