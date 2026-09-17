/**
 * @file lv_ui_style_guide.h
 * @brief Unified button style presets for I1 e-ink.
 *
 * Style only — layout & size belong to the caller.
 *
 * Usage:
 *   #include "lv_ui_style_guide.h"
 *   lv_obj_t *btn = lv_button_create(parent);
 *   ui_style_set_btn_primary(btn);
 *   lv_obj_set_size(btn, LV_PCT(90), 48);  // caller controls size
 */

#ifndef LV_UI_STYLE_GUIDE_H
#define LV_UI_STYLE_GUIDE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Button appearance ─────────────────────────────────────────────── */

void ui_style_set_btn_primary(lv_obj_t *btn);
void ui_style_set_btn_secondary(lv_obj_t *btn);

/* ── Dropdown appearance ───────────────────────────────────────────── */

void ui_style_set_dropdown(lv_obj_t *dd);

/* ── Switch appearance ─────────────────────────────────────────────── */

/* Size is part of the preset here (unlike buttons): the switch draws its
 * knob as a full-height circle, so track height and knob diameter are the
 * same number — the caller can't set one without the other. */
#define UI_SWITCH_W  46
#define UI_SWITCH_H  26

void ui_style_set_switch(lv_obj_t *sw);

/* ── Button size presets (width: content / LV_PCT, height: fixed) ──── */

#define UI_BTN_H_LARGE   48
#define UI_BTN_H_MEDIUM  40
#define UI_BTN_H_SMALL   32

/* 页面底部的主操作按钮：Clock 的 Add Alarm / Start·Reset、Chatbot 的
 * Hold to Talk 等。整宽（或整行 flex_grow），固定高度，离屏幕底边留出 GAP。
 *
 * GAP 是"离屏幕底边的总距离"，不是 margin。父容器各自有不同的 pad_bottom，
 * 所以调用点要写：
 *     margin_bottom = UI_BOTTOM_ACTION_GAP - <父容器 pad_bottom>
 * 想整体上移/下移所有页面的底部按钮，改这里一处即可。 */
#define UI_BOTTOM_ACTION_H     60
#define UI_BOTTOM_ACTION_GAP   32

/* 弹层（lv_bottom_sheet）里动作按钮的尺寸。
 * 约定：两枚并排横排，**Cancel 在左、主按钮在右** —— 主按钮位置固定，
 * 用户不用每个弹层重新找。不要再用上下堆叠的 100% 宽按钮。 */
#define UI_SHEET_BTN_W   160
#define UI_SHEET_BTN_H   56

void ui_style_set_btn_size_large(lv_obj_t *btn);
void ui_style_set_btn_size_medium(lv_obj_t *btn);
void ui_style_set_btn_size_small(lv_obj_t *btn);

#ifdef __cplusplus
}
#endif

#endif
