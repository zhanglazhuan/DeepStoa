#include <stdint.h>
#include <lvgl.h>
#include "esp_log.h"
#include "controller_done.h"
#include "view_done.h"
#include "../view.h"
#include "../model.h"
#include "../app.h"

// 全局应用实例
extern TodoListApp g_todolist_app;

static const char *TAG = "todolist_controller_done";

/* 行事件的 user_data 是任务 id（不是指针，见 view_done.c 的说明） */
static uint32_t event_task_id(lv_event_t * e) {
    return (uint32_t)(uintptr_t)lv_event_get_user_data(e);
}

static void refresh_done_async(void *unused) {
    (void)unused;
    view_refresh_done_list();
}

// Done 页：取消勾选 = 恢复成待办
void controller_on_done_checkbox_changed(lv_event_t * e) {
    lv_obj_t * cb = lv_event_get_target(e);
    if(lv_obj_has_state(cb, LV_STATE_CHECKED)) return;

    uint32_t task_id = event_task_id(e);
    if (task_id == 0) return;

    if (!todolist_model_restore_todo_task(g_todolist_app.model, task_id)) {
        ESP_LOGW(TAG, "restore task %u failed (todo list full?)", (unsigned)task_id);
    }

    /* 恢复之后这一行必须消失。原来漏了刷新，行会一直留在 Done 列表里直到切 tab。
     * 用 async：现在还在这一行自己的事件回调里，不能同步 clean 掉父容器。 */
    lv_async_call(refresh_done_async, NULL);
}

void controller_on_done_delete(lv_event_t * e) {
    uint32_t task_id = event_task_id(e);
    if (task_id == 0) return;

    todolist_model_delete_done_task(g_todolist_app.model, task_id);
    lv_async_call(refresh_done_async, NULL);
}
