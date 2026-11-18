#ifndef TODOLIST_CONTROLLER_SETTINGS_H
#define TODOLIST_CONTROLLER_SETTINGS_H

#include "lvgl.h"
#include "../controller.h"

void view_open_settings(void);
void view_close_settings(void);
void task_duration_change_cb(lv_event_t * e);
void rest_duration_change_cb(lv_event_t * e);
void pre_alert_change_cb(lv_event_t * e);

/* 设置页销毁时调用（绑 LV_EVENT_DELETE）：把改动一次性写进 Flash */
void settings_flush_config(lv_event_t * e);
/* 进入设置页时调用：丢弃上一次遗留的 dirty 标记 */
void settings_mark_config_clean(void);

#endif // TODOLIST_CONTROLLER_SETTINGS_H
