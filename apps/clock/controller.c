// apps/clock/controller.c
// Clock controller —— 事件回调，业务全部委派给 system/alarm。
//
// 这里不再有闹钟轮询和倒计时 tick：
//   - 闹钟由 alarm_service 订阅 APP_EVENT_CLOCK_TICK 触发，app 关掉照样响；
//   - 倒计时由 alarm_service 自己走，app 关掉照样在跑。
// 本文件唯一的定时器是 ui_tick，只负责把剩余时间画到屏幕上，
// 而且只在 Timer 页活着的时候存在。

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lvgl.h>
#include "esp_log.h"

#include "alarm_service.h"
#include "controller.h"
#include "model.h"
#include "subpages/view_alarm_edit.h"
#include "subpages/view_main.h"
#include "view.h"

static const char *TAG = "clock_ctrl";

/* ── Timer 页刷新 ────────────────────────────────────────────────────── */

static void ui_tick_cb(lv_timer_t *t)
{
    ClockApp *app = lv_timer_get_user_data(t);
    if (!app || !app->view) return;
    clock_view_refresh_timer(app);
}

void clock_controller_timer_ui_attach(ClockApp *app)
{
    if (!app->controller || app->controller->ui_tick) return;
    /* 1 Hz 只是采样频率；真正要不要推屏由 clock_view_refresh_timer()
     * 判断文本有没有变（见 view.c）。 */
    app->controller->ui_tick = lv_timer_create(ui_tick_cb, 1000, app);
}

void clock_controller_timer_ui_detach(ClockApp *app)
{
    if (!app->controller || !app->controller->ui_tick) return;
    lv_timer_del(app->controller->ui_tick);
    app->controller->ui_tick = NULL;
}

/* ── Lifecycle ───────────────────────────────────────────────────────── */

void clock_controller_init(ClockApp *app)
{
    app->controller = malloc(sizeof(ClockController));
    if (!app->controller) {
        ESP_LOGE(TAG, "Failed to alloc ClockController");
        return;
    }
    memset(app->controller, 0, sizeof(ClockController));
}

void clock_controller_deinit(ClockApp *app)
{
    if (!app->controller) return;
    clock_controller_timer_ui_detach(app);
    free(app->controller);
    app->controller = NULL;
}

/* ── 闹钟列表 ────────────────────────────────────────────────────────── */

void clock_controller_on_add_alarm(lv_event_t *e)
{
    ClockApp *app = lv_event_get_user_data(e);

    if (alarm_service_count() >= ALARM_MAX_COUNT) {
        clock_view_main_show_max_alarm_warning();
        return;
    }

    app->model->edit_alarm_idx = -1;   /* -1 = 新建 */
    PAGE_NAVIGATE_TO(app, PAGE_ALARM_EDIT, NULL);
}

void clock_controller_on_alarm_toggle(lv_event_t *e)
{
    (void)lv_event_get_user_data(e);
    lv_obj_t *sw = lv_event_get_target(e);
    uint8_t idx  = (uint8_t)(uintptr_t)lv_obj_get_user_data(sw);

    /* LVGL 在发 VALUE_CHANGED 之前已经把 CHECKED 翻过来了 */
    bool shown = lv_obj_has_state(sw, LV_STATE_CHECKED);

    /* 服务内部只写 4 字节的开关位图，不重写整张闹钟表 */
    if (!alarm_service_toggle(idx)) {
        /* 服务没接受，把开关拨回去 —— 否则 UI 显示的状态和实际不符，
         * 用户以为关掉了，闹钟照响。原来这个返回值是被丢掉的。 */
        ESP_LOGW(TAG, "alarm_service_toggle(%u) rejected", (unsigned)idx);
        if (shown) lv_obj_remove_state(sw, LV_STATE_CHECKED);
        else       lv_obj_add_state(sw, LV_STATE_CHECKED);
        return;
    }

    /* 以服务为准回读，别假设"翻一下"两边就一定同步 */
    const alarm_t *a = alarm_service_get(idx);
    bool on = a ? a->enabled : shown;
    if (on != shown) {
        if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
        else    lv_obj_remove_state(sw, LV_STATE_CHECKED);
    }

    /* 删除线立刻跟上。原来这里只改模型不动 UI，删除线要等下次重建列表
     * （重进页面）才变，看起来就是"开关动了、文字没反应"。 */
    clock_view_main_set_row_enabled(sw, on);
}

