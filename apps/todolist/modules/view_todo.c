#include <stdint.h>
#include <stdio.h>
#include "esp_log.h"
#include "lv_ui_style_guide.h"
#include "lv_toast.h"

#include "view_todo.h"
#include "controller_todo.h"
#include "../controller.h"
#include "../utils.h"
#include "../app.h"

static const char *TAG = "todolist_view_todo";

// 全局应用实例
extern TodoListApp g_todolist_app;

/* 行高与热区。
 * 原来行高是 LV_SIZE_CONTENT + 上下 8px ≈ 40px，低于手指目标的 48px 下限；
 * 右侧两个图标间距 12px、各自 ext_click_area 10px，热区实际是贴着的，容易误触 ——
 * 在墨水屏上一次误触的代价是一整帧刷新。 */
#define TODO_ROW_MIN_H      56
#define TODO_ROW_PAD_V      10
#define TODO_ROW_GAP        16
#define TODO_ICON_HIT_EXT   12
/* 悬浮添加按钮的尺寸与边距，列表底部要留出同样的空间，否则会压住最后一行 */
#define TODO_FAB_SIZE       72
#define TODO_FAB_MARGIN     24

/* 行绑定的是任务 id，不是 todolist_task_t* ——
 * model 的增删改用 memmove 压缩数组，指针在数据变化后会指向另一条任务。 */
static void row_bind_task_id(lv_obj_t *obj, lv_event_cb_t cb, lv_event_code_t code, uint32_t id) {
    lv_obj_add_event_cb(obj, cb, code, (void *)(uintptr_t)id);
}

