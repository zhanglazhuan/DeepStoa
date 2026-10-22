#include "lxj_clock_app.h"

#include "app_manager.h"
#include "esp_log.h"
#include "lvgl.h"
#include "time_service.h"

static const char *TAG = "lxj_clock";
static lv_obj_t *s_root;
static lv_obj_t *s_time_label;
static lv_obj_t *s_sync_label;
static lv_timer_t *s_timer;

static void lxj_clock_refresh(lv_timer_t *timer)
{
    (void)timer;
    if (!s_time_label || !lv_obj_is_valid(s_time_label)) return;

    lv_label_set_text_fmt(s_time_label, "%02d:%02d:%02d",
                          time_service_get_hour(),
                          time_service_get_minute(),
                          time_service_get_second());
    if (s_sync_label && lv_obj_is_valid(s_sync_label)) {
        lv_label_set_text(s_sync_label,
                          time_service_is_synced() ? "SNTP synced" : "RTC / local time");
    }
}

static void lxj_clock_close_event(lv_event_t *event)
{
    (void)event;
    app_manager_exit_app();
}

static void lxj_clock_start(lv_obj_t *root, lv_group_t *group)
{
    (void)group;
    ESP_LOGI(TAG, "start");

    s_root = lv_obj_create(root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_root);
    lv_obj_set_style_pad_all(s_root, 24, 0);

    lv_obj_t *title = lv_label_create(s_root);
    lv_label_set_text(title, "LXJ Clock");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    s_time_label = lv_label_create(s_root);
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, -20);

    s_sync_label = lv_label_create(s_root);
    lv_obj_align(s_sync_label, LV_ALIGN_CENTER, 0, 20);

    lv_obj_t *close_button = lv_button_create(s_root);
    lv_obj_set_size(close_button, 140, 52);
    lv_obj_align(close_button, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_add_event_cb(close_button, lxj_clock_close_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_label = lv_label_create(close_button);
    lv_label_set_text(close_label, "Close");
    lv_obj_center(close_label);

    lxj_clock_refresh(NULL);
    s_timer = lv_timer_create(lxj_clock_refresh, 1000, NULL);
}

static void lxj_clock_stop(void)
{
    ESP_LOGI(TAG, "stop");
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_root && lv_obj_is_valid(s_root)) lv_obj_delete(s_root);
    s_root = NULL;
    s_time_label = NULL;
    s_sync_label = NULL;
}

static bool lxj_clock_back(void)
{
    return false;
}

static application_t s_lxj_clock_application = {
    .name = "LXJ Clock",
    .icon = NULL,
    .start_func = lxj_clock_start,
    .stop_func = lxj_clock_stop,
    .back_func = lxj_clock_back,
    .category = APP_CATEGORY_TOOLS,
};

void lxj_clock_init(void)
{
    app_manager_add_application(&s_lxj_clock_application);
    ESP_LOGI(TAG, "registered");
}
