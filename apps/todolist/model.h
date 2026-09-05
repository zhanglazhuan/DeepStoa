#ifndef TODOLIST_MODEL_H
#define TODOLIST_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include "app.h"

#define TODOLIST_MAX_TASKS 64
#define TODOLIST_TITLE_MAX 128

// 应用配置结构
typedef struct {
    uint32_t task_duration_min;   // 默认任务时长（分钟）
    uint32_t rest_duration_min;   // 默认休息时长（分钟）
    uint32_t pre_alert_min;       // 提前提醒（分钟）
} todolist_config_t;

// 任务结构
// 注意：布局一旦改动，storage.c 里的 TODOLIST_STORAGE_VERSION 必须 +1
typedef struct {
    uint32_t id;  // 任务唯一ID
    char title[TODOLIST_TITLE_MAX];
    uint32_t estimate_s;  // 预计时长（秒）
    uint32_t elapsed_s;   // 已累计用时（秒），不含「正在跑的这一段」
} todolist_task_t;

// 任务列表模型
typedef struct TodoListModel {
    todolist_task_t * todo_tasks;         // 任务数组
    uint8_t todo_task_count;
    todolist_task_t * done_tasks;         // 任务数组
    uint8_t done_task_count;
    uint32_t _id_generator;               // 下一个任务ID
    uint32_t active_task_id;              // tab timer task id

    /* 计时状态。
     * run_started_at 是墙钟秒（0 = 没在计时），会持久化，用来跨睡眠/重启恢复。
     * run_started_us 是开机以来的单调微秒，只存内存，用来实际累加 ——
     * 墙钟会被 SNTP 同步一次性推进几十年，拿它算增量会瞬间把任务算成已完成。 */
    uint32_t run_started_at;
    int64_t  run_started_us;

    todolist_config_t config;             // 应用配置
} TodoListModel;

void todolist_model_init(struct TodoListApp* app);
void todolist_model_deinit(struct TodoListApp* app);

void todolist_model_config_load(TodoListModel* model);
void todolist_model_config_save(TodoListModel* model);

/* 返回新任务 id；0 表示失败（列表已满） */
uint32_t todolist_model_add_todo_task(TodoListModel* model, const char *title, uint32_t estimate_s);
todolist_task_t* todolist_model_get_todo_task(TodoListModel* model, uint32_t id);
bool todolist_model_delete_todo_task(TodoListModel* model, uint32_t id);
todolist_task_t* todolist_model_get_todo_task_at(TodoListModel* model, uint8_t idx);

todolist_task_t* todolist_model_get_done_task(TodoListModel* model, uint32_t id);
bool todolist_model_delete_done_task(TodoListModel* model, uint32_t id);
bool todolist_model_clear_all_done_tasks(TodoListModel* model);
bool todolist_model_restore_todo_task(TodoListModel* model, uint32_t id);
todolist_task_t* todolist_model_get_done_task_at(TodoListModel* model, uint8_t idx);

bool todolist_model_mark_task_done(TodoListModel* model, uint32_t id);
bool todolist_model_reorder_todo_task(TodoListModel* model, uint8_t from_idx, uint8_t to_idx);

/* 从 Todo 列表选中一个任务开始计时：切换当前任务并停表（保留已用时） */
bool todolist_model_active_todo_task(TodoListModel* model, uint32_t id);
/* 只切换当前任务，id = 0 表示清除 */
bool todolist_model_set_active_task(TodoListModel* model, uint32_t id);
todolist_task_t* todolist_model_get_active_task(TodoListModel* model);

todolist_task_t* todolist_model_get_next_todo_task(TodoListModel* model);
bool todolist_model_update_todo_task(TodoListModel* model, uint32_t task_id, const char* title, uint32_t estimate_s);

/* ── 计时 ─────────────────────────────────────────────────────────────── */

bool     todolist_model_timer_is_running(const TodoListModel* model);
/* 开始/继续计时。没有当前任务时返回 false。 */
bool     todolist_model_timer_start(TodoListModel* model);
/* 暂停并把这一段累加进 task->elapsed_s */
void     todolist_model_timer_pause(TodoListModel* model);
/* 当前任务的实际已用时（含正在跑的这一段） */
uint32_t todolist_model_timer_elapsed_s(const TodoListModel* model, const todolist_task_t* t);
/* 冷启动后调用：根据持久化的墙钟起点恢复或丢弃未完成的计时 */
void     todolist_model_timer_restore(TodoListModel* model);

todolist_config_t * todolist_model_config_get(void);
void todolist_model_config_set(uint32_t task_duration_min, uint32_t rest_duration_min, uint32_t pre_alert_min);

#endif // TODOLIST_MODEL_H