lv_obj_t * view_create_todo_row(lv_obj_t *parent, const todolist_task_t *t) {
    // --- 1. 创建基础行容器 ---
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, TODO_ROW_MIN_H, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, TODO_ROW_GAP, 0);
    lv_obj_set_style_pad_top(row, TODO_ROW_PAD_V, 0);
    lv_obj_set_style_pad_bottom(row, TODO_ROW_PAD_V, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    row_bind_task_id(row, controller_on_todo_label_swipe, LV_EVENT_GESTURE, t->id);
    row_bind_task_id(row, controller_on_todo_label_swipe, LV_EVENT_CLICKED, t->id);

    // --- 2. 左侧：Checkbox ---
    lv_obj_t * cb = lv_checkbox_create(row);
    lv_checkbox_set_text(cb, "");
    lv_obj_set_style_pad_right(cb, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_right(cb, -8, LV_PART_MAIN);
    lv_obj_set_style_transform_width(cb, -6, LV_PART_INDICATOR);
    lv_obj_set_style_transform_height(cb, -6, LV_PART_INDICATOR);
    lv_obj_set_style_text_font(cb, LV_FONT_TINY, LV_PART_INDICATOR);

    // 安全防御标志位
    lv_obj_clear_flag(cb, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(cb, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(cb, LV_OBJ_FLAG_PRESS_LOCK); // 关键：按下锁定
    row_bind_task_id(cb, controller_on_todo_checkbox_changed, LV_EVENT_VALUE_CHANGED, t->id);

    // --- 3. 中间：文本 (Label) ---
    lv_obj_t * label = lv_label_create(row);
    lv_obj_set_style_text_font(label, LV_FONT_SMALL, 0);
    lv_obj_set_width(label, 0);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text_fmt(label, "%s", t->title);

    /* --- 4. 预计时长：直接显示在行内。
     * 这是台番茄钟设备，计划时间是主信息之一；不显示的话用户每次都要进编辑页确认，
     * 而进出编辑页在墨水屏上就是两帧整屏刷新。 */
    lv_obj_t * dur = lv_label_create(row);
    lv_label_set_text_fmt(dur, "%um", (unsigned)(t->estimate_s / 60));
    lv_obj_set_style_text_font(dur, LV_FONT_TINY, 0);

    // --- 5. 右侧：Edit 图标 ---
    lv_obj_t * edit_icon = lv_label_create(row);
    lv_label_set_text(edit_icon, MY_SYMBOL_EDIT);
    lv_obj_set_style_text_font(edit_icon, &custom_font_normal, 0);
    lv_obj_set_style_text_color(edit_icon, lv_color_black(), 0);
    lv_obj_set_ext_click_area(edit_icon, TODO_ICON_HIT_EXT);
    lv_obj_add_flag(edit_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(edit_icon, LV_OBJ_FLAG_PRESS_LOCK); // 关键：按下锁定
    lv_obj_clear_flag(edit_icon, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(edit_icon, LV_OBJ_FLAG_GESTURE_BUBBLE);
    row_bind_task_id(edit_icon, controller_on_todo_label_edit, LV_EVENT_CLICKED, t->id);

    // --- 6. 最右侧：拖拽句柄 ---
    lv_obj_t * handle = lv_label_create(row);
    lv_label_set_text(handle, MY_SYMBOL_DRAGGABLE);
    lv_obj_set_style_text_font(handle, &custom_font_normal, 0);
    lv_obj_set_style_text_color(handle, lv_color_black(), 0);
    lv_obj_set_ext_click_area(handle, TODO_ICON_HIT_EXT);
    lv_obj_set_style_bg_opa(handle, LV_OPA_TRANSP, 0);

    lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(handle, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(handle, LV_OBJ_FLAG_GESTURE_BUBBLE);

    row_bind_task_id(handle, controller_on_todo_drag_pressed,  LV_EVENT_PRESSED,    t->id);
    row_bind_task_id(handle, controller_on_todo_drag_pressing, LV_EVENT_PRESSING,   t->id);
    row_bind_task_id(handle, controller_on_todo_drag_released, LV_EVENT_RELEASED,   t->id);
    row_bind_task_id(handle, controller_on_todo_drag_released, LV_EVENT_PRESS_LOST, t->id);

    return row;
}

/* 空状态：一片纯白的列表既不解释也不引导，新机第一眼就应该知道下一步做什么 */
static void view_todo_show_empty(lv_obj_t *list, const char *text) {
    lv_obj_t *hint = lv_label_create(list);
    lv_label_set_text(hint, text);
    lv_obj_set_style_text_font(hint, LV_FONT_SMALL, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_margin_top(hint, 64, 0);
}

void view_refresh_todo_list(void * user_data) {
    (void)user_data;
    TodoListViewHomeCtx *home_ctx = (TodoListViewHomeCtx*)g_todolist_app.view->page_nav.nav_ctx;
    if (!home_ctx || !home_ctx->todo_list) return;

    lv_obj_clean(home_ctx->todo_list);

    int32_t cnt = g_todolist_app.model->todo_task_count;
    if (cnt == 0) {
        view_todo_show_empty(home_ctx->todo_list, "No tasks yet.\nTap + to add one.");
        return;
    }
    for(int32_t i = 0; i < cnt; i++) {
        todolist_task_t *t = todolist_model_get_todo_task_at(g_todolist_app.model, i);
        if (t) (void)view_create_todo_row(home_ctx->todo_list, t);
    }
}

void view_build_todo_tab(TodoListView* v) {
    TodoListViewHomeCtx *home_ctx = (TodoListViewHomeCtx*)v->page_nav.nav_ctx;
    lv_obj_t * tab = home_ctx->tab_todo;
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab, 2, 0);

    lv_obj_t * todo_list = lv_obj_create(tab);
    lv_obj_remove_style_all(todo_list);
    /* 只用 flex_grow 定高：原来同时写了 LV_PCT(70) 和 flex_grow=1，两个规则互相打架 */
    lv_obj_set_width(todo_list, LV_PCT(100));
    lv_obj_set_flex_flow(todo_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(todo_list, LV_DIR_VER);
    lv_obj_set_flex_grow(todo_list, 1);
    lv_obj_set_style_pad_top(todo_list, 8, 0);
    /* 给悬浮按钮让出位置，否则最后一行的编辑图标和把手会被压住 */
    lv_obj_set_style_pad_bottom(todo_list, TODO_FAB_SIZE + TODO_FAB_MARGIN, 0);
    home_ctx->todo_list = todo_list;

    // 悬浮的 + 按钮
    lv_obj_t * todo_add_btn = lv_button_create(tab);
    ui_style_set_btn_primary(todo_add_btn);
    lv_obj_set_size(todo_add_btn, TODO_FAB_SIZE, TODO_FAB_SIZE);
    lv_obj_set_ext_click_area(todo_add_btn, 8);
    lv_obj_add_flag(todo_add_btn, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
    lv_obj_align(todo_add_btn, LV_ALIGN_BOTTOM_RIGHT, -TODO_FAB_MARGIN, -TODO_FAB_MARGIN);
    /* 方角而不是圆形：与主题 radius=2 的硬边语言一致，
     * 而且 1bpp 面板画不出抗锯齿，圆形边缘只会变成锯齿。 */
    lv_obj_set_style_radius(todo_add_btn, 2, 0);
    lv_obj_set_style_shadow_width(todo_add_btn, 0, LV_PART_MAIN);

    lv_obj_t * add_lbl = lv_label_create(todo_add_btn);
    lv_label_set_text(add_lbl, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(add_lbl, LV_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(add_lbl, lv_color_white(), 0);
    lv_obj_center(add_lbl);

    lv_obj_add_event_cb(todo_add_btn, controller_on_add_btn_clicked, LV_EVENT_CLICKED, NULL);

    view_refresh_todo_list(NULL);
}

/* ── 删除确认 ─────────────────────────────────────────────────────────── */

static void delete_task_confirmed(void *user_data) {
    uint32_t task_id = (uint32_t)(uintptr_t)user_data;
    if (task_id == 0) return;

    if (todolist_model_delete_todo_task(g_todolist_app.model, task_id)) {
        lv_async_call(view_refresh_todo_list, NULL);
    }
}

void view_todo_show_delete_dialog(uint32_t task_id) {
    const todolist_task_t *task = todolist_model_get_todo_task(g_todolist_app.model, task_id);
    if (!task) {
        ESP_LOGW(TAG, "delete dialog: task %u is gone", (unsigned)task_id);
        return;
    }

    char msg_buf[192];
    snprintf(msg_buf, sizeof(msg_buf), "Delete \"%.120s\"?", task->title);
    todolist_confirm_dialog("Confirm Delete", msg_buf, "Delete",
                            delete_task_confirmed, (void *)(uintptr_t)task_id);
}
