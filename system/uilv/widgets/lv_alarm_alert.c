/**
 * @file lv_alarm_alert.c
 * @brief 全局闹钟 / 计时提醒弹层，见 lv_alarm_alert.h。
 */

#include "lv_alarm_alert.h"

#include <stdio.h>
#include <string.h>

#include <lvgl.h>

#include "../theme/lv_theme_hardcore.h"
#include "../theme/lv_ui_style_guide.h"
#include "app_event.h"
#include "../utils/lv_epd_region.h"

/* 同一时刻只允许一个提醒。第二条来了就覆盖内容，不叠弹层 ——
 * 墨水屏上叠两层半透明遮罩会糊成一片，而且用户只需要按一次“知道了”。 */
static lv_obj_t *s_overlay   = NULL;
static lv_obj_t *s_title_lbl = NULL;
static lv_obj_t *s_sub_lbl   = NULL;

static void on_dismiss(lv_event_t *e)
{
    (void)e;
    if (s_overlay) {
        lv_obj_delete_async(s_overlay);
        s_overlay   = NULL;
        s_title_lbl = NULL;
        s_sub_lbl   = NULL;
    }
}

static void build_overlay(void)
{
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_overlay, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(s_overlay, 32, 0);
    lv_obj_set_style_pad_row(s_overlay, 20, 0);

    /* 主行：时间或“时间到” —— 站在一米外也要能看清 */
    s_title_lbl = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_title_lbl, LV_FONT_LARGE, 0);
    lv_obj_set_style_text_color(s_title_lbl, lv_color_black(), 0);
    lv_obj_set_style_text_align(s_title_lbl, LV_TEXT_ALIGN_CENTER, 0);

    s_sub_lbl = lv_label_create(s_overlay);
    lv_label_set_long_mode(s_sub_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_sub_lbl, LV_PCT(90));
    lv_obj_set_style_text_font(s_sub_lbl, LV_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_sub_lbl, lv_color_black(), 0);
    lv_obj_set_style_text_align(s_sub_lbl, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *btn = lv_button_create(s_overlay);
    ui_style_set_btn_primary(btn);
    lv_obj_set_size(btn, LV_PCT(70), 72);
    lv_obj_set_style_margin_top(btn, 24, 0);
    lv_obj_add_event_cb(btn, on_dismiss, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Dismiss");
    lv_obj_set_style_text_font(lbl, LV_FONT_SMALL, 0);
    lv_obj_center(lbl);
}

static void show(const char *title, const char *sub)
{
    /* 提醒是整屏的，而某些页面（比如倒计时）正把面板刷新窗口钉在一小块上。
     * 不先解除，弹层就只会画出那一小块。 */
    if (epd_region_is_active()) epd_region_end();

    if (!s_overlay || !lv_obj_is_valid(s_overlay)) build_overlay();

    lv_label_set_text(s_title_lbl, title);
    lv_label_set_text(s_sub_lbl, sub ? sub : "");
    lv_obj_move_foreground(s_overlay);
}

static void on_event(app_event_t event, const void *data)
{
    if (event == APP_EVENT_ALARM_FIRED) {
        const app_event_alarm_t *a = data;
        char title[16];
        snprintf(title, sizeof(title), "%02u:%02u",
                 (unsigned)(a ? a->hour : 0), (unsigned)(a ? a->minute : 0));
        show(title, (a && a->label[0]) ? a->label : "Alarm");
    } else if (event == APP_EVENT_TIMER_FINISHED) {
        show("00:00", "Timer finished");
    }
}

void lv_alarm_alert_init(void)
{
    app_event_register(on_event);
}
