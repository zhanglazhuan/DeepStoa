#include <string.h>
#include <time.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "flash_control.h"

#include "model.h"
#include "storage.h"
#include "app.h"

// 全局应用实例
extern TodoListApp g_todolist_app;

static const char *TAG = "todolist_model";

/* 跨重启恢复计时时允许的最大间隔。超过这个值就当作「用户早就不在做这件事了」，
 * 丢弃未完成的那一段，而不是把一整夜算进任务里。 */
#define TODOLIST_RESUME_MAX_GAP_S (12 * 3600)

void todolist_model_init(TodoListApp* app) {
    app->model = (TodoListModel*)malloc(sizeof(TodoListModel));
    if (app->model == NULL) {
        ESP_LOGE(TAG, "Model memory allocation failed");
        return;
    }
    memset(app->model, 0, sizeof(TodoListModel));

    app->model->todo_tasks = malloc(sizeof(todolist_task_t) * TODOLIST_MAX_TASKS);
    if (!app->model->todo_tasks) {
        ESP_LOGE(TAG, "Todo task array memory allocation failed");
        free(app->model); app->model = NULL;
        return;
    }
    app->model->todo_task_count = 0;

    app->model->done_tasks = malloc(sizeof(todolist_task_t) * TODOLIST_MAX_TASKS);
    if (!app->model->done_tasks) {
        ESP_LOGE(TAG, "Done task array memory allocation failed");
        free(app->model->todo_tasks); free(app->model); app->model = NULL;
        return;
    }
    app->model->done_task_count = 0;

    app->model->_id_generator = 1;
    app->model->active_task_id = 0;

    // 初始化配置
    todolist_model_config_load(app->model);

    // load tasks
    todolist_storage_init(app->model);
    todolist_storage_load_all(app->model);
    todolist_model_timer_restore(app->model);
}

void todolist_model_deinit(TodoListApp* app) {
    if (app->model == NULL) return;

    /* 退出前先把这一段计时结算掉并落盘，否则本次计时白跑 */
    todolist_model_timer_pause(app->model);
    todolist_storage_deinit();

    if (app->model->todo_tasks != NULL) {
        free(app->model->todo_tasks);
        app->model->todo_tasks = NULL;
    }
    if (app->model->done_tasks != NULL) {
        free(app->model->done_tasks);
        app->model->done_tasks = NULL;
    }

    free(app->model);
    app->model = NULL;
}

static int16_t _find_todo_task_index_by_id(TodoListModel* model, uint32_t id) {
    for(uint8_t i = 0; i < model->todo_task_count; i++) {
        if(model->todo_tasks[i].id == id) return (int16_t)i;
    }
    return -1;
}

static int16_t _find_done_task_index_by_id(TodoListModel* model, uint32_t id) {
    for(uint8_t i = 0; i < model->done_task_count; i++) {
        if(model->done_tasks[i].id == id) return (int16_t)(i);
    }
    return -1;
}

uint32_t todolist_model_add_todo_task(TodoListModel* model, const char *title, uint32_t estimate_s) {
    if(model->todo_task_count >= TODOLIST_MAX_TASKS) return 0;   /* 0 = 失败 */
    todolist_task_t *t = &(model->todo_tasks[model->todo_task_count]);
    t->id = model->_id_generator++;
    strncpy(t->title, title ? title : "", TODOLIST_TITLE_MAX - 1);
    t->title[TODOLIST_TITLE_MAX - 1] = '\0';
    t->estimate_s = estimate_s;
    t->elapsed_s = 0;
    model->todo_task_count++;

    todolist_storage_mark_meta_dirty();
    todolist_storage_mark_todo_dirty();
    return t->id;
}

bool todolist_model_delete_todo_task(TodoListModel* model, uint32_t id) {
    int32_t idx = _find_todo_task_index_by_id(model, id);
    if (idx < 0) return false;

    if (idx == model->todo_task_count - 1) {
        memset(&model->todo_tasks[idx], 0, sizeof(todolist_task_t));
    } else {
        memmove(&model->todo_tasks[idx], &model->todo_tasks[idx + 1], sizeof(todolist_task_t) * (model->todo_task_count - idx - 1));
        memset(&model->todo_tasks[model->todo_task_count - 1], 0, sizeof(todolist_task_t));
    }

    model->todo_task_count--;

    /* 被删掉的正好是当前计时任务：停表并清除，别留一个查不到的 id */
    if (model->active_task_id == id) {
        model->run_started_at = 0;
        model->active_task_id = 0;
        todolist_storage_mark_meta_dirty();
    }

    todolist_storage_mark_todo_dirty();
    return true;
}

