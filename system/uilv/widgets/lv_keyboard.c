/**
 * @file lv_keyboard.c
 * @brief Custom keyboard widget implementation
 *
 * Ported from D:\Codes\EPOS\epos\epos_lv\widgets\epos_keyboard.c
 *
 * Changes from EPOS:
 *   - #include "drivers/epd_display_control.h" → "display_control.h"
 *     (the DeepStoa controller component provides the same API)
 */

#include <lvgl.h>
#include "display_control.h"
#include "../utils/lv_epd_region.h"
#include "lv_keyboard.h"

/* ── Custom lowercase keyboard map (5 rows, USER_1) ───────────────────── */

static const char *const custom_kb_map_lc[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    "ABC", "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_NEW_LINE, "\n",
    ",", ".", "?", " ", "!", LV_SYMBOL_BACKSPACE, ""
};

/* ── Custom uppercase keyboard map (5 rows, USER_2) ───────────────────── */

static const char *const custom_kb_map_uc[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    "abc", "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_NEW_LINE, "\n",
    ",", ".", "?", " ", "!", LV_SYMBOL_BACKSPACE, ""
};

/* ── Button width weights (shared by both cases) ──────────────────────── */

static const lv_buttonmatrix_ctrl_t custom_kb_ctrl_map[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 1, 1, 1, 1, 1, 1, 1, 2,
    1, 1, 1, 4, 1, 2
};

/* ── Shift (case) toggle ──────────────────────────────────────────────── */

static void custom_kb_event_cb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_current_target(e);
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    uint32_t btn_id = lv_buttonmatrix_get_selected_button(kb);
    if (btn_id == LV_BUTTONMATRIX_BUTTON_NONE) return;

    const char *txt = lv_buttonmatrix_get_button_text(kb, btn_id);
    if (txt == NULL) return;

    if (lv_strcmp(txt, "ABC") == 0) {
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_2);
        lv_event_stop_processing(e);
    }
    else if (lv_strcmp(txt, "abc") == 0) {
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
        lv_event_stop_processing(e);
    }
}

/* ── Public API ───────────────────────────────────────────────────────── */

