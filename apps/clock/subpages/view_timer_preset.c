/*
 * view_timer_preset.c  --  时长设置页
 *
 * 两件事：
 *   1. 任意时长 —— 输入分钟数直接开始，不用先改快捷键；
 *   2. 改 4 个快捷时长 —— 改完保存，回到 Timer 页 chip 上就是新值。
 *
 * Timer 主页只留 chip，就是把"调时长"这件低频操作挪到这里一次输入完成，
 * 而不是在主页放 ±1 按钮让用户点二十下（每一下都是一次墨水屏刷新）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lvgl.h>
#include "esp_system.h"
#include "lv_ui_style_guide.h"

#include "lv_keyboard.h"
#include "lv_page.h"

#include "alarm_service.h"
#include "../controller.h"
#include "../model.h"
#include "../view.h"
#include "view_main.h"
#include "view_timer_preset.h"

/* 键盘的全部交互（弹出/收起、✓ 与键盘图标、光标置末尾、当前框加粗、
 * 让开被遮挡的输入框）都在 system/uilv/widgets/lv_keyboard.c 里，
 * 这一页只负责把滚动容器告诉它、把每个输入框 attach 上去。 */

static void page_deleted_cb(lv_event_t *e)
{
    (void)e;
    epos_keyboard_set_scroller(NULL);
}

/* ── 一个分钟数输入框 ───────────────────────────────────────── */

static lv_obj_t *make_minute_input(lv_obj_t *parent, lv_obj_t *kb, unsigned value)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_group_remove_obj(ta);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 3);          /* 最多 999 分钟 */
    lv_textarea_set_accepted_chars(ta, "0123456789");
    lv_obj_set_width(ta, 120);
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ta, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ta, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, lv_color_black(), LV_PART_MAIN);
    /* 边框宽度由 epos_keyboard_attach() 管：静止 1px，键盘绑上时 3px */

    /* 光标不闪：墨水屏上每闪一次就是一次刷新 */
    lv_obj_set_style_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(ta, 0, LV_PART_CURSOR | LV_STATE_FOCUSED);

    char buf[8];
    snprintf(buf, sizeof(buf), "%u", value);
    lv_textarea_set_text(ta, buf);

    epos_keyboard_attach(ta, kb);
    return ta;
}

/** 一行「说明 + 输入框」。 */
static lv_obj_t *make_row(lv_obj_t *parent, const char *text)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(row, 12, 0);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, LV_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    return row;
}

static lv_obj_t *build_timer_preset_page(ClockApp *app, void *user_data)
{
    (void)user_data;
    TimerPresetEditCtx *ctx = &app->view->timer_preset_ctx;

    Page page = lv_page_create("Timer Duration", true,
                               page_navigator_navigate_back,
                               &app->view->page_nav);

    lv_obj_clear_flag(page.screen, LV_OBJ_FLAG_SCROLLABLE);
    if (page.container) lv_obj_clear_flag(page.container, LV_OBJ_FLAG_SCROLLABLE);

    /* 内容比一屏高（键盘弹出时更明显），这一列要能滚；
     * screen 和 page.container 保持不可滚，避免嵌套滚动互相抢事件。 */
    lv_obj_t *col = lv_obj_create(page.container);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);
    lv_obj_set_style_pad_all(col, 16, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *kb = epos_keyboard_create_number(page.screen);

    /* col 比一屏高，键盘弹出时要让它滚起来，否则下半屏的 Shortcut 框被盖住 */
    epos_keyboard_set_scroller(col);
    lv_obj_add_event_cb(page.screen, page_deleted_cb, LV_EVENT_DELETE, NULL);

    /* ── 1. 任意时长，用完就走 ── */
    lv_obj_t *hdr1 = lv_label_create(col);
    lv_label_set_text(hdr1, "Custom duration (minutes)");
    lv_obj_set_style_text_font(hdr1, LV_FONT_TINY, 0);
    lv_obj_set_style_text_color(hdr1, lv_color_black(), 0);
    lv_obj_set_width(hdr1, LV_PCT(100));
    lv_obj_set_style_pad_bottom(hdr1, 8, 0);

    lv_obj_t *custom_row = make_row(col, "Minutes");
    ctx->ta_custom = make_minute_input(custom_row, kb,
                                       (unsigned)(alarm_service_timer_set_seconds() / 60u));

    lv_obj_t *use_btn = lv_btn_create(col);
    ui_style_set_btn_primary(use_btn);
    lv_obj_set_size(use_btn, LV_PCT(100), 56);
    lv_obj_add_event_cb(use_btn, clock_controller_on_timer_custom_use, LV_EVENT_CLICKED, app);
    lv_obj_t *use_lbl = lv_label_create(use_btn);
    lv_obj_set_style_text_font(use_lbl, LV_FONT_SMALL, 0);
    lv_label_set_text(use_lbl, "Use this duration");
    lv_obj_center(use_lbl);

    /* 分隔：1px 纯黑，1bpp 上任何中间灰都会被阈值判成白色 */
    lv_obj_t *hr = lv_obj_create(col);
    lv_obj_set_size(hr, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(hr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(hr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hr, 0, 0);
    lv_obj_set_style_margin_ver(hr, 20, 0);

    /* ── 2. 4 个快捷时长 ── */
    lv_obj_t *hdr2 = lv_label_create(col);
    lv_label_set_text(hdr2, "Shortcuts on the timer page");
    lv_obj_set_style_text_font(hdr2, LV_FONT_TINY, 0);
    lv_obj_set_style_text_color(hdr2, lv_color_black(), 0);
    lv_obj_set_width(hdr2, LV_PCT(100));
    lv_obj_set_style_pad_bottom(hdr2, 8, 0);

    for (int i = 0; i < ALARM_PRESET_COUNT; i++) {
        char lbl_buf[16];
        snprintf(lbl_buf, sizeof(lbl_buf), "Shortcut %d", i + 1);
        lv_obj_t *row = make_row(col, lbl_buf);
        ctx->ta_presets[i] = make_minute_input(row, kb, alarm_service_preset((uint8_t)i));
    }

    lv_obj_t *save_btn = lv_btn_create(col);
    ui_style_set_btn_secondary(save_btn);
    lv_obj_set_size(save_btn, LV_PCT(100), 56);
    lv_obj_set_style_margin_top(save_btn, 8, 0);
    lv_obj_add_event_cb(save_btn, clock_controller_on_timer_preset_save, LV_EVENT_CLICKED, app);
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_obj_set_style_text_font(save_lbl, LV_FONT_SMALL, 0);
    lv_label_set_text(save_lbl, "Save shortcuts");
    lv_obj_center(save_lbl);

    ctx->btn_save = save_btn;
    return page.screen;
}

void clock_view_timer_preset_init_registry(struct ClockApp *app)
{
    PAGE_REGISTE(app, PAGE_TIMER_PRESET_EDIT, build_timer_preset_page);
}
