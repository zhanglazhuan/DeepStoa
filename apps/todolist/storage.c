#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "storage.h"
#include "flash_control.h" // 引入你的g_kvdb 实例

static const char *TAG = "todolist_storage";

extern struct fdb_kvdb g_kvdb;

#define KV_FMT   "td_fmt"
#define KV_TODOS "td_todos"
#define KV_DONES "td_dones"
#define KV_META  "td_meta"

/* 数据格式版本。todolist_task_t 的布局一旦变化就必须 +1，否则 OTA 之后
 * read_len / sizeof() 会静默算出错误的条数，把脏数据当任务加载出来。 */
#define TODOLIST_STORAGE_MAGIC   0x4F444F54u  /* "TODO" */
#define TODOLIST_STORAGE_VERSION 1u

/* 延迟写入的去抖时长 */
#define TODOLIST_FLUSH_DEBOUNCE_MS 2000

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t task_size;   /* sizeof(todolist_task_t) */
} todolist_format_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t id_generator;
    uint32_t active_task_id;
    uint32_t run_started_at;  /* 墙钟秒；0 = 当前没有在计时 */
} todolist_meta_t;

/* v0（没有 magic 的旧格式）的 meta 布局，仅用于迁移 */
typedef struct {
    uint32_t id_generator;
    uint32_t active_task_id;
} todolist_meta_v0_t;

/* ── 延迟写入 ─────────────────────────────────────────────────────────── */

static TodoListModel *s_model;
static lv_timer_t    *s_flush_timer;
static bool s_dirty_todo;
static bool s_dirty_done;
static bool s_dirty_meta;

static void flush_timer_cb(lv_timer_t *t)
{
    todolist_storage_flush();
    lv_timer_pause(t);   /* 没有脏数据就不要再醒来 */
}

static void request_flush(void)
{
    if (!s_flush_timer) return;
    lv_timer_reset(s_flush_timer);   /* 重新计时：连续改动合并成一次写入 */
    lv_timer_resume(s_flush_timer);
}

void todolist_storage_mark_todo_dirty(void) { s_dirty_todo = true; request_flush(); }
void todolist_storage_mark_done_dirty(void) { s_dirty_done = true; request_flush(); }
void todolist_storage_mark_meta_dirty(void) { s_dirty_meta = true; request_flush(); }

static void write_format_tag(void)
{
    todolist_format_t fmt = {
        .magic     = TODOLIST_STORAGE_MAGIC,
        .version   = TODOLIST_STORAGE_VERSION,
        .task_size = (uint16_t)sizeof(todolist_task_t),
    };
    struct fdb_blob blob;
    fdb_kv_set_blob(&g_kvdb, KV_FMT, fdb_blob_make(&blob, &fmt, sizeof(fmt)));
}

static void write_list(const char *key, const todolist_task_t *tasks, uint8_t count)
{
    struct fdb_blob blob;
    size_t len = (size_t)count * sizeof(todolist_task_t);
    if (len > 0) {
        fdb_kv_set_blob(&g_kvdb, key, fdb_blob_make(&blob, (void *)tasks, len));
    } else {
        /* 数组空了就删 Key，回收 Flash 空间 */
        fdb_kv_del(&g_kvdb, key);
    }
}

static void write_meta(const TodoListModel *model)
{
    todolist_meta_t meta = {
        .magic           = TODOLIST_STORAGE_MAGIC,
        .version         = TODOLIST_STORAGE_VERSION,
        .reserved        = 0,
        .id_generator    = model->_id_generator,
        .active_task_id  = model->active_task_id,
        .run_started_at  = model->run_started_at,
    };
    struct fdb_blob blob;
    fdb_kv_set_blob(&g_kvdb, KV_META, fdb_blob_make(&blob, &meta, sizeof(meta)));
}

void todolist_storage_flush(void)
{
    if (!s_model) return;
    if (!s_dirty_todo && !s_dirty_done && !s_dirty_meta) return;

    if (s_dirty_todo) {
        write_list(KV_TODOS, s_model->todo_tasks, s_model->todo_task_count);
        s_dirty_todo = false;
    }
    if (s_dirty_done) {
        write_list(KV_DONES, s_model->done_tasks, s_model->done_task_count);
        s_dirty_done = false;
    }
    if (s_dirty_meta) {
        write_meta(s_model);
        s_dirty_meta = false;
    }
    ESP_LOGD(TAG, "flushed");
}

void todolist_storage_init(TodoListModel* model)
{
    s_model = model;
    s_dirty_todo = s_dirty_done = s_dirty_meta = false;

    if (!s_flush_timer) {
        s_flush_timer = lv_timer_create(flush_timer_cb, TODOLIST_FLUSH_DEBOUNCE_MS, NULL);
        if (s_flush_timer) lv_timer_pause(s_flush_timer);
    }
}