bool todolist_model_delete_done_task(TodoListModel* model, uint32_t id) {
    int32_t idx = _find_done_task_index_by_id(model, id);
    if (idx < 0) return false;

    if (idx == model->done_task_count - 1) {
        memset(&model->done_tasks[idx], 0, sizeof(todolist_task_t));
    } else {
        memmove(&model->done_tasks[idx], &model->done_tasks[idx + 1], sizeof(todolist_task_t) * (model->done_task_count - idx - 1));
        memset(&model->done_tasks[model->done_task_count - 1], 0, sizeof(todolist_task_t));
    }

    model->done_task_count--;

    todolist_storage_mark_done_dirty();
    return true;
}

bool todolist_model_clear_all_done_tasks(TodoListModel* model) {
    if(model->done_task_count == 0) return false;
    memset(model->done_tasks, 0, sizeof(todolist_task_t) * model->done_task_count);
    model->done_task_count = 0;
    todolist_storage_mark_done_dirty();
    return true;
}

bool todolist_model_mark_task_done(TodoListModel* model, uint32_t id) {
    int32_t idx = _find_todo_task_index_by_id(model, id);
    if(idx < 0) return false;

    /* 结算正在跑的这一段，否则完成记录里的用时会少掉最后一截 */
    if (model->active_task_id == id) {
        todolist_model_timer_pause(model);
        idx = _find_todo_task_index_by_id(model, id);
        if (idx < 0) return false;
    }

    todolist_task_t t = model->todo_tasks[idx];

    // 加入 done task 数组
    if(model->done_task_count >= TODOLIST_MAX_TASKS){
        todolist_model_delete_done_task(model, model->done_tasks[0].id);
    }
    model->done_tasks[model->done_task_count] = t;
    model->done_task_count++;

    // 从 todo task 中移除
    todolist_model_delete_todo_task(model, t.id);

    todolist_storage_mark_done_dirty();
    return true;
}

bool todolist_model_restore_todo_task(TodoListModel* model, uint32_t id) {
    int32_t idx = _find_done_task_index_by_id(model, id);
    if(idx < 0) return false;

    todolist_task_t task = model->done_tasks[idx];
    task.elapsed_s = 0;

    // 添加到 todo list
    if (model->todo_task_count >= TODOLIST_MAX_TASKS) {
        return false;
    }
    model->todo_tasks[model->todo_task_count] = task;
    model->todo_task_count++;

    // 从 done list 删除
    todolist_model_delete_done_task(model, id);

    todolist_storage_mark_todo_dirty();
    return true;
}

todolist_task_t* todolist_model_get_todo_task_at(TodoListModel* model, uint8_t idx) {
    if(idx >= model->todo_task_count) return NULL;
    return &model->todo_tasks[idx];
}

todolist_task_t* todolist_model_get_done_task_at(TodoListModel* model, uint8_t idx) {
    if(idx >= model->done_task_count) return NULL;
    return &model->done_tasks[idx];
}

todolist_task_t* todolist_model_get_todo_task(TodoListModel* model, uint32_t id) {
    if (!model) return NULL;
    int32_t idx = _find_todo_task_index_by_id(model, id);
    if(idx < 0) return NULL;
    return &(model->todo_tasks[idx]);
}

todolist_task_t* todolist_model_get_done_task(TodoListModel* model, uint32_t id) {
    if (!model) return NULL;
    int32_t idx = _find_done_task_index_by_id(model, id);
    if(idx < 0) return NULL;
    return &(model->done_tasks[idx]);
}

