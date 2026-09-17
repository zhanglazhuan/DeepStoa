#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include "esp_log.h"

#include "controller_timer.h"
#include "view_timer.h"
#include "../controller.h"
#include "../view.h"
#include "../model.h"
#include "../storage.h"
#include "../app.h"

static const char *TAG = "todolist_controller_timer";

// 全局应用实例
extern TodoListApp g_todolist_app;

/* 轮询周期。这个定时器只唤醒 CPU 去比对显示值，不等于一次面板刷新 ——
 * 面板只在 todolist_view_timer_display_key() 变化时才推一帧。
 * 5 秒足够：最细的显示粒度是 10 秒。 */
#define TIMER_POLL_PERIOD_MS 5000

static void timer_tick_cb(lv_timer_t * timer);

/* 把「当前应该显示成什么」推到面板，值没变就什么都不做。 */
static void timer_push_display(TodoListController* controller, bool force)
{
    TodoListTimerController* ctx = controller->timer_ctx;
    todolist_task_t * t = todolist_model_get_active_task(controller->model);
    if (!t) return;

    uint32_t elapsed = todolist_model_timer_elapsed_s(controller->model, t);
    uint32_t rem = (elapsed >= t->estimate_s) ? 0 : (t->estimate_s - elapsed);
    uint32_t key = todolist_view_timer_display_key(rem);

    if (!force && ctx->has_display_key && ctx->last_display_key == key) return;

    ctx->last_display_key = key;
    ctx->has_display_key = true;
    todolist_view_timer_refresh_progress(t);
}

static void timer_invalidate_display_key(TodoListController* controller)
{
    if (controller && controller->timer_ctx) controller->timer_ctx->has_display_key = false;
}

// 定时器回调：到点则入 Done，否则按需刷新
static void timer_tick_cb(lv_timer_t * timer) {
    (void)timer;
    TodoListController* controller = g_todolist_app.controller;
    if(!controller || !controller->timer_ctx) return;
    if(!todolist_model_timer_is_running(controller->model)) return;

    todolist_task_t * t = todolist_model_get_active_task(controller->model);
    if(!t) return;

    uint32_t elapsed = todolist_model_timer_elapsed_s(controller->model, t);

    if(elapsed >= t->estimate_s) {
        /* 时间到。注意顺序：mark_task_done() 内部会 memmove 压缩 todo 数组并清零尾项，
         * 之后 t 指向的已经是别的任务了 —— 所以必须先用还有效的指针刷完最后一帧，
         * 再动数组，最后把当前任务清空。 */
        uint32_t done_id = t->id;

        timer_push_display(controller, true);

        todolist_model_mark_task_done(controller->model, done_id);
        todolist_model_set_active_task(controller->model, 0);
        timer_invalidate_display_key(controller);
        todolist_view_timer_sync(NULL, false);
        todolist_storage_flush();   /* 任务完成是个值得立刻落盘的节点 */
        return;
    }

    timer_push_display(controller, false);
}

// 对外初始化：创建计时器
void controller_timer_init(struct TodoListController* controller) {
    if (!controller || !controller->timer_ctx) return;
    TodoListTimerController* ctx = controller->timer_ctx;

    ctx->timer = lv_timer_create(timer_tick_cb, TIMER_POLL_PERIOD_MS, NULL);
    /* 默认不跑：只有停在 Timer 页、且确实在计时时才需要轮询 */
    if (ctx->timer) lv_timer_pause(ctx->timer);
}

void controller_timer_deinit(struct TodoListController* controller) {
    if (controller->timer_ctx != NULL) {
        if (controller->timer_ctx->timer) lv_timer_delete(controller->timer_ctx->timer);
        free(controller->timer_ctx);
        controller->timer_ctx = NULL;
    }
}

/* 只有「在 Timer 页 + 正在计时」才需要轮询。
 * 其他情况让定时器停着 —— 已用时是按时间戳算的，不靠 tick 累加，停掉不会丢时间。 */
void controller_timer_resume_polling(struct TodoListController* controller) {
    if (!controller || !controller->timer_ctx || !controller->timer_ctx->timer) return;
    if (!todolist_model_timer_is_running(controller->model)) return;
    lv_timer_resume(controller->timer_ctx->timer);
    lv_timer_reset(controller->timer_ctx->timer);
}

