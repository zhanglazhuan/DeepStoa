#include "lxj_todolist_app.h"

#include "app_manager.h"
#include "esp_log.h"
#include "fs_control.h"
#include "lvgl.h"
#include "sd_control.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "lxj_todolist";
static lv_obj_t *s_root;
static lv_obj_t *s_list;
static lv_obj_t *s_status;

#define LXJ_TODO_MAX 12
typedef struct {
    char content[160];
    bool completed;
} lxj_todo_item_t;

static lxj_todo_item_t s_items[LXJ_TODO_MAX];
static unsigned s_item_count;
static void lxj_todolist_refresh(void);

static bool lxj_todolist_load_file(const char *path, bool legacy)
{
    FILE *file = fopen(path, "r");
    if (!file) return false;

    s_item_count = 0;
    char line[384];
    while (s_item_count < LXJ_TODO_MAX && fgets(line, sizeof(line), file)) {
        char *save = NULL;
        char *field = strtok_r(line, ",\r\n", &save);
        if (!field) continue;
        char *content = strtok_r(NULL, ",\r\n", &save);
        if (!content || !content[0]) continue;

        lxj_todo_item_t *item = &s_items[s_item_count];
        snprintf(item->content, sizeof(item->content), "%s", content);
        item->completed = false;
        if (legacy) {
            for (int index = 2; index <= 14; ++index) {
                field = strtok_r(NULL, ",\r\n", &save);
                if (!field) break;
                if (index == 14) item->completed = atoi(field) != 0;
            }
        } else {
            item->completed = atoi(field) != 0;
        }
        ++s_item_count;
    }
    fclose(file);
    return true;
}

static void lxj_todolist_load(void)
{
    if (lxj_todolist_load_file("/sdcard/lxj_todolist/todolist.csv", false)) return;
    lxj_todolist_load_file("/sdcard/todolist/todolist.csv", true);
}

static void lxj_todolist_save(void)
{
    sd_control_create_dir("/sdcard/lxj_todolist");
    FILE *file = fopen("/sdcard/lxj_todolist/todolist.csv", "w");
    if (!file) return;
    for (unsigned i = 0; i < s_item_count; ++i) {
        fprintf(file, "%d,%s\n", s_items[i].completed ? 1 : 0, s_items[i].content);
    }
    fclose(file);
}

static void lxj_todolist_toggle_event(lv_event_t *event)
{
    uintptr_t index = (uintptr_t)lv_event_get_user_data(event);
    if (index >= s_item_count) return;
    s_items[index].completed = !s_items[index].completed;
    lxj_todolist_save();
    lxj_todolist_refresh();
}

static void lxj_todolist_refresh(void)
{
    if (!s_list || !lv_obj_is_valid(s_list)) return;
    lv_obj_clean(s_list);
    if (!fs_control_is_mounted()) {
        lv_label_set_text(s_status, "Storage is not mounted");
        return;
    }

    lxj_todolist_load();
    if (s_item_count == 0) {
        lv_label_set_text(s_status, "No legacy todo CSV found");
        return;
    }
    for (unsigned i = 0; i < s_item_count; ++i) {
        lxj_todo_item_t *item = &s_items[i];
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 38);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, lxj_todolist_toggle_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text_fmt(label, "%s%s", item->completed ? "[x] " : "[ ] ", item->content);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);
    }
    lv_label_set_text_fmt(s_status, "%u LXJ task(s), click to complete", s_item_count);
}

static void lxj_todolist_refresh_event(lv_event_t *event)
{
    (void)event;
    lxj_todolist_refresh();
}

static void lxj_todolist_close_event(lv_event_t *event)
{
    (void)event;
    app_manager_exit_app();
}

static void lxj_todolist_start(lv_obj_t *root, lv_group_t *group)
{
    (void)group;
    ESP_LOGI(TAG, "start");

    s_root = lv_obj_create(root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_root);
    lv_obj_set_style_pad_all(s_root, 24, 0);

    lv_obj_t *title = lv_label_create(s_root);
    lv_label_set_text(title, "LXJ TodoList");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    s_status = lv_label_create(s_root);
    lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 12, 56);
    s_list = lv_obj_create(s_root);
    lv_obj_set_size(s_list, LV_PCT(100), 390);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 4, 0);

    lv_obj_t *refresh = lv_button_create(s_root);
    lv_obj_set_size(refresh, 140, 48);
    lv_obj_align(refresh, LV_ALIGN_BOTTOM_LEFT, 24, -18);
    lv_obj_add_event_cb(refresh, lxj_todolist_refresh_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_label = lv_label_create(refresh);
    lv_label_set_text(refresh_label, "Refresh");
    lv_obj_center(refresh_label);

    lv_obj_t *close_button = lv_button_create(s_root);
    lv_obj_set_size(close_button, 140, 52);
    lv_obj_align(close_button, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_add_event_cb(close_button, lxj_todolist_close_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_label = lv_label_create(close_button);
    lv_label_set_text(close_label, "Close");
    lv_obj_center(close_label);
    lxj_todolist_refresh();
}

static void lxj_todolist_stop(void)
{
    ESP_LOGI(TAG, "stop");
    if (s_root && lv_obj_is_valid(s_root)) lv_obj_delete(s_root);
    s_root = NULL;
    s_list = NULL;
    s_status = NULL;
}

static bool lxj_todolist_back(void)
{
    return false;
}

static application_t s_lxj_todolist_application = {
    .name = "LXJ TodoList",
    .icon = NULL,
    .start_func = lxj_todolist_start,
    .stop_func = lxj_todolist_stop,
    .back_func = lxj_todolist_back,
    .category = APP_CATEGORY_SYSTEM,
};

void lxj_todolist_init(void)
{
    app_manager_add_application(&s_lxj_todolist_application);
    ESP_LOGI(TAG, "registered");
}