void clock_controller_on_alarm_row_clicked(lv_event_t *e)
{
    ClockApp *app = lv_event_get_user_data(e);
    lv_obj_t *row = lv_event_get_target(e);
    uint8_t idx   = (uint8_t)(uintptr_t)lv_obj_get_user_data(row);

    app->model->edit_alarm_idx = (int8_t)idx;
    PAGE_NAVIGATE_TO(app, PAGE_ALARM_EDIT, NULL);
}

/* ── 闹钟编辑 ────────────────────────────────────────────────────────── */

void clock_controller_on_alarm_save(lv_event_t *e)
{
    ClockApp     *app = lv_event_get_user_data(e);
    AlarmEditCtx *ctx = &app->view->edit_ctx;
    ClockModel   *m   = app->model;

    int h   = atoi(lv_textarea_get_text(ctx->ta_hour));
    int min = atoi(lv_textarea_get_text(ctx->ta_min));
    /* 兜底钳位，和 view_alarm_edit.c 的 ta_event_cb 保持同一套语义：
     * 越界压到边界，不是清零。正常路径下输入框失焦时已经改好了，
     * 这里只挡住"没走失焦就保存"之类的漏网情况。 */
    if (h < 0)    h = 0;
    if (h > 23)   h = 23;
    if (min < 0)  min = 0;
    if (min > 59) min = 59;

    /* 位掩码的 bit0 是周日，而界面上第一列是周一 —— 这张表负责换算 */
    static const uint8_t day_bits[7] = {
        ALARM_REPEAT_MON, ALARM_REPEAT_TUE, ALARM_REPEAT_WED,
        ALARM_REPEAT_THU, ALARM_REPEAT_FRI, ALARM_REPEAT_SAT,
        ALARM_REPEAT_SUN,
    };
    uint8_t repeat = ALARM_REPEAT_NONE;
    for (int i = 0; i < 7; i++) {
        if (ctx->repeat_btns[i] &&
            lv_obj_has_state(ctx->repeat_btns[i], LV_STATE_CHECKED)) {
            repeat |= day_bits[i];
        }
    }

    alarm_t a = {
        .hour    = (uint8_t)h,
        .minute  = (uint8_t)min,
        .repeat  = repeat,
        .enabled = true,
    };
    if (ctx->label_input) {
        snprintf(a.label, sizeof(a.label), "%s", lv_textarea_get_text(ctx->label_input));
    }

    if (m->edit_alarm_idx < 0) {
        alarm_service_add(&a);
    } else {
        /* 编辑已有条目：保留原来的启用状态和标签（标签没输入框时不覆盖），
         * 原来的实现在这里把 label 无条件写成空串，把出厂默认名抹掉了。 */
        const alarm_t *old = alarm_service_get((uint8_t)m->edit_alarm_idx);
        if (old && !ctx->label_input) {
            snprintf(a.label, sizeof(a.label), "%s", old->label);
        }
        alarm_service_update((uint8_t)m->edit_alarm_idx, &a);
    }

    ESP_LOGI(TAG, "save alarm %02d:%02d repeat=0x%02X", h, min, repeat);
    page_navigator_navigate_pop(&app->view->page_nav, app);
}

void clock_controller_on_alarm_delete(lv_event_t *e)
{
    ClockApp   *app = lv_event_get_user_data(e);
    ClockModel *m   = app->model;

    if (m->edit_alarm_idx >= 0) {
        alarm_service_remove((uint8_t)m->edit_alarm_idx);
        m->edit_alarm_idx = -1;
    }
    page_navigator_navigate_pop(&app->view->page_nav, app);
}

/* ── 倒计时 ──────────────────────────────────────────────────────────── */

