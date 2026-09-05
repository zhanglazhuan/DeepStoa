/*
 * view_alarm_edit.c  --  新建 / 编辑一条闹钟
 *
 * 数据在 system/alarm 里，这一页只读它、改它。时间用数字输入框 + 加减按钮
 * （不是 roller —— 墨水屏上滚动列表既看不清也刷不动），重复用 7 个复选框，
 * 删除走二次确认弹窗。
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_system.h"
#include "esp_log.h"
#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "lv_bottom_sheet.h"
#include "lv_keyboard.h"

#include "view_alarm_edit.h"
#include "../view.h"
#include "../model.h"
#include "../controller.h"
#include "alarm_service.h"

static const char *TAG = "clock_view_alarm_edit";

/* 时/分的上限。超出就压到上限，不是清零 —— 用户敲 99 的意图明显是
 * "要尽量大"，弹回 00 更让人意外。加减按钮那边用的是 24/60 取模，
 * 两处含义不同（一个是钳位、一个是回绕），别互相套用。 */
#define ALARM_HOUR_MAX  23
#define ALARM_MIN_MAX   59

/* ── Day abbreviations ───────────────────────────────────────── */
static const char *s_day_abbr[7] = {
    "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
};
static const uint8_t s_day_bits[7] = {
    ALARM_REPEAT_MON, ALARM_REPEAT_TUE,
    ALARM_REPEAT_WED, ALARM_REPEAT_THU, ALARM_REPEAT_FRI,
    ALARM_REPEAT_SAT, ALARM_REPEAT_SUN,
};

/* ── Helper ──────────────────────────────────────────────────── */
static lv_obj_t *make_divider(lv_obj_t *parent)
{
    lv_obj_t *hr = lv_obj_create(parent);
    lv_obj_set_size(hr, LV_PCT(100), 1);
    /* 必须是纯黑：LV_COLOR_FORMAT_I1 下任何中间灰都会被阈值判成白色 */
    lv_obj_set_style_bg_color(hr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(hr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hr, 0, 0);
    return hr;
}

/* ── Local Callbacks for UI Adjustments ── */
static void hour_adjust_cb(lv_event_t * e) {
    lv_obj_t * btn = lv_event_get_target(e);
    lv_obj_t * ta = lv_event_get_user_data(e);

    /* Cleverly use button label text (e.g., "+5", "-1") to convert directly to integer */
    int delta = atoi(lv_label_get_text(lv_obj_get_child(btn, 0)));
    int val = atoi(lv_textarea_get_text(ta));

    /* Add 24 modulo, perfectly handles negative wraparound (e.g., 2 - 5 = 21) */
    val = (val + delta + 24) % 24;
    char buf[4];
    snprintf(buf, sizeof(buf), "%02d", val);
    lv_textarea_set_text(ta, buf);
}

static void min_adjust_cb(lv_event_t * e) {
    lv_obj_t * btn = lv_event_get_target(e);
    lv_obj_t * ta = lv_event_get_user_data(e);

    int delta = atoi(lv_label_get_text(lv_obj_get_child(btn, 0)));
    int val = atoi(lv_textarea_get_text(ta));

    val = (val + delta + 60) % 60;
    char buf[4];
    snprintf(buf, sizeof(buf), "%02d", val);
    lv_textarea_set_text(ta, buf);
}

/* 失焦 / ✓ / 键盘图标时校验并补零。user_data 是这个框的上限值。
 *
 * 只在提交时钳位，不在 VALUE_CHANGED 里边打边改 —— 那样敲 "2" 想接 "3"
 * 的时候会被中途改掉，而且在 VALUE_CHANGED 里调 lv_textarea_set_text()
 * 会再发一次 VALUE_CHANGED，要额外加重入保护。
 * 点 Save 时 DEFOCUSED 先于按钮的 CLICKED 触发（前者在按下时发、后者在
 * 抬起时发），所以直接点保存也走得到这里。 */
static void ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_DEFOCUSED && code != LV_EVENT_READY
        && code != LV_EVENT_CANCEL) return;

    lv_obj_t *ta  = lv_event_get_target(e);
    int       max = (int)(intptr_t)lv_event_get_user_data(e);

    int val = atoi(lv_textarea_get_text(ta));   /* 空串 atoi 得 0，正好补成 00 */
    if (val < 0)   val = 0;
    if (val > max) val = max;

    char buf[4];
    snprintf(buf, sizeof(buf), "%02d", val);
    lv_textarea_set_text(ta, buf);
}

/* ── 删除确认（lv_bottom_sheet）───────────────────────────── */

/* sheet 里的动作按钮：primary = 实心（危险/主动作），否则描边。
 * 并排放在按钮行里，尺寸见 UI_SHEET_BTN_*。 */
