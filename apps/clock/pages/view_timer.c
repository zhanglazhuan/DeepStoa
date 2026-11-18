// Timer tab: construction, display updates, and partial-refresh lifetime.
#include <stdio.h>
#include <string.h>
#include <lvgl.h>
#include "alarm_service.h"
#include "lv_ui_style_guide.h"
#include "../controller.h"
#include "../model.h"
#include "../view.h"
#include "lv_epd_region.h"
#include "view_timer.h"

/** 建立和刷新快捷时长时使用同一套文案。 */
void clock_view_timer_duration_text(uint16_t minutes, char *buf, size_t len)
{
    if (minutes >= 60 && minutes % 60 == 0) snprintf(buf, len, "%uh", (unsigned)(minutes / 60));
    else                                    snprintf(buf, len, "%um", (unsigned)minutes);
}

/* ── Timer tab ───────────────────────────────────────────────── */

/** Timer 页销毁：停掉刷新定时器并解除局部刷新窗口。
 *  计时本身在 system/services/alarm 里继续跑 —— 这里只是不再画了。 */
static void timer_tab_delete_cb(lv_event_t *e)
{
    ClockApp *app = lv_event_get_user_data(e);
    clock_controller_timer_ui_detach(app);
    clock_view_timer_leave(app);
}

static lv_obj_t *make_chip(lv_obj_t *parent, lv_coord_t w)
{
    lv_obj_t *chip = lv_btn_create(parent);
    ui_style_set_btn_secondary(chip);
    lv_obj_set_size(chip, w, 60);
    lv_obj_set_style_pad_all(chip, 4, LV_PART_MAIN);
    return chip;
}

/** 选中的 chip 反白。1bpp 上填充 / 描边是唯一可靠的选中表达 ——
 *  没有颜色可用，也没有阴影。 */
