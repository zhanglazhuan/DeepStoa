#include <stdint.h>
#include <string.h>
#include <lvgl.h>
#include "esp_log.h"

#include "controller_todo.h"
#include "view_todo.h"
#include "view_timer.h"
#include "../view.h"
#include "../model.h"
#include "../controller.h"
#include "../utils.h"
#include "../app.h"

// 全局应用实例
extern TodoListApp g_todolist_app;

static const char *TAG = "todolist_controller_todo";

/* 所有行事件的 user_data 都是任务 id（不是指针，见 view_todo.c 的说明） */
static uint32_t event_task_id(lv_event_t * e) {
    return (uint32_t)(uintptr_t)lv_event_get_user_data(e);
}

static void async_switch_to_timer_cb(void * user_data) {
    (void)user_data;

    /* 触发页面切换 ( true 代表要求底层真正执行状态改变 ) */
    todolist_controller_active_tab(1, true);
    /* Timer tab 建好时会自己从 model 水合，这里不需要再手动灌数据 */
}

// 点击或右滑：切换到 Timer 页面并选中该任务
static void start_task_timer(uint32_t task_id) {
    if (!todolist_model_active_todo_task(g_todolist_app.model, task_id)) return;
    lv_async_call(async_switch_to_timer_cb, NULL);
}

void controller_on_todo_label_swipe(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);

    uint32_t task_id = event_task_id(e);
    if (task_id == 0) return;

    if (code == LV_EVENT_CLICKED) {
        start_task_timer(task_id);
        return;
    }

    if (code == LV_EVENT_GESTURE) {
        lv_indev_t * indev = lv_indev_get_act();
        if (!indev) return;

        lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        ESP_LOGD(TAG, "gesture dir: %d", dir);

        if (dir == LV_DIR_LEFT) {
            /* 左滑 = 删除（带二次确认）。About 页和出厂教程的文案必须与这里一致。 */
            view_todo_show_delete_dialog(task_id);
        }
        else if (dir == LV_DIR_RIGHT) {
            start_task_timer(task_id);
        }
    }
}

void controller_on_todo_label_edit(lv_event_t * e) {
    uint32_t task_id = event_task_id(e);
    if (task_id == 0) {
        ESP_LOGW(TAG, "Edit failed: no task id");
        return;
    }

    /* 必须用 PAGE_NAVIGATE_TO 而不是裸的 navigate_to：只有宏会把当前页压入
     * 返回栈并更新 current_page，否则表单里的 confirm/cancel 调 navigate_pop
     * 会因为栈空而失败（日志: "already at root, cannot go back"）。
     * 传的是 id 而不是 todolist_task_t*：表单页要跨页面存活，期间后台计时器
     * 完成一个任务就会 memmove 压缩数组，指针会指向另一条任务。 */
    PAGE_NAVIGATE_TO(&g_todolist_app, PAGE_TASK_FORM, (void *)(uintptr_t)task_id);
}

static int32_t controller_calc_target_index(lv_obj_t * list, lv_coord_t py) {
    int32_t child_cnt = lv_obj_get_child_cnt(list);
    if(child_cnt <= 0) return -1;
    for(int32_t i = 0; i < child_cnt; i++) {
        lv_obj_t * child = lv_obj_get_child(list, i);
        lv_area_t child_coords;
        lv_obj_get_coords(child, &child_coords);
        lv_coord_t mid = (child_coords.y1 + child_coords.y2) / 2;
        if(py < mid) return i;
    }
    return child_cnt - 1;
}

void controller_on_todo_drag_pressed(lv_event_t * e) {
    TodoListViewHomeCtx *home_ctx = (TodoListViewHomeCtx*)g_todolist_app.view->page_nav.nav_ctx;
    if (!home_ctx) return;

    lv_obj_t * handle = lv_event_get_target(e);
    lv_obj_t * row = lv_obj_get_parent(handle);
    home_ctx->drag_ctx.row = row;
    home_ctx->drag_ctx.from_index = lv_obj_get_index(row);

    // Disable list scrolling while dragging to avoid touch conflicts
    lv_obj_t * list = lv_obj_get_parent(row);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    lv_indev_t * indev = lv_indev_get_act();
    if(indev) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        home_ctx->drag_ctx.start_y = p.y;
    }
}

void controller_on_todo_drag_pressing(lv_event_t * e) {
    (void)e;
    TodoListViewHomeCtx *home_ctx = (TodoListViewHomeCtx*)g_todolist_app.view->page_nav.nav_ctx;
    if (!home_ctx || !home_ctx->drag_ctx.row) return;

    lv_obj_t * row = home_ctx->drag_ctx.row;
    lv_obj_t * list = lv_obj_get_parent(row);

    lv_indev_t * indev = lv_indev_get_act();
    if(!indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    // 单击不重排：需要超过一定拖拽距离才移动
    if(LV_ABS(p.y - home_ctx->drag_ctx.start_y) < DRAG_THRESHOLD_PX) return;

    int32_t target = controller_calc_target_index(list, p.y);
    if(target < 0) return;

    int32_t cur = lv_obj_get_index(row);
    if(target != cur) {
        lv_obj_move_to_index(row, target); // 仅调整 UI 顺序
    }
}

void controller_on_todo_drag_released(lv_event_t * e) {
    (void)e;
    TodoListViewHomeCtx *home_ctx = (TodoListViewHomeCtx*)g_todolist_app.view->page_nav.nav_ctx;
    if (!home_ctx || !home_ctx->drag_ctx.row) return;

    lv_obj_t * row = home_ctx->drag_ctx.row;

    // Restore list scrolling capabilities
    lv_obj_t * list = lv_obj_get_parent(row);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    int32_t to_idx   = lv_obj_get_index(row);
    int32_t from_idx = home_ctx->drag_ctx.from_index;

    if(from_idx != to_idx) {
        if(todolist_model_reorder_todo_task(g_todolist_app.model, from_idx, to_idx)) {
            // Safely refresh the list on the next LVGL tick
            lv_async_call(view_refresh_todo_list, NULL);
        }
    }

    home_ctx->drag_ctx.row = NULL;
    home_ctx->drag_ctx.from_index = -1;
}

// 加号按钮点击：进入新建表单（user_data = 0 表示新建）
void controller_on_add_btn_clicked(lv_event_t * e) {
    (void)e;
    /* 同上：用宏才会 push 返回栈，表单的 Confirm/Cancel 才能 pop 回来 */
    PAGE_NAVIGATE_TO(&g_todolist_app, PAGE_TASK_FORM, NULL);
}

// 待办复选框：勾选即标记完成
void controller_on_todo_checkbox_changed(lv_event_t * e) {
    lv_obj_t * cb = lv_event_get_target(e);
    if(!lv_obj_has_state(cb, LV_STATE_CHECKED)) return;

    uint32_t task_id = event_task_id(e);
    if (task_id == 0) return;

    ESP_LOGI(TAG, "mark task %u done", (unsigned)task_id);
    todolist_model_mark_task_done(g_todolist_app.model, task_id);

    lv_async_call(view_refresh_todo_list, NULL);
}
