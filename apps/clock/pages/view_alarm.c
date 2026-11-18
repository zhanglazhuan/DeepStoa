// Alarm tab: construction, row updates, and alarm-specific dialogs.
#include <stdio.h>
#include <string.h>
#include <lvgl.h>
#include "alarm_service.h"
#include "lv_ui_style_guide.h"
#include "../controller.h"
#include "../model.h"
#include "../view.h"
#include "lv_bottom_sheet.h"
#include "view_alarm_edit.h"
#include "view_alarm.h"

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
void clock_view_alarm_set_row_enabled(lv_obj_t *sw, bool enabled)
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

lv_obj_t *clock_view_alarm_build(lv_obj_t *tab_content, ClockApp *app)
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

    clock_view_alarm_rebuild_list(app);

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

/* ── 重建闹钟列表 ────────────────────────────────────────────── */

void clock_view_alarm_rebuild_list(ClockApp *app)
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

void clock_view_alarm_show_max_warning(void)
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