void clock_controller_on_timer_start_pause(lv_event_t *e)
{
    ClockApp *app = lv_event_get_user_data(e);

    switch (alarm_service_timer_state()) {
    case ALARM_TIMER_IDLE:
        /* 时长已经由 chip 选好了，这里只管开始 —— 不再需要从三个输入框
         * 里现读一遍。 */
        alarm_service_timer_start();
        break;
    case ALARM_TIMER_RUNNING:
        alarm_service_timer_pause();
        break;
    case ALARM_TIMER_PAUSED:
        alarm_service_timer_start();
        break;
    case ALARM_TIMER_FINISHED:
        alarm_service_timer_reset();
        break;
    }

    clock_view_refresh_timer(app);
}

void clock_controller_on_timer_reset(lv_event_t *e)
{
    ClockApp *app = lv_event_get_user_data(e);
    alarm_service_timer_reset();
    clock_view_refresh_timer(app);
}

/* ── 时长选择 ────────────────────────────────────────────────────────── */

void clock_controller_on_timer_preset_apply(lv_event_t *e)
{
    ClockApp *app = lv_event_get_user_data(e);
    lv_obj_t *chip = lv_event_get_target(e);
    uint8_t idx = (uint8_t)(uintptr_t)lv_obj_get_user_data(chip);

    /* 一次点中就是最终时长，不用再按 Reset 确认 */
    alarm_service_timer_set((uint32_t)alarm_service_preset(idx) * 60u);

    clock_view_main_refresh_timer_chips(app);
    clock_view_refresh_timer(app);
}

void clock_controller_on_timer_preset_edit_open(lv_event_t *e)
{
    ClockApp *app = lv_event_get_user_data(e);
    app->model->active_tab = 1;   /* 返回时停在 Timer 页 */
    PAGE_NAVIGATE_TO(app, PAGE_TIMER_PRESET_EDIT, NULL);
}

/** 任意时长：输入分钟数直接用，不改快捷键。 */
void clock_controller_on_timer_custom_use(lv_event_t *e)
{
    ClockApp *app = lv_event_get_user_data(e);
    TimerPresetEditCtx *ctx = &app->view->timer_preset_ctx;
    if (!ctx->ta_custom) return;

    int mins = atoi(lv_textarea_get_text(ctx->ta_custom));
    if (mins <= 0 || mins > 999) {
        ESP_LOGW(TAG, "custom duration %d out of range, ignored", mins);
        return;
    }

    alarm_service_timer_set((uint32_t)mins * 60u);
    app->model->active_tab = 1;
    page_navigator_navigate_pop(&app->view->page_nav, app);

    clock_view_main_refresh_timer_chips(app);
    clock_view_refresh_timer(app);
}

void clock_controller_on_timer_preset_save(lv_event_t *e)
{
    ClockApp *app = lv_event_get_user_data(e);
    TimerPresetEditCtx *ctx = &app->view->timer_preset_ctx;

    uint16_t mins[ALARM_PRESET_COUNT] = {0};
    for (int i = 0; i < ALARM_PRESET_COUNT; i++) {
        if (ctx->ta_presets[i]) {
            mins[i] = (uint16_t)atoi(lv_textarea_get_text(ctx->ta_presets[i]));
        }
    }
    alarm_service_set_presets(mins);   /* 服务里会夹紧非法值 */

    app->model->active_tab = 1;
    page_navigator_navigate_pop(&app->view->page_nav, app);
    clock_view_main_refresh_timer_chips(app);
}

void clock_controller_on_tab_changed(lv_tab_t *tab, uint32_t idx, void *user_data)
{
    (void)tab;
    ClockApp *app = (ClockApp *)user_data;
    if (!app || !app->model || !app->view) return;

    app->model->active_tab = (uint8_t)idx;

    /* lv_tab 只是隐藏页面，不会发 LV_EVENT_DELETE。切到 Alarm 页时如果不
     * 主动收掉，面板刷新窗口还钉在倒计时数字上 —— 闹钟列表就只有那一小块
     * 会显示出来。 */
    if (idx == 1) {
        clock_view_refresh_timer(app);
        clock_controller_timer_ui_attach(app);
    } else {
        clock_controller_timer_ui_detach(app);
        clock_view_timer_leave(app);
    }
}
