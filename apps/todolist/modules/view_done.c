#include <stdio.h>
#include "esp_log.h"

#include "view_done.h"
#include "view_todo.h"
#include "controller_done.h"

#include "../model.h"
#include "../controller.h"
#include "../app.h"

// 全局应用实例
extern TodoListApp g_todolist_app;

static const char *TAG = "todolist_view_done";

/* 与 Todo 行保持同样的行高和间距，见 view_todo.c 里的说明 */
#define DONE_ROW_MIN_H     56
#define DONE_ROW_PAD_V     10
#define DONE_ROW_GAP       16
#define DONE_ICON_HIT_EXT  12

lv_obj_t * view_create_done_row(lv_obj_t * parent, const todolist_task_t * t) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, DONE_ROW_MIN_H, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, DONE_ROW_GAP, 0);
    lv_obj_set_style_pad_top(row, DONE_ROW_PAD_V, 0);
    lv_obj_set_style_pad_bottom(row, DONE_ROW_PAD_V, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

    // --- 2. 左侧：Checkbox ---
    lv_obj_t * cb = lv_checkbox_create(row);
    lv_checkbox_set_text(cb, "");
    lv_obj_add_state(cb, LV_STATE_CHECKED);
    lv_obj_set_style_pad_right(cb, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_right(cb, -8, LV_PART_MAIN);
    lv_obj_set_style_transform_width(cb, -6, LV_PART_INDICATOR);
    lv_obj_set_style_transform_height(cb, -6, LV_PART_INDICATOR);
    lv_obj_set_style_text_font(cb, LV_FONT_TINY, LV_PART_INDICATOR);

    // 安全防御标志位
    lv_obj_clear_flag(cb, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(cb, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(cb, LV_OBJ_FLAG_PRESS_LOCK); // 关键：按下锁定
    /* 绑 id 不绑指针：done 数组删除时同样会 memmove 压缩 */
    lv_obj_add_event_cb(cb, controller_on_done_checkbox_changed, LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)t->id);

    // --- 3. 中间：文本 (Label) ---
    lv_obj_t * label = lv_label_create(row);
    lv_obj_set_style_text_font(label, LV_FONT_SMALL, 0);
    lv_obj_set_width(label, 0);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text_fmt(label, "%s", t->title);
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // --- 4. 实际用时 ---
    lv_obj_t * dur = lv_label_create(row);
    lv_label_set_text_fmt(dur, "%um", (unsigned)((t->elapsed_s + 59) / 60));
    lv_obj_set_style_text_font(dur, LV_FONT_TINY, 0);

    // --- 5. 右侧：Delete 图标 ---
    lv_obj_t * del_icon = lv_label_create(row);
    lv_label_set_text(del_icon, LV_SYMBOL_TRASH);
    lv_obj_set_style_margin_right(del_icon, 8, LV_PART_MAIN);
    lv_obj_set_style_text_font(del_icon, LV_FONT_SMALL, 0);
    lv_obj_set_style_text_color(del_icon, lv_color_black(), 0);

    lv_obj_set_ext_click_area(del_icon, DONE_ICON_HIT_EXT);
    lv_obj_add_flag(del_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(del_icon, LV_OBJ_FLAG_PRESS_LOCK); // 关键：按下锁定
    lv_obj_clear_flag(del_icon, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(del_icon, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(del_icon, controller_on_done_delete, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)t->id);
    return row;
}

void view_refresh_done_list(void) {
    TodoListViewHomeCtx *home_ctx = (TodoListViewHomeCtx*)g_todolist_app.view->page_nav.nav_ctx;
    if (!home_ctx || !home_ctx->done_list) return;

    lv_obj_clean(home_ctx->done_list);

    uint8_t cnt = g_todolist_app.model->done_task_count;
    ESP_LOGD(TAG, "view_refresh_done_list cnt: %d", cnt);

    if (cnt == 0) {
        lv_obj_t *hint = lv_label_create(home_ctx->done_list);
        lv_label_set_text(hint, "Nothing finished yet.");
        lv_obj_set_style_text_font(hint, LV_FONT_SMALL, 0);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(hint, LV_PCT(100));
        lv_obj_set_style_margin_top(hint, 64, 0);
        return;
    }

    for(int32_t i = 0; i < cnt; i++) {
        const todolist_task_t * t = todolist_model_get_done_task_at(g_todolist_app.model, i);
        if (t) (void)view_create_done_row(home_ctx->done_list, t);
    }
}

void view_build_done_tab(TodoListView* v) {
    TodoListViewHomeCtx *home_ctx = (TodoListViewHomeCtx*)v->page_nav.nav_ctx;
    lv_obj_t * tab = home_ctx->tab_done;
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab, 2, 0);

    lv_obj_t * done_list = lv_obj_create(tab);
    lv_obj_remove_style_all(done_list);
    lv_obj_set_width(done_list, LV_PCT(100));
    lv_obj_set_flex_flow(done_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(done_list, LV_DIR_VER);
    lv_obj_set_flex_grow(done_list, 1);
    lv_obj_set_style_pad_top(done_list, 8, 0);
    home_ctx->done_list = done_list;

    view_refresh_done_list();
}