/* 弹层的动作按钮行：横排，Cancel 在左、主按钮在右。
 * SPACE_EVENLY 让两枚朝中间收，两侧留出边距，不贴屏幕边。
 * 尺寸/约定见 lv_ui_style_guide.h 的 UI_SHEET_BTN_*。 */
static lv_obj_t *del_btn_row(lv_obj_t *parent)
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

static lv_obj_t *del_action_btn(lv_obj_t *parent, const char *text, bool primary,
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
 * 刻意不调 lv_bottom_sheet_add_header() —— 它自带关闭 ×，会变成第三个出口，
 * 用户还得先分辨"哪个是取消"。点遮罩关闭是 widget 自带的无害默认退出。 */
static lv_obj_t *del_sheet_body(lv_bottom_sheet_t *bs, const char *title, const char *message)
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

static void delete_cancel_cb(lv_event_t *e)
{
    lv_bottom_sheet_close((lv_bottom_sheet_t *)lv_event_get_user_data(e));
}

static void delete_alarm_confirm_cb(lv_event_t *e)
{
    ClockApp *app = lv_event_get_user_data(e);
    ClockModel *m = app->model;
    /* sheet 存在按钮自己的 user_data 里 —— 事件的 user_data 让给了 app，
     * 这是本文件原来就在用的两条通道的写法。 */
    lv_bottom_sheet_t *bs = lv_obj_get_user_data(lv_event_get_current_target(e));

    /* 先关弹层：下面可能会 pop 页面，弹层在 lv_layer_top 上不随 screen 走 */
    lv_bottom_sheet_close(bs);

    if (m->edit_alarm_idx >= 0) {
        alarm_service_remove((uint8_t)m->edit_alarm_idx);
        m->edit_alarm_idx = -1;
    }
    if (app->view->page_nav.current_page == PAGE_ALARM_EDIT) {
        page_navigator_navigate_pop(&app->view->page_nav, app);
    }
    clock_view_refresh_alarm_list(app);
}

void show_delete_confirm_dialog(lv_event_t *e)
{
    ClockApp *app = lv_event_get_user_data(e);

    lv_bottom_sheet_t *bs = lv_bottom_sheet_create(lv_layer_top());
    if (!bs) return;

    lv_obj_t *c = del_sheet_body(bs, "Confirm Delete",
                                 "Are you sure you want to delete this alarm?");
    /* 横排：Cancel 左、主按钮右 */
    lv_obj_t *row = del_btn_row(c);
    del_action_btn(row, "Cancel", false, delete_cancel_cb, bs);
    lv_obj_t *btn_del = del_action_btn(row, "Delete", true, delete_alarm_confirm_cb, app);
    lv_obj_set_user_data(btn_del, bs);
}

/* ── Page builder ────────────────────────────────────────────── */
static lv_obj_t *build_alarm_edit_page(ClockApp *app, void *user_data)
{
    (void)user_data;

    bool is_new = (app->model->edit_alarm_idx < 0);
    const alarm_t *existing = is_new
                            ? NULL
                            : alarm_service_get((uint8_t)app->model->edit_alarm_idx);
    if (!is_new && !existing) is_new = true;   /* 列表变了，下标已失效 */

    Page page = lv_page_create(is_new ? "Add Alarm" : "Edit Alarm",
                               true,
                               page_navigator_navigate_back,
                               &app->view->page_nav);

    /* Create a number keyboard, shared by all textareas */
    lv_obj_t *kb = epos_keyboard_create_number(page.screen);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    /*
    * Content column -- NOT scrollable.
    * The page without Label fits in one screen, and making it non-scrollable
    * is essential so the roller's vertical swipe is not stolen by a parent
    * scroll container.
    */
    lv_obj_t *col = lv_obj_create(page.container);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_ver(col, 12, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE); /* critical for rollers */
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 16, LV_PART_MAIN);

    /* ==============================================================
     * 1. TIME PICKERS  HH  :  MM (Button & Keyboard Mode)
     * ============================================================== */
    lv_obj_t *time_hdr = lv_label_create(col);
    lv_obj_set_width(time_hdr, LV_PCT(100));
    lv_label_set_text(time_hdr, "Time");

    lv_obj_t *picker_row = lv_obj_create(col);
    lv_obj_remove_style_all(picker_row);
    lv_obj_set_size(picker_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(picker_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(picker_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(picker_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Updated macro: for vertical layout, set button width to match textarea (e.g. 100), height 44 */
#define MAKE_ADJ_BTN(parent_obj, txt, ta_obj, cb) do { \
        lv_obj_t *b = lv_btn_create(parent_obj); \
        ui_style_set_btn_secondary(b); \
        lv_obj_set_size(b, 100, 44); /* Wider for pure vertical layout */ \
        lv_obj_t *l = lv_label_create(b); \
        lv_label_set_text(l, txt); \
        lv_obj_center(l); \
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ta_obj); \
    } while(0)

    /* ==============================================================
     * --- HH Column ---
     * ============================================================== */
    lv_obj_t *hh_col = lv_obj_create(picker_row);
    lv_obj_remove_style_all(hh_col);
    lv_obj_set_size(hh_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hh_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hh_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hh_col, 8, 0); /* Vertical spacing between elements */

    /* 1. Create textarea first so we have the pointer */
    lv_obj_t *ta_hour = lv_textarea_create(hh_col);
    lv_textarea_set_one_line(ta_hour, true);
    lv_textarea_set_max_length(ta_hour, 2);
    lv_textarea_set_accepted_chars(ta_hour, "0123456789");
    lv_obj_set_width(ta_hour, 100); /* Match button width */
    lv_obj_set_style_text_align(ta_hour, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_opa(ta_hour, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(ta_hour, 0, LV_PART_CURSOR | LV_STATE_FOCUSED);
    char buf_h[4];
    snprintf(buf_h, sizeof(buf_h), "%02d", existing ? existing->hour : 7);
    lv_textarea_set_text(ta_hour, buf_h);
    lv_obj_set_style_text_font(ta_hour, LV_FONT_LARGE, 0);
    lv_obj_add_event_cb(ta_hour, ta_event_cb, LV_EVENT_ALL,
                        (void *)(intptr_t)ALARM_HOUR_MAX);
    /* ta_event_cb 已经在上面注册过了 —— 必须排在 attach 前面：
     * helper 处理 ✓ / 键盘图标时会 lv_event_stop_processing()，
     * 排在它后面的回调收不到 READY/CANCEL，补零就不会执行。 */
    epos_keyboard_attach(ta_hour, kb);
    app->view->edit_ctx.ta_hour = ta_hour;

    /* 2. Create plus buttons above */
    MAKE_ADJ_BTN(hh_col, "+5", ta_hour, hour_adjust_cb);
    MAKE_ADJ_BTN(hh_col, "+1", ta_hour, hour_adjust_cb);

    /* 3. Magic: move textarea to between +5/+1 and -1/-5 (index 2) */
    lv_obj_move_to_index(ta_hour, 2);

    /* 4. Create minus buttons below */
    MAKE_ADJ_BTN(hh_col, "-1", ta_hour, hour_adjust_cb);
    MAKE_ADJ_BTN(hh_col, "-5", ta_hour, hour_adjust_cb);

    /* ==============================================================
     * --- Colon (:) ---
     * ============================================================== */
    lv_obj_t *colon = lv_label_create(picker_row);
    lv_obj_set_style_text_font(colon, LV_FONT_LARGE, 0);
    lv_obj_set_style_text_color(colon, lv_color_black(), 0);
    lv_label_set_text(colon, ":");

    /* ==============================================================
     * --- MM Column ---
     * ============================================================== */
    lv_obj_t *mm_col = lv_obj_create(picker_row);
    lv_obj_remove_style_all(mm_col);
    lv_obj_set_size(mm_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(mm_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mm_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(mm_col, 8, 0);

    /* 1. Create textarea first so we have the pointer */
    lv_obj_t *ta_min = lv_textarea_create(mm_col);
    lv_textarea_set_one_line(ta_min, true);
    lv_textarea_set_max_length(ta_min, 2);
    lv_textarea_set_accepted_chars(ta_min, "0123456789");
    lv_obj_set_width(ta_min, 100);
    lv_obj_set_style_text_font(ta_min, LV_FONT_LARGE, 0);
    lv_obj_set_style_text_align(ta_min, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_opa(ta_min, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(ta_min, 0, LV_PART_CURSOR | LV_STATE_FOCUSED);
    char buf_m[4];
    snprintf(buf_m, sizeof(buf_m), "%02d", existing ? existing->minute : 0);
    lv_textarea_set_text(ta_min, buf_m);
    lv_obj_add_event_cb(ta_min, ta_event_cb, LV_EVENT_ALL,
                        (void *)(intptr_t)ALARM_MIN_MAX);
    /* ta_event_cb 已经在上面注册过了 —— 必须排在 attach 前面：
     * helper 处理 ✓ / 键盘图标时会 lv_event_stop_processing()，
     * 排在它后面的回调收不到 READY/CANCEL，补零就不会执行。 */
    epos_keyboard_attach(ta_min, kb);
    app->view->edit_ctx.ta_min = ta_min;

    /* 2. Create plus buttons above */
    MAKE_ADJ_BTN(mm_col, "+10", ta_min, min_adjust_cb);
    MAKE_ADJ_BTN(mm_col, "+1", ta_min, min_adjust_cb);

    /* 3. Move textarea to middle */
    lv_obj_move_to_index(ta_min, 2);

    /* 4. Create minus buttons below */
    MAKE_ADJ_BTN(mm_col, "-1", ta_min, min_adjust_cb);
    MAKE_ADJ_BTN(mm_col, "-10", ta_min, min_adjust_cb);

#undef MAKE_ADJ_BTN

    /* ==============================================================
     * 2. REPEAT (Grid layout: perfect matrix alignment, 4 top, 3 bottom)
     * ============================================================== */
    make_divider(col);

    lv_obj_t *rep_hdr = lv_label_create(col);
    lv_obj_set_width(rep_hdr, LV_PCT(100));
    lv_label_set_text(rep_hdr, "Repeat");

    /* Define grid rules: 4 columns (equal width) x 2 rows (auto height) */
    static const int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

    /* Create grid main container */
    lv_obj_t *rep_grid = lv_obj_create(col);
    lv_obj_remove_style_all(rep_grid);
    lv_obj_set_size(rep_grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(rep_grid, LV_OBJ_FLAG_SCROLLABLE);

    /* Enable Grid layout and apply rules */
    lv_obj_set_layout(rep_grid, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(rep_grid, col_dsc, row_dsc);
    lv_obj_set_style_pad_row(rep_grid, 12, 0);
    lv_obj_set_style_pad_column(rep_grid, 4, 0);

    uint8_t existing_repeat = existing ? existing->repeat : ALARM_REPEAT_NONE;

    /* Loop 7 days, drop into grid by math */
    for (int i = 0; i < 7; i++) {
        lv_obj_t *cb = lv_checkbox_create(rep_grid);
        lv_checkbox_set_text(cb, s_day_abbr[i]);

        lv_obj_set_style_text_font(cb, LV_FONT_SMALL, LV_PART_MAIN);
        lv_obj_set_style_text_color(cb, lv_color_black(), LV_PART_MAIN);

        /* E-ink optimized Checkbox style */
        lv_obj_set_style_pad_all(cb, 5, LV_PART_INDICATOR);
        lv_obj_set_style_border_width(cb, 2, LV_PART_INDICATOR);
        lv_obj_set_style_border_color(cb, lv_color_black(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(cb, lv_color_white(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(cb, lv_color_black(), LV_PART_INDICATOR | LV_STATE_CHECKED);

        /* Key: calculate column(c) and row(r) and place in grid */
        int c = i % 4; // 0, 1, 2, 3 (column index)
        int r = i / 4; // 0, 1       (row index)
        lv_obj_set_grid_cell(cb, LV_GRID_ALIGN_START, c, 1, LV_GRID_ALIGN_CENTER, r, 1);

        app->view->edit_ctx.repeat_btns[i] = cb;
        if (existing_repeat & s_day_bits[i]) {
            lv_obj_add_state(cb, LV_STATE_CHECKED);
        }
    }

    // /* ==============================================================
    //  *  3. BUTTON ROW  Delete (left) | Save (right)
    //  *     Fix 4: Label section removed entirely.
    //  *     Fix 5: Both buttons on one row.
    //  * ============================================================== */
    make_divider(col);

    lv_obj_t *btn_row = lv_obj_create(col);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Delete -- left side (hidden for new alarm) */
    lv_obj_t *del_btn = lv_btn_create(btn_row);
    ui_style_set_btn_secondary(del_btn);
    lv_obj_set_size(del_btn, 140, LV_SIZE_CONTENT);
    lv_obj_add_event_cb(del_btn, show_delete_confirm_dialog, LV_EVENT_CLICKED, app);
    lv_obj_t *del_lbl = lv_label_create(del_btn);
    lv_label_set_text(del_lbl, "Delete");
    lv_obj_center(del_lbl);
    app->view->edit_ctx.btn_delete = del_btn;

    if (is_new) {
        lv_obj_add_flag(del_btn, LV_OBJ_FLAG_HIDDEN);
        /* invisible spacer to keep Save on the right */
        lv_obj_t *sp = lv_obj_create(btn_row);
        lv_obj_remove_style_all(sp);
        lv_obj_set_size(sp, 1, 1);
    }

    /* Save -- right side */
    lv_obj_t *save_btn = lv_btn_create(btn_row);
    ui_style_set_btn_primary(save_btn);
    lv_obj_set_size(save_btn, 140, LV_SIZE_CONTENT);
    lv_obj_add_event_cb(save_btn, clock_controller_on_alarm_save, LV_EVENT_CLICKED, app);
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, "Save");
    lv_obj_center(save_lbl);

    app->view->edit_ctx.btn_save  = save_btn;

    return page.screen;
}

void clock_view_alarm_edit_init_registry(struct ClockApp *app)
{
    PAGE_REGISTE(app, PAGE_ALARM_EDIT, build_alarm_edit_page);
}
