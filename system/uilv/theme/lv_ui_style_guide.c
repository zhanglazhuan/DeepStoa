/**
 * @file lv_ui_style_guide.c
 * @brief Button style presets — the sole source of button styling.
 *
 * Theme no longer touches buttons, so no need to fight theme styles here.
 * Style-only: color, border, radius, padding.
 * Size & position belong to the caller.
 */

#include "lv_ui_style_guide.h"

/* ── All styles in one struct ──────────────────────────────────────── */

typedef struct {
    lv_style_t primary;
    lv_style_t secondary;
    lv_style_t dropdown;
    bool       inited;
} ui_styles_t;

static ui_styles_t g_styles;

static void init_once(void)
{
    if (g_styles.inited) return;

    /* Primary: black fill + white text */
    lv_style_init(&g_styles.primary);
    lv_style_set_bg_opa(&g_styles.primary, LV_OPA_COVER);
    lv_style_set_bg_color(&g_styles.primary, lv_color_black());
    lv_style_set_border_width(&g_styles.primary, 0);
    lv_style_set_text_color(&g_styles.primary, lv_color_white());
    lv_style_set_radius(&g_styles.primary, 4);
    lv_style_set_pad_all(&g_styles.primary, 10);

    /* Secondary: transparent + 2px black border */
    lv_style_init(&g_styles.secondary);
    lv_style_set_bg_opa(&g_styles.secondary, LV_OPA_TRANSP);
    lv_style_set_border_width(&g_styles.secondary, 2);
    lv_style_set_border_color(&g_styles.secondary, lv_color_black());
    lv_style_set_text_color(&g_styles.secondary, lv_color_black());
    lv_style_set_radius(&g_styles.secondary, 4);
    lv_style_set_pad_all(&g_styles.secondary, 10);

    /* Dropdown: white fill + 1px black border + radius 2 (matches card) */
    lv_style_init(&g_styles.dropdown);
    lv_style_set_bg_opa(&g_styles.dropdown, LV_OPA_COVER);
    lv_style_set_bg_color(&g_styles.dropdown, lv_color_white());
    lv_style_set_border_width(&g_styles.dropdown, 1);
    lv_style_set_border_color(&g_styles.dropdown, lv_color_black());
    lv_style_set_text_color(&g_styles.dropdown, lv_color_black());
    lv_style_set_radius(&g_styles.dropdown, 2);
    lv_style_set_pad_all(&g_styles.dropdown, 8);

    g_styles.inited = true;
}

/* ── Public ────────────────────────────────────────────────────────── */

void ui_style_set_btn_primary(lv_obj_t *btn)
{
    init_once();
    lv_obj_add_style(btn, &g_styles.primary, 0);
}

void ui_style_set_btn_secondary(lv_obj_t *btn)
{
    init_once();
    lv_obj_add_style(btn, &g_styles.secondary, 0);
}

void ui_style_set_dropdown(lv_obj_t *dd)
{
    init_once();
    lv_obj_add_style(dd, &g_styles.dropdown, 0);
}

/* ── Switch ────────────────────────────────────────────────────────── */

/* 主题给 switch 的默认样式在 1bit 屏上辨识度不够，各页面自己乱改也不统一。
 * 这里固化 clock 页里已经验证好用的那套画法：
 *   轨道  关 = 白底黑边   开 = 实心黑
 *   滑块  关 = 实心黑     开 = 白底黑边
 * 两个状态都有大块黑白反差，一眼能看出拨到哪边了。 */
void ui_style_set_switch(lv_obj_t *sw)
{
    lv_obj_set_size(sw, UI_SWITCH_W, UI_SWITCH_H);

    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sw, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sw, lv_color_black(),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(sw, 1, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(sw, lv_color_black(), LV_PART_INDICATOR);
    lv_obj_set_style_radius(sw, UI_SWITCH_H / 2, LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sw, lv_color_black(), LV_PART_KNOB);
    lv_obj_set_style_border_width(sw, 0, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sw, lv_color_white(),
                              LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(sw, 1, LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(sw, lv_color_black(),
                                  LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_KNOB);
}

/* ── Size presets ──────────────────────────────────────────────────── */

void ui_style_set_btn_size_large(lv_obj_t *btn)
{
    lv_obj_set_height(btn, UI_BTN_H_LARGE);
}

void ui_style_set_btn_size_medium(lv_obj_t *btn)
{
    lv_obj_set_height(btn, UI_BTN_H_MEDIUM);
}

void ui_style_set_btn_size_small(lv_obj_t *btn)
{
    lv_obj_set_height(btn, UI_BTN_H_SMALL);
}
