// apps/calendar/controller.c
// Calendar controller — 月份导航与选择器
//
// 这里刻意没有"午夜定时器"。原来那个是 24 小时周期、而且 app 一停就被
// 销毁，实际永远不会触发；就算触发了也是"开 app 后 24h"而不是午夜。
// 现在改成 calendar_view_month_rebuild() 每次重画时顺手重读系统时间：
// 开 app、翻月、按 Today 都会走到，跨天之后第一次交互今天的圈就自己对了。

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include <lvgl.h>

#include "controller.h"
#include "model.h"
#include "view.h"

static const char *TAG = "calendar_ctrl";

/* app 在 calendar_app_stop() 里被 memset，弹层的延迟回调可能晚于它到达 */
static bool alive(CalendarApp *app)
{
    return app && app->model && app->view;
}

/* ── Lifecycle ────────────────────────────────────────────────── */

void calendar_controller_init(CalendarApp *app)
{
    app->controller = malloc(sizeof(CalendarController));
    if (!app->controller) {
        ESP_LOGE(TAG, "Failed to alloc CalendarController");
        return;
    }
    memset(app->controller, 0, sizeof(CalendarController));
}

void calendar_controller_deinit(CalendarApp *app)
{
    if (app->controller) {
        free(app->controller);
        app->controller = NULL;
    }
}

/* ── 月份导航 ─────────────────────────────────────────────────── */

void calendar_controller_on_prev_month(lv_event_t *e)
{
    CalendarApp *app = lv_event_get_user_data(e);
    if (!alive(app)) return;

    calendar_model_prev_month(app->model);
    calendar_view_refresh_month(app);
}

void calendar_controller_on_next_month(lv_event_t *e)
{
    CalendarApp *app = lv_event_get_user_data(e);
    if (!alive(app)) return;

    calendar_model_next_month(app->model);
    calendar_view_refresh_month(app);
}

void calendar_controller_on_today(lv_event_t *e)
{
    CalendarApp *app = lv_event_get_user_data(e);
    if (!alive(app)) return;

    /* 已经在当前月就什么都不做 —— 重画一遍画面完全一样，
     * 却要在墨水屏上白刷一次。Today 按钮是常驻的，这条路径很常见。 */
    calendar_model_refresh_today(app->model);
    if (calendar_model_viewing_today_month(app->model)) {
        ESP_LOGD(TAG, "already on current month, nothing to repaint");
        return;
    }

    calendar_model_go_today(app->model);
    calendar_view_refresh_month(app);
}

/* ── 月份选择器 ───────────────────────────────────────────────── */

void calendar_controller_on_month_title_clicked(lv_event_t *e)
{
    CalendarApp *app = lv_event_get_user_data(e);
    if (!alive(app)) return;

    calendar_view_show_month_picker(app);
}

void calendar_controller_on_picker_year_prev(lv_event_t *e)
{
    CalendarApp *app = lv_event_get_user_data(e);
    if (!alive(app)) return;

    calendar_view_picker_step_year(app, -1);
}

void calendar_controller_on_picker_year_next(lv_event_t *e)
{
    CalendarApp *app = lv_event_get_user_data(e);
    if (!alive(app)) return;

    calendar_view_picker_step_year(app, +1);
}

void calendar_controller_on_month_picked(CalendarApp *app, uint8_t month)
{
    if (!alive(app)) return;

    uint16_t year = app->view->month_ctx.picker_year;
    ESP_LOGI(TAG, "picked %04d-%02d", year, month);

    calendar_model_set_month(app->model, year, month);

    /* 先关弹层再重画：关弹层和新网格落在同一帧里，只刷一次屏。
     * 关闭是异步删除，收尾（清指针 + 重新进局刷模式）在 view 的
     * LV_EVENT_DELETE 回调里做，三条关闭路径共用一份。 */
    calendar_view_close_month_picker(app);
    calendar_view_refresh_month(app);
}
