/**
 * @file lv_keyboard.h
 * @brief Custom keyboard widget — user-1 lowercase layout + number pad
 *
 * Ported from D:\Codes\EPOS\epos\epos_lv\widgets\epos_keyboard.h
 */

#ifndef LV_KEYBOARD_WIDGET_H
#define LV_KEYBOARD_WIDGET_H

#include <lvgl.h>

/**
 * @brief Configure a keyboard object with the custom user-1 lowercase map.
 *
 * After calling this, the keyboard is set to LV_KEYBOARD_MODE_USER_1
 * with a QWERTY-like lowercase layout.
 */
void setup_custom_keyboard(lv_obj_t *kb);

/**
 * @brief Create a numeric keyboard hidden by default, aligned to bottom.
 *
 * The keyboard is created as a floating object on @p parent and hidden.
 * Use epos_keyboard_ta_event_cb to show/hide it when a textarea is focused.
 *
 * @param parent  Parent object (usually the screen).
 * @return        The keyboard object.
 */
lv_obj_t *epos_keyboard_create_number(lv_obj_t *parent);

/**
 * @brief Standard textarea event callback for showing/hiding the keyboard.
 *
 * Attach to a textarea's LV_EVENT_ALL or specific events (FOCUSED, CLICKED,
 * DEFOCUSED, READY, CANCEL).  The keyboard is shown on focus/click and
 * hidden on defocus/ready/cancel.  Partial-window e-ink flush is used while
 * the keyboard is visible to reduce flicker.
 *
 * 一般直接用 epos_keyboard_attach()，只有想自己挑事件时才用这个。
 *
 * @param e  LVGL event, whose user_data must be the keyboard object.
 */
void epos_keyboard_ta_event_cb(lv_event_t *e);

/* ── 输入框边框：用粗细标出"键盘现在绑在哪个框上" ────────────────────
 * 1bpp 屏上没有高亮色可用，边框宽度是最可靠的强调手段。一页上有好几个
 * 长得一样的输入框时（闹钟的 HH/MM、Timer 的 4 个 Shortcut），不标出来
 * 用户认不出在改哪个。 */
#define EPOS_KB_TA_BORDER_IDLE    1
#define EPOS_KB_TA_BORDER_ACTIVE  3

/**
 * @brief 把一个输入框接到数字键盘上，一次注册好全部交互。
 *
 * 聚焦 / 点击   → 弹出键盘、绑定、**光标置末尾**、加粗边框，
 *                 并（若设了滚动容器）把输入框滚到键盘上方
 * 失焦 / ✓ / 键盘图标 → 收起键盘、解绑、恢复边框、还原滚动容器
 *
 * 光标置末尾这一步是必须的：lv_textarea 会把光标设到点击的位置
 * （lv_textarea.c:1002），居中的 "07" 点中间光标就落在两位之间，
 * 退格只能删掉左边那位，剩下的一位怎么按都删不掉。
 *
 * @param ta  输入框
 * @param kb  epos_keyboard_create_number() 建出来的键盘
 */
void epos_keyboard_attach(lv_obj_t *ta, lv_obj_t *kb);

/**
 * @brief 指定键盘弹出时需要让开的滚动容器。
 *
 * 数字键盘贴底、占半屏，会盖住下半屏的输入框。设了之后，键盘弹出时容器
 * 高度会被压到键盘顶边以上，并把当前输入框滚进可视区；收起时还原。
 *
 * 页面级设置，进页面设一次，页面销毁时传 NULL 清掉。
 *
 * @param scroller  可竖向滚动的内容容器，NULL 表示不需要这个行为
 */
void epos_keyboard_set_scroller(lv_obj_t *scroller);

#endif /* LV_KEYBOARD_WIDGET_H */
