#include <lvgl.h>
#include "esp_log.h"

#include "controller_detail.h"
#include "../controller.h"
#include "../view.h"
#include "../model.h"
#include "news_svc.h"

static const char *TAG = "news_ctrl_detail";

void news_controller_on_detail_back(lv_event_t *e)
{
    NewsApp *app = lv_event_get_user_data(e);
    if (!app || !app->view || !app->controller) return;

    /* 正文还在路上就走人：取消它，免得回来时又把详情状态改花 */
    if (app->controller->detail_req) {
        news_svc_abort(app->controller->detail_req);
        app->controller->detail_req = 0;
        news_model_set_detail_state(app->model, NEWS_LOAD_IDLE, NULL);
    }

    ESP_LOGI(TAG, "leaving detail");
    page_navigator_navigate_pop(&app->view->page_nav, app);
}

void news_controller_on_detail_retry(lv_event_t *e)
{
    NewsApp *app = lv_event_get_user_data(e);
    ESP_LOGI(TAG, "detail retry");
    news_controller_reload_detail(app);
}
