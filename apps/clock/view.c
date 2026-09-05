// apps/clock/view.c
// Clock view —— page navigator 初始化 + 刷新辅助
//
// 墨水屏刷新预算（见 apps/player/subpages/view_play.c 的同类做法）：
//   1. 文本没变就不调 lv_label_set_text() —— 它不比较旧值，每次都标脏，
//      而默认 flush 模式是 PARTIAL_ALL，一次标脏 = 整块 480×800 重推。
//   2. 计时中逐秒显示 hh:mm:ss / mm:ss，所以每秒都要推一次 —— 这只有在
//      刷新窗口钉死在 hero 区（数字 + 状态 + 进度条）之后才付得起：
//      推的是那一小块，不是整块 480×800。窗口没钉住时（IDLE / PAUSED）
//      本来就不逐秒变，也不会有代价。

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lvgl.h>
#include "esp_log.h"
#include "esp_system.h"

#include "alarm_service.h"
#include "controller.h"
#include "lv_epd_region.h"
#include "model.h"
#include "subpages/view_alarm_edit.h"
#include "subpages/view_main.h"
#include "subpages/view_timer_preset.h"
#include "view.h"

static const char *TAG = "clock_view";

void clock_view_init(ClockApp *app)
{
    app->view = malloc(sizeof(ClockView));
    if (!app->view) {
        ESP_LOGE(TAG, "Failed to alloc ClockView");
        return;
    }
    memset(app->view, 0, sizeof(ClockView));

    page_navigator_page_t *registry = malloc(sizeof(page_navigator_page_t) * CLOCK_PAGE_ID_MAX);
    memset(registry, 0, sizeof(page_navigator_page_t) * CLOCK_PAGE_ID_MAX);
    page_navigator_init(&app->view->page_nav, registry, CLOCK_PAGE_ID_MAX, app);

    clock_view_main_init_registry(app);
    clock_view_alarm_edit_init_registry(app);
    clock_view_timer_preset_init_registry(app);
}

void clock_view_deinit(ClockApp *app)
{
    if (app->view) {
        page_navigator_deinit(&app->view->page_nav);
        free(app->view->page_nav.registry);
        free(app->view);
        app->view = NULL;
    }
}

/* ── Alarm list refresh ──────────────────────────────────────── */

void clock_view_refresh_alarm_list(ClockApp *app)
{
    clock_view_main_rebuild_alarm_list(app);
}

/* ── Timer display refresh ───────────────────────────────────── */

/**
 * 剩余时间的显示文本：hh:mm:ss，不足 1 小时省掉小时段显示成 mm:ss。
 *
 * 逐秒变化意味着计时中每秒都要推一次屏 —— 前提是刷新窗口已经钉在 hero 区
 * （见 clock_view_refresh_timer 末尾的 epd_region_begin_focus），推的是
 * 那一小块而不是整屏。窗口没钉住的时候（IDLE / PAUSED）数字本来就不动。
 */
static void format_remaining(uint32_t seconds, char *buf, size_t len)
{
    unsigned h = (unsigned)(seconds / 3600u);
    unsigned m = (unsigned)((seconds % 3600u) / 60u);
    unsigned sec = (unsigned)(seconds % 60u);

    if (h > 0) snprintf(buf, len, "%u:%02u:%02u", h, m, sec);
    else       snprintf(buf, len, "%02u:%02u", m, sec);
}

/** 主角区文案：大号数字 + 下面那行说明。 */
static void hero_text_for(alarm_timer_state_t st, char *text, size_t tlen,
                          const char **caption)
{
    switch (st) {
    case ALARM_TIMER_IDLE: {
        uint32_t set_s = alarm_service_timer_set_seconds();
        if (set_s == 0) snprintf(text, tlen, "00:00");
        else            format_remaining(set_s, text, tlen);
        *caption = "Ready";
        break;
    }
    case ALARM_TIMER_RUNNING:
        format_remaining(alarm_service_timer_remaining_s(), text, tlen);
        *caption = "Running";
        break;
    case ALARM_TIMER_PAUSED:
        format_remaining(alarm_service_timer_remaining_s(), text, tlen);
        *caption = "Paused";
        break;
    case ALARM_TIMER_FINISHED:
    default:
        snprintf(text, tlen, "00:00");
        *caption = "Time is up";
        break;
    }
}

/** 剩余占比 0..100。拿不到总时长就保持上一次的值（返回 keep）。 */
static int bar_pct_for(alarm_timer_state_t st, int keep)
{
    if (st == ALARM_TIMER_FINISHED) return 0;
    if (st == ALARM_TIMER_IDLE)     return 100;

    uint32_t total = alarm_service_timer_set_seconds();
    if (total == 0) return keep < 0 ? 100 : keep;

    uint32_t left = alarm_service_timer_remaining_s();
    if (left > total) left = total;
    return (int)((left * 100u + total / 2u) / total);
}

