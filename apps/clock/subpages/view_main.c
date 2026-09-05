/*
 * view_main.c  --  Clock app main page (tabview)
 *
 * 数据全部来自 system/alarm —— 这一页只负责画和收事件。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lvgl.h>
#include "esp_log.h"
#include "esp_system.h"

#include "alarm_service.h"
#include "lv_page.h"
#include "lv_bottom_sheet.h"
#include "lv_tab.h"
#include "lv_ui_style_guide.h"
#include "ui_fonts.h"

#include "../controller.h"
#include "../model.h"
#include "../view.h"
#include "view_alarm_edit.h"
#include "view_main.h"

static const char __attribute__((unused)) *TAG = "clock_view_main";

/* ── 重复掩码 -> 人话 ─────────────────────────────────────────── */

static const char *repeat_str(uint8_t repeat)
{
    switch (repeat) {
    case ALARM_REPEAT_NONE:     return "Once";
    case ALARM_REPEAT_EVERY:    return "Every day";
    case ALARM_REPEAT_WEEKDAYS: return "Mon - Fri";
    case ALARM_REPEAT_WEEKEND:  return "Sat, Sun";
    default: {
        static char buf[32];
        static const char *abbr[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
        buf[0] = '\0';
        for (int i = 0; i < 7; i++) {
            if (!(repeat & (1u << i))) continue;
            if (buf[0]) strcat(buf, " ");
            strcat(buf, abbr[i]);
        }
        return buf;
    }
    }
}

/** 时长文案。建立和刷新走同一个函数 —— 原来一个用 "%d'"、一个用 "%dm"，
 *  编辑保存之后同一个按钮的格式会变。 */
void clock_view_main_duration_text(uint16_t minutes, char *buf, size_t len)
{
    if (minutes >= 60 && minutes % 60 == 0) snprintf(buf, len, "%uh", (unsigned)(minutes / 60));
    else                                    snprintf(buf, len, "%um", (unsigned)minutes);
}

/* ── 闹钟行 ──────────────────────────────────────────────────── */

static void alarm_row_gesture_cb(lv_event_t *e)
{
    ClockApp *app = lv_event_get_user_data(e);
    lv_obj_t *row = lv_event_get_current_target(e);
    uint8_t idx = (uint8_t)(uintptr_t)lv_obj_get_user_data(row);

    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) != LV_DIR_LEFT) return;

    app->model->edit_alarm_idx = (int8_t)idx;
    show_delete_confirm_dialog(e);

    /* 吃掉这次触摸，避免紧跟着的 LV_EVENT_CLICKED 误跳编辑页 */
    lv_indev_t *indev = lv_indev_get_act();
    if (indev) lv_indev_wait_release(indev);
}

/* 开关命中区向外扩的像素数，理由见 build_alarm_row() 里的注释 */
#define ALARM_SWITCH_EXT_CLICK  16

/* 停用态：1bpp 上没有"调暗"这回事，用删除线表达 —— 形状可辨，
 * 不像灰度那样会被阈值抹平。
 * 建行和拨开关两条路径都走这里，保证两边永远一致。 */
static void apply_enabled_decor(lv_obj_t *time_lbl, lv_obj_t *sub, bool enabled)
{
    lv_text_decor_t d = enabled ? LV_TEXT_DECOR_NONE : LV_TEXT_DECOR_STRIKETHROUGH;
    lv_obj_set_style_text_decor(time_lbl, d, 0);
    lv_obj_set_style_text_decor(sub, d, 0);
}

/* 从开关反查同一行的两个文字标签，就地更新删除线。
 *
 * 依赖 build_alarm_row() 定下的结构：
 *   row -> [0] left(列容器) -> [0] time_lbl, [1] sub
 *       -> [1] switch
 * 两者在同一个文件里，改布局时记得一起改。
 *
 * 之所以不直接重建整张列表：墨水屏上那是一次整屏刷新，而且会丢掉滚动位置；
 * 这里只动两个 label 的样式，和开关自身的重绘合并成同一帧。 */
