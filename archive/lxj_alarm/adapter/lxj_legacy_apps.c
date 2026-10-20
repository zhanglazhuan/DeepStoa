#include "lxj_legacy_apps.h"

#include <string.h>
#include <stdint.h>

#include "app_manager.h"
#include "alarm_service.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "lxj_apps";
static lv_obj_t *s_root;
static lv_obj_t *s_alarm_list;
static void lxj_legacy_close(lv_event_t *event);
static void lxj_legacy_stop(void);
static bool lxj_legacy_back(void);
static void lxj_alarm_toggle(lv_event_t *event);
static void lxj_alarm_refresh(void)
{
    if (!s_alarm_list || !lv_obj_is_valid(s_alarm_list)) return;
    lv_obj_clean(s_alarm_list);
    uint8_t count = alarm_service_count();
    if (count == 0U) {
        lv_obj_t *empty = lv_label_create(s_alarm_list);
        lv_label_set_text(empty, "No alarms configured");
        return;
    }
    for (uint8_t i = 0; i < count; ++i) {
        const alarm_t *alarm = alarm_service_get(i);
        if (!alarm) continue;
        lv_obj_t *row = lv_button_create(s_alarm_list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 48);
        lv_obj_add_event_cb(row, lxj_alarm_toggle, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text_fmt(label, "%02u:%02u  %s",
                              (unsigned)alarm->hour, (unsigned)alarm->minute,
                              alarm->enabled ? "ON" : "OFF");
        lv_obj_center(label);
    }
}

static void lxj_alarm_toggle(lv_event_t *event)
{
    uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    alarm_service_toggle(index);
    lxj_alarm_refresh();
}

static void lxj_alarm_start(lv_obj_t *root, lv_group_t *group)
{
    (void)group;
    s_root = lv_obj_create(root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_root);
    lv_obj_set_style_pad_all(s_root, 20, 0);
    lv_obj_t *heading = lv_label_create(s_root);
    lv_label_set_text(heading, "LXJ Alarm");
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 10);
    s_alarm_list = lv_obj_create(s_root);
    lv_obj_set_size(s_alarm_list, LV_PCT(100), 230);
    lv_obj_align(s_alarm_list, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_flex_flow(s_alarm_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_alarm_list, 6, 0);
    lxj_alarm_refresh();

    lv_obj_t *close = lv_button_create(s_root);
    lv_obj_set_size(close, 130, 48);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(close, lxj_legacy_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close);
    lv_label_set_text(close_label, "Close");
    lv_obj_center(close_label);
}

static application_t s_lxj_alarm_application = {
    .name = "LXJ Alarm",
    .icon = NULL,
    .start_func = lxj_alarm_start,
    .stop_func = lxj_legacy_stop,
    .back_func = lxj_legacy_back,
    .category = APP_CATEGORY_TOOLS,
};

void lxj_alarm_init(void)
{
    app_manager_add_application(&s_lxj_alarm_application);
    ESP_LOGI(TAG, "registered LXJ Alarm");
}

static void lxj_legacy_close(lv_event_t *event)
{
    (void)event;
    app_manager_exit_app();
}

static void lxj_legacy_stop(void)
{
    s_alarm_list = NULL;
    if (s_root && lv_obj_is_valid(s_root)) lv_obj_delete(s_root);
    s_root = NULL;
}

static bool lxj_legacy_back(void)
{
    return false;
}
