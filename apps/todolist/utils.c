#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lv_theme_hardcore.h"
#include "lv_bottom_sheet.h"
#include "lv_ui_style_guide.h"
#include "utils.h"

const char* lv_event_code_str(lv_event_code_t code) {
    switch(code) {
        case LV_EVENT_PRESSED: return "LV_EVENT_PRESSED";
        case LV_EVENT_PRESSING: return "LV_EVENT_PRESSING";
        case LV_EVENT_PRESS_LOST: return "LV_EVENT_PRESS_LOST";
        case LV_EVENT_SHORT_CLICKED: return "LV_EVENT_SHORT_CLICKED";
        case LV_EVENT_CLICKED: return "LV_EVENT_CLICKED";
        case LV_EVENT_LONG_PRESSED: return "LV_EVENT_LONG_PRESSED";
        case LV_EVENT_LONG_PRESSED_REPEAT: return "LV_EVENT_LONG_PRESSED_REPEAT";
        /* LV_EVENT_DOUBLE_CLICKED exists only in some LVGL versions */
        #ifdef LV_EVENT_DOUBLE_CLICKED
        case LV_EVENT_DOUBLE_CLICKED: return "LV_EVENT_DOUBLE_CLICKED";
        #endif
        case LV_EVENT_VALUE_CHANGED: return "LV_EVENT_VALUE_CHANGED";
        case LV_EVENT_READY: return "LV_EVENT_READY";
        case LV_EVENT_CANCEL: return "LV_EVENT_CANCEL";
        case LV_EVENT_FOCUSED: return "LV_EVENT_FOCUSED";
        case LV_EVENT_DEFOCUSED: return "LV_EVENT_DEFOCUSED";
        case LV_EVENT_KEY: return "LV_EVENT_KEY";
        case LV_EVENT_SCROLL_BEGIN: return "LV_EVENT_SCROLL_BEGIN";
        case LV_EVENT_SCROLL_END: return "LV_EVENT_SCROLL_END";
        case LV_EVENT_SCROLL: return "LV_EVENT_SCROLL";
        case LV_EVENT_GESTURE: return "LV_EVENT_GESTURE";
        case LV_EVENT_SIZE_CHANGED: return "LV_EVENT_SIZE_CHANGED";
        case LV_EVENT_DELETE: return "LV_EVENT_DELETE";
        default: return "LV_EVENT_UNKNOWN";
    }
}

static const char * key_to_name(uint32_t c) {
    switch(c) {
        case LV_KEY_ENTER:     return "ENTER";
        case LV_KEY_BACKSPACE: return "BACKSPACE";
        case LV_KEY_DEL:       return "DEL";
        case LV_KEY_LEFT:      return "LEFT";
        case LV_KEY_RIGHT:     return "RIGHT";
        case LV_KEY_UP:        return "UP";
        case LV_KEY_DOWN:      return "DOWN";
        case LV_KEY_HOME:      return "HOME";
        case LV_KEY_END:       return "END";
        case LV_KEY_ESC:       return "ESC";
        case LV_KEY_NEXT:      return "NEXT";
        case LV_KEY_PREV:      return "PREV";
        default:               return NULL;
    }
}

void ta_debug_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);

    if(code == LV_EVENT_KEY) {
        uint32_t k = *((uint32_t *)lv_event_get_param(e));
        const char * name = key_to_name(k);
        if(name) {
            printf("[TA] KEY: %s (%lu)\n", name, (unsigned long)k);
        } else if(k >= 32 && k < 127) {
            printf("[TA] KEY: '%c' (%lu)\n", (char)k, (unsigned long)k);
        } else {
            printf("[TA] KEY: U+%04lX (%lu)\n", (unsigned long)k, (unsigned long)k);
        }
    } else if(code == LV_EVENT_INSERT) {
        const char * txt = (const char *)lv_event_get_param(e);
        printf("[TA] INSERT: \"%s\"\n", txt ? txt : "(null)");
    } else if(code == LV_EVENT_VALUE_CHANGED) {
        const char * t = lv_textarea_get_text(ta);
        uint32_t pos = lv_textarea_get_cursor_pos(ta);
        printf("[TA] VALUE_CHANGED: \"%s\" (cursor=%lu)\n", t ? t : "", (unsigned long)pos);
    }
}

/* ── 统一的二次确认弹窗 ───────────────────────────────────────────────── */

typedef struct {
    lv_obj_t              *bg;
    todolist_confirm_cb_t  on_confirm;
    void                  *user_data;
} confirm_ctx_t;

static void confirm_close(confirm_ctx_t *ctx)
{
    if (ctx && ctx->bg) lv_obj_delete_async(ctx->bg);
}

static void confirm_bg_free_cb(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

static void confirm_ok_cb(lv_event_t *e)
{
    confirm_ctx_t *ctx = lv_event_get_user_data(e);
    if (!ctx) return;
    if (ctx->on_confirm) ctx->on_confirm(ctx->user_data);
    confirm_close(ctx);
}

static void confirm_cancel_cb(lv_event_t *e)
{
    confirm_close(lv_event_get_user_data(e));
}

/* sheet 里的整宽动作按钮：primary = 实心（危险动作），否则描边 */
/* 弹层的动作按钮行：横排，Cancel 在左、主按钮在右。
 * SPACE_EVENLY 让两枚朝中间收，两侧留出边距，不贴屏幕边。
 * 尺寸/约定见 lv_ui_style_guide.h 的 UI_SHEET_BTN_*。 */
static lv_obj_t *confirm_btn_row(lv_obj_t *parent)
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

static lv_obj_t *confirm_action_btn(lv_obj_t *parent, const char *text, bool primary,
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

void todolist_confirm_dialog(const char *title, const char *message,
                             const char *confirm_txt,
                             todolist_confirm_cb_t on_confirm, void *user_data)
{
    lv_bottom_sheet_t *bs = lv_bottom_sheet_create(lv_layer_top());
    if (!bs) return;

    confirm_ctx_t *ctx = malloc(sizeof(confirm_ctx_t));
    if (!ctx) { lv_bottom_sheet_close(bs); return; }
    memset(ctx, 0, sizeof(confirm_ctx_t));
    ctx->on_confirm = on_confirm;
    ctx->user_data  = user_data;
    ctx->bg         = bs->overlay;   /* 关闭时用它 */
    /* ctx 跟着遮罩走：按钮关的、点遮罩关的、整屏销毁的，DELETE 都会到 */
    lv_obj_add_event_cb(bs->overlay, confirm_bg_free_cb, LV_EVENT_DELETE, ctx);

    /* 刻意不调 lv_bottom_sheet_add_header()：它自带关闭 ×，加上 Cancel 和
     * 确认就是三个出口，用户还得先分辨"哪个是取消"。标题直接进内容区。 */
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

    /* 横排：Cancel 左、主按钮右 */
    lv_obj_t *row = confirm_btn_row(c);
    confirm_action_btn(row, "Cancel",    false, confirm_cancel_cb, ctx);
    confirm_action_btn(row, confirm_txt, true,  confirm_ok_cb,     ctx);
}