static const char *primary_label_for(alarm_timer_state_t st)
{
    switch (st) {
    case ALARM_TIMER_RUNNING:  return "Pause";
    case ALARM_TIMER_PAUSED:   return "Resume";
    case ALARM_TIMER_FINISHED: return "Done";
    case ALARM_TIMER_IDLE:
    default:                   return "Start";
    }
}

void clock_view_refresh_timer(ClockApp *app)
{
    TimerTabCtx *ctx = &app->view->timer_ctx;
    if (!ctx->hero_label || !lv_obj_is_valid(ctx->hero_label)) return;

    alarm_timer_state_t st = alarm_service_timer_state();

    /* 状态切换会改变布局（按钮文案、Reset 显隐、chip 禁用），这时必须
     * 整屏刷一次，局部刷新的差分基准也要跟着重建。 */
    if (st != ctx->last_state) {
        ctx->last_state   = st;
        ctx->last_text[0] = '\0';
        if (ctx->region_on) {
            epd_region_end();
            ctx->region_on = false;
        }

        if (ctx->btn_primary_label && lv_obj_is_valid(ctx->btn_primary_label)) {
            lv_label_set_text(ctx->btn_primary_label, primary_label_for(st));
        }
        ctx->last_bar_pct = -1;   /* 逼下面重推一次进度条 */
        if (ctx->hero_bar && lv_obj_is_valid(ctx->hero_bar)) {
            /* IDLE 还没开始，没有"剩余占比"可言 */
            if (st == ALARM_TIMER_IDLE) lv_obj_add_flag(ctx->hero_bar, LV_OBJ_FLAG_HIDDEN);
            else                        lv_obj_remove_flag(ctx->hero_bar, LV_OBJ_FLAG_HIDDEN);
        }
        if (ctx->btn_reset && lv_obj_is_valid(ctx->btn_reset)) {
            /* Reset 只在开始之后才有意义 */
            if (st == ALARM_TIMER_IDLE) lv_obj_add_flag(ctx->btn_reset, LV_OBJ_FLAG_HIDDEN);
            else                        lv_obj_remove_flag(ctx->btn_reset, LV_OBJ_FLAG_HIDDEN);
        }
        clock_view_main_refresh_timer_chips(app);
    }

    char text[16];
    const char *caption = "";
    hero_text_for(st, text, sizeof(text), &caption);

    bool caption_changed = strcmp(caption, ctx->last_caption) != 0;
    bool text_changed    = strcmp(text, ctx->last_text) != 0;

    /* 进度条：剩余 / 总时长。数字是分钟粒度，条子要是也逐秒推就把省下来的
     * 刷新又花回去了 —— 所以只在跨过 5% 时推一次（整场 20 次，和分钟数字
     * 一个量级）；反正文本要变的时候顺带推一次，那次是白送的。 */
    int  bar_pct     = bar_pct_for(st, ctx->last_bar_pct);
    bool bar_changed = (ctx->last_bar_pct < 0) ||
                       (bar_pct != ctx->last_bar_pct &&
                        (text_changed || caption_changed ||
                         bar_pct - ctx->last_bar_pct >= 5 ||
                         ctx->last_bar_pct - bar_pct >= 5));

    if (!text_changed && !caption_changed && !bar_changed) return;   /* 值没变，不上屏 */

    if (text_changed) {
        snprintf(ctx->last_text, sizeof(ctx->last_text), "%s", text);
        lv_label_set_text(ctx->hero_label, text);
    }
    if (caption_changed && ctx->hero_caption && lv_obj_is_valid(ctx->hero_caption)) {
        snprintf(ctx->last_caption, sizeof(ctx->last_caption), "%s", caption);
        lv_label_set_text(ctx->hero_caption, caption);
    }
    if (bar_changed && ctx->hero_bar && lv_obj_is_valid(ctx->hero_bar)) {
        ctx->last_bar_pct = bar_pct;
        lv_bar_set_value(ctx->hero_bar, bar_pct, LV_ANIM_OFF);
    }

    /* 计时中第一次变化时打一帧基准并把刷新窗口钉在数字上，
     * 之后每次只推这一块。 */
    if (!ctx->region_on && st == ALARM_TIMER_RUNNING) {
        /* 钉整个 hero 容器，不是单个 label —— 进度条也在里面，
         * 只框住数字的话条子的像素落在窗口外，永远推不上屏。 */
        epd_region_begin_focus(lv_screen_active(),
                               ctx->hero_box ? ctx->hero_box : ctx->hero_label);
        ctx->region_on = true;
    }
}

void clock_view_timer_leave(ClockApp *app)
{
    TimerTabCtx *ctx = &app->view->timer_ctx;
    if (ctx->region_on) {
        epd_region_end();
        ctx->region_on = false;
    }
    ctx->last_text[0]    = '\0';
    ctx->last_caption[0] = '\0';
    /* 切回来时强制走一次完整的状态分支，重建基准帧 */
    ctx->last_state = (alarm_timer_state_t)0xFF;
}