void clock_view_main_set_row_enabled(lv_obj_t *sw, bool enabled)
{
    if (!sw) return;
    lv_obj_t *row = lv_obj_get_parent(sw);
    if (!row || lv_obj_get_child_count(row) < 1) return;

    lv_obj_t *left = lv_obj_get_child(row, 0);
    if (!left || lv_obj_get_child_count(left) < 2) return;

    apply_enabled_decor(lv_obj_get_child(left, 0), lv_obj_get_child(left, 1), enabled);
}

static void build_alarm_row(lv_obj_t *parent, ClockApp *app, uint8_t idx)
{
    const alarm_t *a = alarm_service_get(idx);
    if (!a) return;

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_style_pad_hor(row, 10, 0);
    lv_obj_set_style_pad_ver(row, 8, 0);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_shadow_width(row, 0, 0);
    /* 1px 纯黑分隔线。1bpp 面板上灰色会被阈值判成白色 —— 画了也看不见。 */
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_black(), 0);
    lv_obj_set_user_data(row, (void *)(uintptr_t)idx);
    lv_obj_add_event_cb(row, clock_controller_on_alarm_row_clicked, LV_EVENT_CLICKED, app);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(row, alarm_row_gesture_cb, LV_EVENT_GESTURE, app);

    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* ── 左侧：时间 + 重复/标签 ─────────────────────────────── */
    lv_obj_t *left = lv_obj_create(row);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(left, 2, 0);
    lv_obj_add_flag(left, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *time_lbl = lv_label_create(left);
    lv_label_set_text_fmt(time_lbl, "%02u:%02u", (unsigned)a->hour, (unsigned)a->minute);
    lv_obj_set_style_text_font(time_lbl, LV_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(time_lbl, lv_color_black(), 0);
    lv_obj_add_flag(time_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* 次要信息用小一号字体 —— 原来时间和重复说明都是 32px，扫一眼
     * 分不出主次；墨水屏上没有颜色和阴影，字号几乎是唯一的层级手段。 */
    lv_obj_t *sub = lv_label_create(left);
    if (a->label[0]) {
        lv_label_set_text_fmt(sub, "%s  -  %s", repeat_str(a->repeat), a->label);
    } else {
        lv_label_set_text(sub, repeat_str(a->repeat));
    }
    lv_obj_set_style_text_font(sub, LV_FONT_TINY, 0);
    lv_obj_set_style_text_color(sub, lv_color_black(), 0);
    lv_obj_add_flag(sub, LV_OBJ_FLAG_EVENT_BUBBLE);

    apply_enabled_decor(time_lbl, sub, a->enabled);

    /* ── 右侧：开关 ─────────────────────────────────────────── */
    lv_obj_t *sw = lv_switch_create(row);
    ui_style_set_switch(sw);   /* 关=白轨黑滑块，开=黑轨白滑块 */
    lv_obj_set_user_data(sw, (void *)(uintptr_t)idx);
    if (a->enabled) lv_obj_add_state(sw, LV_STATE_CHECKED);

    /* 开关本体只有 46x26（≈5.0 x 2.8mm），比手指小得多。差一点点按空就落到
     * row 上，而 row 的点击是"进入编辑页" —— 想关个闹钟结果跳进了编辑，
     * 这是最烦人的一类误触。
     * ext_click_area 只扩大命中区、不影响布局；子对象比父对象先参与命中测试，
     * 所以这圈范围内的点击会被开关吃掉，不会再穿透到 row。
     * 扩 16 之后有效热区 78x58 ≈ 8.4 x 6.3mm，够手指了，
     * 而 row 高约 68px，纵向也不会溢出到相邻行。 */
    lv_obj_set_ext_click_area(sw, ALARM_SWITCH_EXT_CLICK);

    lv_obj_add_event_cb(sw, clock_controller_on_alarm_toggle, LV_EVENT_VALUE_CHANGED, app);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_EVENT_BUBBLE);
}

/* ── Alarm tab ───────────────────────────────────────────────── */

static lv_obj_t *build_alarm_tab(lv_obj_t *tab_content, ClockApp *app)
{
    lv_obj_set_flex_flow(tab_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab_content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tab_content, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(tab_content, 8, LV_PART_MAIN);
    /* 与 Timer 页内容等宽（pad_hor 16），底部主按钮的 100% 宽度才能真正对齐 */
    lv_obj_set_style_pad_hor(tab_content, 16, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(tab_content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(tab_content, 0, LV_PART_MAIN);

    lv_obj_t *list_cont = lv_obj_create(tab_content);
    lv_obj_remove_style_all(list_cont);
    lv_obj_set_width(list_cont, LV_PCT(100));
    lv_obj_set_flex_grow(list_cont, 1);
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_cont, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list_cont, 8, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_AUTO);

    app->view->alarm_ctx.list = list_cont;

    clock_view_main_rebuild_alarm_list(app);

    lv_obj_t *add_btn = lv_btn_create(tab_content);
    ui_style_set_btn_primary(add_btn);
    lv_obj_set_size(add_btn, LV_PCT(100), UI_BOTTOM_ACTION_H);
    /* tab_content 有 pad_ver 8，8 + 24 = UI_BOTTOM_ACTION_GAP */
    lv_obj_set_style_margin_bottom(add_btn, UI_BOTTOM_ACTION_GAP - 8, LV_PART_MAIN);
    lv_obj_add_event_cb(add_btn, clock_controller_on_add_alarm, LV_EVENT_CLICKED, app);

    lv_obj_t *add_lbl = lv_label_create(add_btn);
    lv_obj_set_style_text_font(add_lbl, LV_FONT_SMALL, 0); /* 与 Start 按钮文案字体一致 */
    lv_label_set_text(add_lbl, LV_SYMBOL_PLUS "  Add Alarm");
    lv_obj_center(add_lbl);

    app->view->alarm_ctx.add_btn = add_btn;
    return tab_content;
}

/* ── Timer tab ───────────────────────────────────────────────── */

/** Timer 页销毁：停掉刷新定时器并解除局部刷新窗口。
 *  计时本身在 system/alarm 里继续跑 —— 这里只是不再画了。 */
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

void clock_view_main_refresh_timer_chips(ClockApp *app)
{
    TimerTabCtx *ctx = &app->view->timer_ctx;

    uint32_t set_s = alarm_service_timer_set_seconds();
    bool     idle  = (alarm_service_timer_state() == ALARM_TIMER_IDLE);

    for (int i = 0; i < ALARM_PRESET_COUNT; i++) {
        if (!ctx->chips[i] || !lv_obj_is_valid(ctx->chips[i])) continue;

        uint16_t mins = alarm_service_preset((uint8_t)i);
        char buf[16];
        clock_view_main_duration_text(mins, buf, sizeof(buf));

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

static lv_obj_t *build_timer_tab(lv_obj_t *tab_content, ClockApp *app)
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
     * 单独包一层，局部刷新窗口就钉在这块上（见 view.c）。 */
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

/* ── 重建闹钟列表 ────────────────────────────────────────────── */

void clock_view_main_rebuild_alarm_list(ClockApp *app)
{
    lv_obj_t *list = app->view->alarm_ctx.list;
    if (!list || !lv_obj_is_valid(list)) return;

    lv_obj_clean(list);

    uint8_t count = alarm_service_count();

    lv_obj_t *empty = lv_label_create(list);
    lv_label_set_text(empty, "No Alarms");
    lv_obj_set_style_margin_top(empty, 20, LV_PART_MAIN);
    lv_obj_set_style_text_font(empty, LV_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(empty, lv_color_black(), 0);
    if (count > 0) lv_obj_add_flag(empty, LV_OBJ_FLAG_HIDDEN);
    app->view->alarm_ctx.empty_label = empty;

    for (uint8_t i = 0; i < count; i++) {
        build_alarm_row(list, app, i);
    }
}

/* ── 页面 builder ────────────────────────────────────────────── */

static lv_obj_t *build_main_page(ClockApp *app, void *user_data)
{
    (void)user_data;

    Page page = lv_page_create("Clock", false, page_navigator_navigate_back,
                               &app->view->page_nav);

    static const char *tab_names[] = {"Alarm", "Timer"};
    lv_tab_t *tab = lv_tab_create(page.container, tab_names, 2, 40);
    lv_obj_set_style_radius(lv_tab_get_tab_bar(tab), 0, 0);  /* 直角 tabbar，不要倒角 */
    lv_obj_t *tab_root = lv_tab_get_root(tab);
    lv_obj_set_flex_grow(tab_root, 1);
    lv_tab_set_changed_cb(tab, clock_controller_on_tab_changed, app);

    lv_obj_t *alarm_tab = lv_tab_add_page(tab);
    build_alarm_tab(alarm_tab, app);

    lv_obj_t *timer_tab = lv_tab_add_page(tab);
    build_timer_tab(timer_tab, app);

    lv_tab_finalize(tab);
    lv_tab_set_active(tab, app->model->active_tab);

    /* 计时器可能在 app 关闭期间一直在跑 —— 进来先把当前状态画对，
     * 再挂上刷新定时器。 */
    clock_view_refresh_timer(app);
    clock_controller_timer_ui_attach(app);

    return page.screen;
}

void clock_view_main_init_registry(struct ClockApp *app)
{
    PAGE_REGISTE(app, PAGE_CLOCK_MAIN, build_main_page);
}

/* ── 上限提示（lv_bottom_sheet）──────────────────────────────
 */

/* sheet 里的动作按钮：primary = 实心（危险/主动作），否则描边。
 * 并排放在按钮行里，尺寸见 UI_SHEET_BTN_*。 */
/* 弹层的动作按钮行：横排，Cancel 在左、主按钮在右。
 * SPACE_EVENLY 让两枚朝中间收，两侧留出边距，不贴屏幕边。
 * 尺寸/约定见 lv_ui_style_guide.h 的 UI_SHEET_BTN_*。 */
static lv_obj_t *warn_btn_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

static lv_obj_t *warn_action_btn(lv_obj_t *parent, const char *text, bool primary,
                        lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    if (primary) ui_style_set_btn_primary(btn);
    else         ui_style_set_btn_secondary(btn);
    lv_obj_set_size(btn, UI_SHEET_BTN_W, UI_SHEET_BTN_H);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, LV_FONT_SMALL, 0);
    lv_obj_center(lbl);
    return btn;
}

/* 建弹层壳子：标题 + 正文，返回内容区供调用方接着加按钮。
 * 刻意不调 lv_bottom_sheet_add_header() —— 它自带关闭 ×，会变成第三个出口。
 * 点遮罩关闭是 widget 自带的无害默认退出，不算一个"选择"。 */
static lv_obj_t *warn_sheet_body(lv_bottom_sheet_t *bs, const char *title, const char *message)
{
    lv_obj_t *c = lv_bottom_sheet_get_content(bs);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c, 12, 0);

    lv_obj_t *t = lv_label_create(c);
    lv_obj_set_width(t, LV_PCT(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(t, LV_FONT_NORMAL, 0);
    lv_label_set_text(t, title);

    lv_obj_t *m = lv_label_create(c);
    lv_obj_set_width(m, LV_PCT(100));
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(m, LV_FONT_SMALL, 0);
    lv_obj_set_style_pad_bottom(m, 8, 0);
    lv_label_set_text(m, message);
    return c;
}

static void warn_ok_cb(lv_event_t *e)
{
    lv_bottom_sheet_close((lv_bottom_sheet_t *)lv_event_get_user_data(e));
}

void clock_view_main_show_max_alarm_warning(void)
{
    lv_bottom_sheet_t *bs = lv_bottom_sheet_create(lv_layer_top());
    if (!bs) return;

    lv_obj_t *c = warn_sheet_body(bs, "Limit Reached",
        "Maximum number of alarms reached. Delete one before adding a new alarm.");
    /* 只有一个动作，仍然靠右 —— 主按钮位置和其它弹层保持一致 */
    lv_obj_t *row = warn_btn_row(c);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    warn_action_btn(row, "OK", true, warn_ok_cb, bs);
}