void todolist_storage_deinit(void)
{
    todolist_storage_flush();
    if (s_flush_timer) {
        lv_timer_delete(s_flush_timer);
        s_flush_timer = NULL;
    }
    s_model = NULL;
}

/* ── 加载 ─────────────────────────────────────────────────────────────── */

/* 返回 true 表示存量数据的布局与当前固件一致，可以直接加载 */
static bool storage_format_is_compatible(void)
{
    todolist_format_t fmt = {0};
    struct fdb_blob blob;
    size_t len = fdb_kv_get_blob(&g_kvdb, KV_FMT,
                                 fdb_blob_make(&blob, &fmt, sizeof(fmt)));

    if (len != sizeof(fmt) || fmt.magic != TODOLIST_STORAGE_MAGIC) {
        /* v0：没有格式头。本版本的 todolist_task_t 布局与 v0 相同，
         * 直接按当前布局读，读完补写格式头即可。 */
        ESP_LOGI(TAG, "no format tag, treating stored data as v0");
        write_format_tag();
        return true;
    }

    if (fmt.version != TODOLIST_STORAGE_VERSION ||
        fmt.task_size != sizeof(todolist_task_t)) {
        ESP_LOGE(TAG, "incompatible storage: v%u/%uB stored, v%u/%uB expected — dropping lists",
                 fmt.version, fmt.task_size,
                 TODOLIST_STORAGE_VERSION, (unsigned)sizeof(todolist_task_t));
        /* 以后如果需要保留用户数据，迁移分支加在这里。
         * 现在宁可丢列表也不能把脏数据当任务加载出来。 */
        fdb_kv_del(&g_kvdb, KV_TODOS);
        fdb_kv_del(&g_kvdb, KV_DONES);
        write_format_tag();
        return false;
    }
    return true;
}

static uint8_t load_list(const char *key, todolist_task_t *dst)
{
    struct fdb_blob blob;
    size_t read_len = fdb_kv_get_blob(&g_kvdb, key,
        fdb_blob_make(&blob, dst, sizeof(todolist_task_t) * TODOLIST_MAX_TASKS));

    if (read_len == 0) return 0;
    if (read_len % sizeof(todolist_task_t) != 0) {
        ESP_LOGE(TAG, "%s: %u bytes is not a whole number of tasks — dropping", key, (unsigned)read_len);
        fdb_kv_del(&g_kvdb, key);
        return 0;
    }
    size_t count = read_len / sizeof(todolist_task_t);
    if (count > TODOLIST_MAX_TASKS) count = TODOLIST_MAX_TASKS;
    return (uint8_t)count;
}

static void load_meta(TodoListModel* model)
{
    todolist_meta_t meta = {0};
    struct fdb_blob blob;
    size_t len = fdb_kv_get_blob(&g_kvdb, KV_META, fdb_blob_make(&blob, &meta, sizeof(meta)));

    if (len == sizeof(meta) && meta.magic == TODOLIST_STORAGE_MAGIC) {
        model->_id_generator  = meta.id_generator;
        model->active_task_id = meta.active_task_id;
        model->run_started_at = meta.run_started_at;
        return;
    }

    /* v0 迁移：{id_generator, active_task_id} 两个裸字段 */
    todolist_meta_v0_t v0 = {0};
    len = fdb_kv_get_blob(&g_kvdb, KV_META, fdb_blob_make(&blob, &v0, sizeof(v0)));
    if (len == sizeof(v0)) {
        ESP_LOGI(TAG, "migrating v0 meta");
        model->_id_generator  = v0.id_generator;
        model->active_task_id = v0.active_task_id;
        model->run_started_at = 0;
        write_meta(model);
        return;
    }

    ESP_LOGI(TAG, "no meta, starting fresh");
    model->_id_generator  = 1;
    model->active_task_id = 0;
    model->run_started_at = 0;
}