bool todolist_model_reorder_todo_task(TodoListModel* model, uint8_t from_idx, uint8_t to_idx) {
    if(from_idx == to_idx) return true;
    int32_t todo_count = model->todo_task_count;
    if(from_idx >= todo_count || to_idx >= todo_count) return false;

    ESP_LOGD(TAG, "Reorder task %d to %d", from_idx, to_idx);

    todolist_task_t temp = model->todo_tasks[from_idx];
    if(from_idx < to_idx) {
        memmove(&model->todo_tasks[from_idx], &model->todo_tasks[from_idx+1], sizeof(todolist_task_t) * (to_idx - from_idx));
        model->todo_tasks[to_idx] = temp;
    } else {
        memmove(&model->todo_tasks[to_idx+1], &model->todo_tasks[to_idx], sizeof(todolist_task_t) * (from_idx - to_idx));
        model->todo_tasks[to_idx] = temp;
    }

    todolist_storage_mark_todo_dirty();
    return true;
}

bool todolist_model_active_todo_task(TodoListModel* model, uint32_t id) {
    int32_t idx = _find_todo_task_index_by_id(model, id);
    if(idx < 0) return false;

    /* 切换任务前先把上一个任务正在跑的那一段结算掉 */
    todolist_model_timer_pause(model);

    /* 这里不再清零 elapsed_s：同一个任务中途切走再点回来应该续算，
     * 「重新开始」是另一个明确的动作，不该由「点一下任务」隐式触发。 */
    model->active_task_id = id;

    todolist_storage_mark_meta_dirty();
    return true;
}

/* 只切换当前任务，不动 elapsed_s。
 * 与 todolist_model_active_todo_task() 的区别：后者还会结算上一段计时。
 * id 传 0 表示清除当前任务（比如任务已经被标记完成、不再存在于 Todo 列表里）。 */
bool todolist_model_set_active_task(TodoListModel* model, uint32_t id) {
    if (!model) return false;
    if (id != 0 && _find_todo_task_index_by_id(model, id) < 0) return false;

    model->active_task_id = id;
    todolist_storage_mark_meta_dirty();
    return true;
}

todolist_task_t* todolist_model_get_active_task(TodoListModel* model) {
    if (!model) return NULL;
    int32_t idx = _find_todo_task_index_by_id(model, model->active_task_id);
    if(idx < 0) return NULL;
    return &(model->todo_tasks[idx]);
}

todolist_task_t* todolist_model_get_next_todo_task(TodoListModel* model) {
    int32_t idx = _find_todo_task_index_by_id(model, model->active_task_id);
    if(idx < 0) return NULL;
    if(model->todo_task_count == 0) return NULL;
    uint8_t next_idx = (idx + 1) % model->todo_task_count;
    return &(model->todo_tasks[next_idx]);
}

bool todolist_model_update_todo_task(TodoListModel* model, uint32_t task_id, const char* title, uint32_t estimate_s) {
    if (!model) return false;
    if (title == NULL) return false;

    int32_t idx = _find_todo_task_index_by_id(model, task_id);
    if(idx < 0) return false;
    todolist_task_t* task = &model->todo_tasks[idx];

    strncpy(task->title, title, TODOLIST_TITLE_MAX - 1);
    task->title[TODOLIST_TITLE_MAX - 1] = '\0';
    task->estimate_s = estimate_s;

    ESP_LOGI(TAG, "Update task %u: %u s", (unsigned)task_id, (unsigned)estimate_s);

    todolist_storage_mark_todo_dirty();
    return true;
}

/* ── 计时 ─────────────────────────────────────────────────────────────── */

bool todolist_model_timer_is_running(const TodoListModel* model) {
    return model && model->run_started_at != 0;
}

bool todolist_model_timer_start(TodoListModel* model) {
    if (!model) return false;
    if (todolist_model_get_active_task(model) == NULL) return false;
    if (model->run_started_at != 0) return true;   /* 已经在跑 */

    model->run_started_at = (uint32_t)time(NULL);
    if (model->run_started_at == 0) model->run_started_at = 1;  /* 0 是「未计时」的哨兵 */
    model->run_started_us = esp_timer_get_time();

    todolist_storage_mark_meta_dirty();
    return true;
}