static void chip_set_active(lv_obj_t *chip, lv_obj_t *lbl, bool active)
{
    lv_obj_set_style_bg_color(chip, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chip, active ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    if (lbl) {
        lv_obj_set_style_text_color(lbl, active ? lv_color_white() : lv_color_black(), 0);
    }
}

void clock_view_timer_refresh_chips(ClockApp *app)
{
    TimerTabCtx *ctx = &app->view->timer_ctx;

    uint32_t set_s = alarm_service_timer_set_seconds();
    bool     idle  = (alarm_service_timer_state() == ALARM_TIMER_IDLE);

    for (int i = 0; i < ALARM_PRESET_COUNT; i++) {
        if (!ctx->chips[i] || !lv_obj_is_valid(ctx->chips[i])) continue;

        uint16_t mins = alarm_service_preset((uint8_t)i);
        char buf[16];
        clock_view_timer_duration_text(mins, buf, sizeof(buf));

        const char *cur = lv_label_get_text(ctx->chip_lbls[i]);
        if (!cur || strcmp(cur, buf) != 0) lv_label_set_text(ctx->chip_lbls[i], buf);

        chip_set_active(ctx->chips[i], ctx->chip_lbls[i],
                        idle && set_s == (uint32_t)mins * 60u);

        /* 计时中改时长没有意义，直接禁掉，省得用户点了没反应 */
        if (idle) lv_obj_remove_state(ctx->chips[i], LV_STATE_DISABLED);
        else      lv_obj_add_state(ctx->chips[i], LV_STATE_DISABLED);
    }

    if (ctx->chip_more && lv_obj_is_valid(ctx->chip_more)) {
        if (idle) lv_obj_remove_state(ctx->chip_more, LV_STATE_DISABLED);
        else      lv_obj_add_state(ctx->chip_more, LV_STATE_DISABLED);
    }
}

lv_obj_t *clock_view_timer_build(lv_obj_t *tab_content, ClockApp *app)
{
    TimerTabCtx *ctx = &app->view->timer_ctx;

    lv_obj_set_style_pad_hor(tab_content, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(tab_content, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(tab_content, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(tab_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab_content, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(tab_content, LV_SCROLLBAR_MODE_OFF);

    /* ── 主角区：大号数字 + 一行说明 ──
     * 单独包一层，局部刷新窗口就钉在这块上（见 clock_view_refresh_timer）。 */
    lv_obj_t *hero = lv_obj_create(tab_content);
    lv_obj_remove_style_all(hero);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_flex_grow(hero, 1);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hero, 8, 0);
    lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);
    ctx->hero_box = hero;

    ctx->hero_label = lv_label_create(hero);
    /* 固定整宽 + 居中：epd_region_begin_focus() 只在开始时钉一次窗口，
     * label 宽度要是随文本长短变（"25 min" -> "9 min"），窗口就框错了。 */
    lv_obj_set_width(ctx->hero_label, LV_PCT(100));
    lv_obj_set_style_text_align(ctx->hero_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ctx->hero_label, LV_FONT_LARGE, 0);
    lv_obj_set_style_text_color(ctx->hero_label, lv_color_black(), 0);
    lv_label_set_text(ctx->hero_label, "");

    ctx->hero_caption = lv_label_create(hero);
    lv_obj_set_width(ctx->hero_caption, LV_PCT(100));
    lv_obj_set_style_text_align(ctx->hero_caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ctx->hero_caption, LV_FONT_TINY, 0);
    lv_obj_set_style_text_color(ctx->hero_caption, lv_color_black(), 0);
    lv_label_set_text(ctx->hero_caption, "");

    /* 剩余占比。数字是分钟粒度（超过 1 分钟不逐秒变），光看数字很难感觉到
     * "还剩多少"，进度条补上这个直观量。IDLE 时没有意义，藏起来。 */
    ctx->hero_bar = lv_bar_create(hero);
    lv_obj_set_size(ctx->hero_bar, LV_PCT(80), 14);
    lv_bar_set_range(ctx->hero_bar, 0, 100);
    lv_bar_set_value(ctx->hero_bar, 100, LV_ANIM_OFF);
    lv_obj_set_style_margin_top(ctx->hero_bar, 4, 0);
    lv_obj_add_flag(ctx->hero_bar, LV_OBJ_FLAG_HIDDEN);

    ctx->last_text[0]    = '\0';
    ctx->last_caption[0] = '\0';
    ctx->last_bar_pct    = -1;
    ctx->region_on       = false;
    /* 故意置一个不可能的值，逼 refresh 走一次完整的状态切换分支 */
    ctx->last_state = (alarm_timer_state_t)0xFF;

    /* ── 时长选择 ── */
    lv_obj_t *chip_hdr = lv_label_create(tab_content);
    lv_label_set_text(chip_hdr, "Shortcut");
    lv_obj_set_style_text_font(chip_hdr, LV_FONT_TINY, 0);
    lv_obj_set_style_text_color(chip_hdr, lv_color_black(), 0);
    lv_obj_set_width(chip_hdr, LV_PCT(100));
    lv_obj_set_style_pad_bottom(chip_hdr, 6, 0);

    lv_obj_t *chip_row = lv_obj_create(tab_content);
    lv_obj_remove_style_all(chip_row);
    lv_obj_set_size(chip_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(chip_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(chip_row, 8, 0);

    for (int i = 0; i < ALARM_PRESET_COUNT; i++) {
        lv_obj_t *chip = make_chip(chip_row, 84);
        lv_obj_set_user_data(chip, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(chip, clock_controller_on_timer_preset_apply,
                            LV_EVENT_CLICKED, app);

        lv_obj_t *lbl = lv_label_create(chip);
        lv_obj_set_style_text_font(lbl, LV_FONT_SMALL, 0);
        lv_label_set_text(lbl, "");
        lv_obj_center(lbl);

        ctx->chips[i]     = chip;
        ctx->chip_lbls[i] = lbl;
    }

    /* 任意时长 / 改快捷键都走这个 chip —— 键盘一次输入完成，
     * 不用 ±1 点二十下，每一下还都是一次整屏刷新。 */
    ctx->chip_more = make_chip(chip_row, 64);
    lv_obj_add_event_cb(ctx->chip_more, clock_controller_on_timer_preset_edit_open,
                        LV_EVENT_CLICKED, app);
    lv_obj_t *more_lbl = lv_label_create(ctx->chip_more);
    lv_obj_set_style_text_font(more_lbl, LV_FONT_SMALL, 0);
    /* 铅笔而不是 "..."：这个 chip 是改快捷时长的值，不是"更多选项" */
    lv_label_set_text(more_lbl, LV_SYMBOL_EDIT);
    lv_obj_center(more_lbl);

    /* ── 底部动作 ──
     * 横向布局：IDLE 时 Reset 隐藏，Start 靠 flex_grow 独占整行（与 Add Alarm
     * 同宽同高同底部边距）；开始后 Reset 显示，Pause + Reset 一行两个按钮。 */
    lv_obj_t *action = lv_obj_create(tab_content);
    lv_obj_remove_style_all(action);
    lv_obj_set_size(action, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(action, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(action, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(action, 10, 0);
    lv_obj_set_style_pad_top(action, 16, 0);
    /* tab_content 有 pad_ver 12，12 + 20 = UI_BOTTOM_ACTION_GAP，与 Add Alarm 底部对齐 */
    lv_obj_set_style_margin_bottom(action, UI_BOTTOM_ACTION_GAP - 12, 0);

    /* Reset 在左、主按钮在右 —— 右侧更好够到，主按钮（Pause/Resume）是
     * 计时中唯一的高频操作；Reset 是低频且不可逆的，放远一点也更安全。
     * IDLE 时 Reset 隐藏，Start 靠 flex_grow 独占整行。 */
    ctx->btn_reset = lv_btn_create(action);
    ui_style_set_btn_secondary(ctx->btn_reset);
    lv_obj_set_style_border_width(ctx->btn_reset, 1, 0);  /* 主按钮 2px，次要的 1px */
    lv_obj_set_flex_grow(ctx->btn_reset, 1);
    lv_obj_set_height(ctx->btn_reset, UI_BOTTOM_ACTION_H);
    lv_obj_add_event_cb(ctx->btn_reset, clock_controller_on_timer_reset,
                        LV_EVENT_CLICKED, app);
    lv_obj_t *reset_lbl = lv_label_create(ctx->btn_reset);
    lv_obj_set_style_text_font(reset_lbl, LV_FONT_SMALL, 0);
    lv_label_set_text(reset_lbl, "Reset");
    lv_obj_center(reset_lbl);
    lv_obj_add_flag(ctx->btn_reset, LV_OBJ_FLAG_HIDDEN);

    /* 白底黑框，不用实心黑 —— 这里是唯一一个"底部有大块黑 + 上方每秒局刷"
     * 同时成立的页面。0x44/0x45 只约束 RAM 写入落在哪里，0x20 一发仍然把
     * 800 条 gate line 全扫一遍（见 Display_EPD_W21.c:862），所以底部按钮
     * 的像素虽然一秒都没被重写过，却每秒都被驱动一次，大块黑上肉眼可见地闪。
     * 实测：频率不变、只把黑面积去掉，闪烁即基本消失 —— 于是逐秒的 mm:ss
     * 手感和观感可以兼得。边框 2px，与 Reset 的 1px 拉开主次。 */
    ctx->btn_primary = lv_btn_create(action);
    ui_style_set_btn_secondary(ctx->btn_primary);
    lv_obj_set_style_bg_opa(ctx->btn_primary, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ctx->btn_primary, lv_color_white(), 0);
    lv_obj_set_flex_grow(ctx->btn_primary, 1);
    lv_obj_set_height(ctx->btn_primary, UI_BOTTOM_ACTION_H);
    lv_obj_add_event_cb(ctx->btn_primary, clock_controller_on_timer_start_pause,
                        LV_EVENT_CLICKED, app);
    ctx->btn_primary_label = lv_label_create(ctx->btn_primary);
    lv_obj_set_style_text_font(ctx->btn_primary_label, LV_FONT_SMALL, 0);
    lv_label_set_text(ctx->btn_primary_label, "Start");
    lv_obj_center(ctx->btn_primary_label);

    lv_obj_add_event_cb(tab_content, timer_tab_delete_cb, LV_EVENT_DELETE, app);

    return tab_content;
}

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
        clock_view_timer_refresh_chips(app);
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