void todolist_storage_load_all(TodoListModel* model)
{
    if (!model) return;

    bool lists_ok = storage_format_is_compatible();

    load_meta(model);

    model->todo_task_count = lists_ok ? load_list(KV_TODOS, model->todo_tasks) : 0;
    model->done_task_count = lists_ok ? load_list(KV_DONES, model->done_tasks) : 0;

    /* id_generator 必须严格大于所有已存在的 id，否则新任务会和旧任务撞号 */
    uint32_t max_id = 0;
    for (uint8_t i = 0; i < model->todo_task_count; i++)
        if (model->todo_tasks[i].id > max_id) max_id = model->todo_tasks[i].id;
    for (uint8_t i = 0; i < model->done_task_count; i++)
        if (model->done_tasks[i].id > max_id) max_id = model->done_tasks[i].id;
    if (model->_id_generator <= max_id) {
        ESP_LOGW(TAG, "id_generator %u <= max id %u, bumping",
                 (unsigned)model->_id_generator, (unsigned)max_id);
        model->_id_generator = max_id + 1;
        todolist_storage_mark_meta_dirty();
    }

    ESP_LOGI(TAG, "loaded: next_id=%u, todo=%u, done=%u, run_started_at=%u",
             (unsigned)model->_id_generator, model->todo_task_count,
             model->done_task_count, (unsigned)model->run_started_at);
}

/* ── 工厂重置 ─────────────────────────────────────────────────────────── */

bool todolist_app_factory_reset(void) {
    struct fdb_blob blob;

    /* 别让延迟写入把旧数据盖回来 */
    s_dirty_todo = s_dirty_done = s_dirty_meta = false;
    if (s_flush_timer) lv_timer_pause(s_flush_timer);

    // 初始化配置
    todolist_config_t default_cfg = {
        .task_duration_min = 25,
        .rest_duration_min = 5,
        .pre_alert_min = 2,
    };

    fdb_err_t err = fdb_kv_set_blob(&g_kvdb, "app_cfg", fdb_blob_make(&blob, &default_cfg, sizeof(todolist_config_t)));

    write_format_tag();

    // 清空任务
    todolist_task_t tutorial_done_tasks[1];
    memset(tutorial_done_tasks, 0, sizeof(tutorial_done_tasks));

    tutorial_done_tasks[0].id = 5; // 接在 Todo 任务 ID 之后
    strncpy(tutorial_done_tasks[0].title, "click checkbox to restore task", TODOLIST_TITLE_MAX - 1);
    tutorial_done_tasks[0].estimate_s = 1500;
    tutorial_done_tasks[0].elapsed_s = 1500; // 模拟已完成 25 分钟

    fdb_kv_set_blob(&g_kvdb, KV_DONES, fdb_blob_make(&blob, tutorial_done_tasks, sizeof(tutorial_done_tasks)));

    // 3. 构建并写入教程待办任务 (Key: "td_todos")
    todolist_task_t tutorial_tasks[4];
    memset(tutorial_tasks, 0, sizeof(tutorial_tasks));

    /* 文案必须与 controller_todo.c 里的手势方向一致：左滑 = 删除，右滑 / 点击 = 开始计时 */
    const char *tutorial_titles[] = {
        "click + button to add task",
        "swipe left on task to delete",
        "swipe right or tap to start",
        "click checkbox to mark Done"
    };

    for (int i = 0; i < 4; i++) {
        tutorial_tasks[i].id = i + 1;
        strncpy(tutorial_tasks[i].title, tutorial_titles[i], TODOLIST_TITLE_MAX - 1);
        tutorial_tasks[i].title[TODOLIST_TITLE_MAX - 1] = '\0';
        tutorial_tasks[i].estimate_s = 1500;
        tutorial_tasks[i].elapsed_s = 0;
    }
    fdb_kv_set_blob(&g_kvdb, KV_TODOS, fdb_blob_make(&blob, tutorial_tasks, sizeof(tutorial_tasks)));

    // 4. 重置并写入系统元数据游标 (Key: "td_meta")
    todolist_meta_t meta = {
        .magic          = TODOLIST_STORAGE_MAGIC,
        .version        = TODOLIST_STORAGE_VERSION,
        .id_generator   = 6,   // 已经创建了 5 个任务，下一个新任务的 ID 从 6 开始
        .active_task_id = 0,   // 清除正在进行的任务状态
        .run_started_at = 0,
    };
    fdb_kv_set_blob(&g_kvdb, KV_META, fdb_blob_make(&blob, &meta, sizeof(meta)));

    /* 这里绝对不能把内存里的数组 sync 回 Flash —— 那是用旧数据盖掉刚写好的出厂数据，
     * 内存为空时还会走 fdb_kv_del() 分支把出厂数据直接删掉。
     * 方向是反的：如果 App 正在运行，把刚写入的出厂数据重新读进内存。 */
    if (g_todolist_app.model) {
        todolist_model_config_load(g_todolist_app.model);
        todolist_storage_load_all(g_todolist_app.model);
    }

    if (err != FDB_NO_ERR) {
        ESP_LOGE(TAG, "factory reset: write app_cfg failed (%d)", err);
        return false;
    }
    return true;
}