void setup_custom_keyboard(lv_obj_t *kb)
{
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, custom_kb_map_lc, custom_kb_ctrl_map);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_2, custom_kb_map_uc, custom_kb_ctrl_map);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);

    /* 顶部黑色分割线：只画顶部，不画底部 */
    lv_obj_set_style_border_width(kb, 2, 0);
    lv_obj_set_style_border_side(kb, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(kb, lv_color_black(), 0);

    lv_obj_remove_event_cb(kb, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(kb, custom_kb_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(kb, lv_keyboard_def_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

lv_obj_t *epos_keyboard_create_number(lv_obj_t *parent)
{
    lv_obj_t *kb = lv_keyboard_create(parent);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -8);
    return kb;
}

/* ── 让开键盘的滚动容器 ───────────────────────────────────────────────
 * 同一时刻只可能有一个键盘弹出来，所以用文件级状态就够了。
 * s_scroller 可能指向已销毁的对象，用之前一律过 lv_obj_is_valid()
 * （它是遍历对象树查的，对野指针安全）。 */
static lv_obj_t *s_scroller;
static int32_t   s_scroller_h;

void epos_keyboard_set_scroller(lv_obj_t *scroller)
{
    s_scroller = scroller;
    /* 记下原始高度（可能是 LV_PCT 编码值），收起键盘时原样还原 */
    s_scroller_h = scroller ? lv_obj_get_style_height(scroller, LV_PART_MAIN) : 0;
}

/** 把滚动容器压到键盘顶边以上，并把输入框滚进来。 */
static void scroller_make_room(lv_obj_t *kb, lv_obj_t *ta)
{
    if (!s_scroller || !lv_obj_is_valid(s_scroller)) return;

    lv_obj_update_layout(lv_obj_get_screen(kb));

    lv_area_t kb_area, sc_area;
    lv_obj_get_coords(kb, &kb_area);
    lv_obj_get_coords(s_scroller, &sc_area);

    int32_t avail = kb_area.y1 - sc_area.y1;
    if (avail < 80) return;          /* 剩不下可视区就别折腾了，还不如全遮 */

    lv_obj_set_height(s_scroller, avail);
    lv_obj_update_layout(s_scroller);
    /* recursive：输入框和滚动容器之间通常还隔着一层 row */
    lv_obj_scroll_to_view_recursive(ta, LV_ANIM_OFF);
}

static void scroller_restore(void)
{
    if (!s_scroller || !lv_obj_is_valid(s_scroller)) return;
    lv_obj_set_height(s_scroller, s_scroller_h);
    lv_obj_scroll_to_y(s_scroller, 0, LV_ANIM_OFF);
}

/** 换绑输入框，顺带把边框粗细切过去。 */
static void bind_textarea(lv_obj_t *kb, lv_obj_t *ta)
{
    lv_obj_t *prev = lv_keyboard_get_textarea(kb);
    if (prev && prev != ta && lv_obj_is_valid(prev)) {
        lv_obj_set_style_border_width(prev, EPOS_KB_TA_BORDER_IDLE, LV_PART_MAIN);
    }
    lv_keyboard_set_textarea(kb, ta);
    if (ta) {
        lv_obj_set_style_border_width(ta, EPOS_KB_TA_BORDER_ACTIVE, LV_PART_MAIN);
    }
}

void epos_keyboard_ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    lv_obj_t *kb = lv_event_get_user_data(e);

    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        bool was_hidden = lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN);

        bind_textarea(kb, ta);
        if (was_hidden) {
            lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(kb);
            lv_obj_update_layout(kb);
        }

        /* 先让位再定刷新窗口：滚动会改变输入框的坐标，
         * 顺序反了窗口就框在滚动前的位置上。 */
        scroller_make_room(kb, ta);

        if (was_hidden) {
            /* 先整屏打一帧（把键盘这些静态内容打到面板并同步差分基准），
             * 之后窗口收到输入框上，打字只刷输入框那一块。
             * 这里原来是手抄的三步流程，聚焦 outline 的外扩也漏了 ——
             * 统一走 lv_epd_region，那些坑在 helper 里已经处理过。 */
            epd_region_begin_focus(lv_screen_active(), ta);
        } else {
            epd_region_flush_obj(ta);
        }

        /* 必须置末尾：lv_textarea 会把光标设到点击的位置
         * （lv_textarea.c:1002），居中的 "07" 点中间光标落在两位之间，
         * 退格只能删掉左边那位，剩的一位怎么按都删不掉。 */
        lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY
               || code == LV_EVENT_CANCEL) {
        /* READY = ✓，CANCEL = 右上角键盘图标。LVGL 把这两个键先发给键盘，
         * 再转发给绑定的输入框（lv_keyboard.c:358/368），所以挂在输入框上
         * 就能接到。 */
        if (!lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN)) {
            lv_indev_t *indev = lv_indev_get_act();
            if (indev) {
                /* 吃掉这次松手，否则键盘一藏，同一次触摸会穿透点到底下的控件 */
                lv_indev_wait_release(indev);
            }
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

            epd_region_end();
        }

        scroller_restore();
        bind_textarea(kb, NULL);
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);

        if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
            lv_event_stop_processing(e);
        }
    }
}

void epos_keyboard_attach(lv_obj_t *ta, lv_obj_t *kb)
{
    if (!ta || !kb) return;

    lv_obj_set_style_border_width(ta, EPOS_KB_TA_BORDER_IDLE, LV_PART_MAIN);

    lv_obj_add_event_cb(ta, epos_keyboard_ta_event_cb, LV_EVENT_FOCUSED,   kb);
    lv_obj_add_event_cb(ta, epos_keyboard_ta_event_cb, LV_EVENT_CLICKED,   kb);
    lv_obj_add_event_cb(ta, epos_keyboard_ta_event_cb, LV_EVENT_DEFOCUSED, kb);
    lv_obj_add_event_cb(ta, epos_keyboard_ta_event_cb, LV_EVENT_READY,     kb);
    lv_obj_add_event_cb(ta, epos_keyboard_ta_event_cb, LV_EVENT_CANCEL,    kb);
}