void todolist_model_timer_pause(TodoListModel* model) {
    if (!model || model->run_started_at == 0) return;

    int64_t delta_us = esp_timer_get_time() - model->run_started_us;
    uint32_t delta_s = (delta_us > 0) ? (uint32_t)(delta_us / 1000000) : 0;

    todolist_task_t *t = todolist_model_get_active_task(model);
    if (t) {
        t->elapsed_s += delta_s;
        todolist_storage_mark_todo_dirty();
    }

    model->run_started_at = 0;
    model->run_started_us = 0;
    todolist_storage_mark_meta_dirty();
}

uint32_t todolist_model_timer_elapsed_s(const TodoListModel* model, const todolist_task_t* t) {
    if (!t) return 0;
    if (!model || model->run_started_at == 0) return t->elapsed_s;
    if (todolist_model_get_active_task((TodoListModel*)model) != t) return t->elapsed_s;

    int64_t delta_us = esp_timer_get_time() - model->run_started_us;
    uint32_t delta_s = (delta_us > 0) ? (uint32_t)(delta_us / 1000000) : 0;
    return t->elapsed_s + delta_s;
}

void todolist_model_timer_restore(TodoListModel* model) {
    if (!model || model->run_started_at == 0) return;

    todolist_task_t *t = todolist_model_get_active_task(model);
    uint32_t now = (uint32_t)time(NULL);

    /* 墙钟必须看起来正常：单调递增、且间隔在可信范围内。
     * 掉电后 RTC 归零、或者 SNTP 把时间往前推，都会落到 else 分支。 */
    if (t && now > model->run_started_at &&
        (now - model->run_started_at) <= TODOLIST_RESUME_MAX_GAP_S) {
        uint32_t gap = now - model->run_started_at;
        t->elapsed_s += gap;
        ESP_LOGI(TAG, "resumed timer, +%u s while away", (unsigned)gap);

        /* 继续跑：重置两个起点 */
        model->run_started_at = (now != 0) ? now : 1;
        model->run_started_us = esp_timer_get_time();
        todolist_storage_mark_todo_dirty();
        todolist_storage_mark_meta_dirty();
    } else {
        ESP_LOGW(TAG, "cannot trust wall clock across restart, dropping pending run");
        model->run_started_at = 0;
        model->run_started_us = 0;
        todolist_storage_mark_meta_dirty();
    }
}

// 配置相关函数实现
todolist_config_t * todolist_model_config_get(void) {
    if (g_todolist_app.model == NULL) {
        return NULL;  // 安全返回，让调用方的 if (config) 生效
    }
    return &g_todolist_app.model->config;
}

void todolist_model_config_set(uint32_t task_duration_min, uint32_t rest_duration_min, uint32_t pre_alert_min) {
    if (g_todolist_app.model) {
        g_todolist_app.model->config.task_duration_min = task_duration_min;
        g_todolist_app.model->config.rest_duration_min = rest_duration_min;
        g_todolist_app.model->config.pre_alert_min = pre_alert_min;
    }
}

// 从 FlashDB 加载配置
void todolist_model_config_load(TodoListModel* model) {
    struct fdb_blob blob;
    size_t saved_len = fdb_kv_get_blob(&g_kvdb, "app_cfg", fdb_blob_make(&blob, &model->config, sizeof(todolist_config_t)));

    // 如果没有找到配置，或者大小不匹配，则使用默认值
    if (saved_len != sizeof(todolist_config_t)) {
        ESP_LOGI(TAG, "No valid config in FlashDB, using defaults.");
        model->config.task_duration_min = 25;
        model->config.rest_duration_min = 5;
        model->config.pre_alert_min = 2;
    } else {
        ESP_LOGI(TAG, "Config loaded from FlashDB.");
    }
}

// 保存配置到 FlashDB
void todolist_model_config_save(TodoListModel* model) {
    if (model == NULL) return;
    struct fdb_blob blob;
    fdb_err_t err = fdb_kv_set_blob(&g_kvdb, "app_cfg", fdb_blob_make(&blob, &model->config, sizeof(todolist_config_t)));
    if (err == FDB_NO_ERR) {
        ESP_LOGI(TAG, "Config saved to FlashDB.");
    } else {
        ESP_LOGE(TAG, "Failed to save config to FlashDB (%d).", err);
    }
}
