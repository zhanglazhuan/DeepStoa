#include <stdint.h>
#include <stdlib.h>
#include "esp_log.h"
#include "controller_settings.h"
#include "../view.h"
#include "../model.h"
#include "../app.h"

static const char *TAG = "todolist_controller_settings";

/* 配置是否被改过 —— 只在真的改过时才写 Flash */
static bool s_config_dirty;

// 任务时长变化回调
void task_duration_change_cb(lv_event_t * e) {
    // 直接从 param 中恢复出 int 值
    int new_val = (int)(intptr_t)lv_event_get_param(e);

    todolist_config_t * config = todolist_model_config_get();
    if (config && config->task_duration_min != (uint32_t)new_val) {
        config->task_duration_min = (uint32_t)new_val;
        s_config_dirty = true;
    }
}

// 休息时长变化回调
void rest_duration_change_cb(lv_event_t * e) {
    int new_val = (int)(intptr_t)lv_event_get_param(e);

    todolist_config_t * config = todolist_model_config_get();
    if (config && config->rest_duration_min != (uint32_t)new_val) {
        config->rest_duration_min = (uint32_t)new_val;
        s_config_dirty = true;
    }
}

// 提前提醒时长变化回调
void pre_alert_change_cb(lv_event_t * e) {
    int new_val = (int)(intptr_t)lv_event_get_param(e);

    todolist_config_t * config = todolist_model_config_get();
    if (config && config->pre_alert_min != (uint32_t)new_val) {
        config->pre_alert_min = (uint32_t)new_val;
        s_config_dirty = true;
    }
}

void settings_mark_config_clean(void) {
    s_config_dirty = false;
}

/* 离开设置页时统一落盘。
 * 不在每次 VALUE_CHANGED 里写：步进是 1，改一次时长要点二十几下，
 * 那就是二十几次 Flash 写入，既磨损又会卡住墨水屏刷新。 */
void settings_flush_config(lv_event_t * e) {
    (void)e;
    if (!s_config_dirty) return;

    todolist_model_config_save(g_todolist_app.model);
    s_config_dirty = false;
    ESP_LOGI(TAG, "config flushed on settings page leave");
}