void controller_timer_pause_polling(struct TodoListController* controller) {
    if (!controller || !controller->timer_ctx || !controller->timer_ctx->timer) return;
    lv_timer_pause(controller->timer_ctx->timer);
}

// Timer 页：开始/暂停
void controller_on_timer_start_pause(lv_event_t * e) {
    (void)e;
    TodoListController* controller = g_todolist_app.controller;
    if(!controller || !controller->timer_ctx) return;

    const todolist_task_t * t = todolist_model_get_active_task(controller->model);
    if(!t) return;

    bool running;
    if (todolist_model_timer_is_running(controller->model)) {
        todolist_model_timer_pause(controller->model);
        controller_timer_pause_polling(controller);
        running = false;
    } else {
        running = todolist_model_timer_start(controller->model);
        controller_timer_resume_polling(controller);
    }

    /* 只有按钮文案变了，把刷新窗口切到按钮上，别整屏刷 */
    todolist_view_timer_set_running(running);
    /* 暂停/继续时把剩余时间也对一次，避免显示停在上一档 */
    timer_invalidate_display_key(controller);
    timer_push_display(controller, false);
}

/* 停表并把当前任务切到 next_id（0 = 没有下一个）。
 * 用 set_active_task 而不是 active_todo_task：后者会顺带结算上一段计时，
 * 而这里的上一段已经在 mark_task_done / timer_pause 里结算过了。 */
static void timer_switch_to_task(TodoListController* controller, uint32_t next_id) {
    todolist_model_timer_pause(controller->model);
    controller_timer_pause_polling(controller);

    todolist_model_set_active_task(controller->model, next_id);
    timer_invalidate_display_key(controller);
    todolist_view_timer_sync(todolist_model_get_active_task(controller->model), false);
}

// Timer 页：跳过到下一个任务
void controller_on_timer_skip(lv_event_t * e) {
    (void)e;
    TodoListController* controller = g_todolist_app.controller;
    if(!controller || !controller->timer_ctx) return;

    /* 之前只更新了视图却没有更新 active_task_id —— 界面显示下一个任务，
     * 计时器却还在跑上一个。这里一并切过去。 */
    todolist_task_t * next = todolist_model_get_next_todo_task(controller->model);
    timer_switch_to_task(controller, next ? next->id : 0);
}

// Timer 页：完成当前任务
void controller_on_timer_finish(lv_event_t * e) {
    (void)e;
    TodoListController* controller = g_todolist_app.controller;
    if(!controller || !controller->timer_ctx) return;

    const todolist_task_t * t = todolist_model_get_active_task(controller->model);
    if(!t) return;

    /* 必须先算出下一个任务的 id：mark_task_done() 会把当前任务移出 todo 数组，
     * 之后 get_next_todo_task() 靠 active_task_id 查位置就永远查不到了（返回 NULL），
     * 界面会直接掉成 "None"。next 指针同样会在 memmove 后失效，所以只留 id。 */
    uint32_t done_id = t->id;
    todolist_task_t * next = todolist_model_get_next_todo_task(controller->model);
    uint32_t next_id = (next && next->id != done_id) ? next->id : 0;

    todolist_model_mark_task_done(controller->model, done_id);
    timer_switch_to_task(controller, next_id);
    todolist_storage_flush();
}

// Timer 页：调整预计时长
void controller_on_timer_adjust_estimate(lv_event_t * e) {
    TodoListController* controller = g_todolist_app.controller;
    if(!controller || !controller->timer_ctx) return;

    // 获取调整值（分钟）
    int adjust = (int)(intptr_t)lv_event_get_user_data(e);

    // 获取当前任务
    todolist_task_t * t = todolist_model_get_active_task(controller->model);
    if(!t) return;

    /* 调整的是「预计时长」本身，不要把已用时减掉：
     * 原来的 estimate + adjust*60 - elapsed 会让「已经跑了 3 分钟时按 +5」反而变短。
     * 下限是 1 分钟（不是 1 秒）。 */
    int64_t new_estimate = (int64_t)t->estimate_s + (int64_t)adjust * 60;
    if(new_estimate < 60) new_estimate = 60;
    t->estimate_s = (uint32_t)new_estimate;
    todolist_storage_mark_todo_dirty();

    ESP_LOGI(TAG, "adjust_estimate %+d min -> estimate_s=%u", adjust, (unsigned)t->estimate_s);

    timer_invalidate_display_key(controller);
    todolist_view_timer_sync(t, todolist_model_timer_is_running(controller->model));
}
