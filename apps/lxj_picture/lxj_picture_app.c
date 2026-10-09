#include "lxj_picture_app.h"

#include "app_manager.h"
#include "esp_log.h"
#include "fs_control.h"
#include "lvgl.h"
#include "sd_control.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "lxj_picture";
static lv_obj_t *s_root;
static lv_obj_t *s_list;
static lv_obj_t *s_status;

static bool lxj_picture_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return strcasecmp(dot, ".bmp") == 0 || strcasecmp(dot, ".jpg") == 0 ||
           strcasecmp(dot, ".jpeg") == 0 || strcasecmp(dot, ".png") == 0 ||
           strcasecmp(dot, ".gif") == 0 || strcasecmp(dot, ".webp") == 0;
}

static void lxj_picture_refresh(void)
{
    if (!s_list || !lv_obj_is_valid(s_list)) return;
    lv_obj_clean(s_list);
    if (!fs_control_is_mounted()) {
        lv_label_set_text(s_status, "Storage is not mounted");
        return;
    }

    file_list_t files = {0};
    if (!sd_control_get_dir_list("/sdcard/picture", &files, NULL)) {
        sd_control_create_dir("/sdcard/picture");
        lv_label_set_text(s_status, "No picture directory");
        return;
    }

    unsigned shown = 0;
    for (uint32_t i = 0; i < files.count && shown < 16; ++i) {
        if (files.nodes[i].type != NODE_TYPE_FILE ||
            !lxj_picture_extension(files.nodes[i].name)) continue;
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 34);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text(label, files.nodes[i].name);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);
        ++shown;
    }
    if (shown == 0) lv_label_set_text(s_status, "No picture files in /sdcard/picture");
    else lv_label_set_text_fmt(s_status, "%u picture file(s)", shown);
    sd_control_free_dir_list(&files);
}

static void lxj_picture_refresh_event(lv_event_t *event)
{
    (void)event;
    lxj_picture_refresh();
}

static void lxj_picture_close_event(lv_event_t *event)
{
    (void)event;
    app_manager_exit_app();
}

static void lxj_picture_start(lv_obj_t *root, lv_group_t *group)
{
    (void)group;
    ESP_LOGI(TAG, "start");
    s_root = lv_obj_create(root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_root);
    lv_obj_set_style_pad_all(s_root, 24, 0);

    lv_obj_t *title = lv_label_create(s_root);
    lv_label_set_text(title, "LXJ Picture");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);
    s_status = lv_label_create(s_root);
    lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 12, 58);

    s_list = lv_obj_create(s_root);
    lv_obj_set_size(s_list, LV_PCT(100), 350);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 88);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 4, 0);

    lv_obj_t *refresh = lv_button_create(s_root);
    lv_obj_set_size(refresh, 140, 48);
    lv_obj_align(refresh, LV_ALIGN_BOTTOM_LEFT, 24, -18);
    lv_obj_add_event_cb(refresh, lxj_picture_refresh_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_label = lv_label_create(refresh);
    lv_label_set_text(refresh_label, "Refresh");
    lv_obj_center(refresh_label);

    lv_obj_t *close_button = lv_button_create(s_root);
    lv_obj_set_size(close_button, 140, 48);
    lv_obj_align(close_button, LV_ALIGN_BOTTOM_RIGHT, -24, -18);
    lv_obj_add_event_cb(close_button, lxj_picture_close_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close_button);
    lv_label_set_text(close_label, "Close");
    lv_obj_center(close_label);
    lxj_picture_refresh();
}

static void lxj_picture_stop(void)
{
    ESP_LOGI(TAG, "stop");
    if (s_root && lv_obj_is_valid(s_root)) lv_obj_delete(s_root);
    s_root = NULL;
    s_list = NULL;
    s_status = NULL;
}

static bool lxj_picture_back(void)
{
    return false;
}

static application_t s_lxj_picture_application = {
    .name = "LXJ Picture",
    .icon = NULL,
    .start_func = lxj_picture_start,
    .stop_func = lxj_picture_stop,
    .back_func = lxj_picture_back,
    .category = APP_CATEGORY_TOOLS,
};

void lxj_picture_init(void)
{
    app_manager_add_application(&s_lxj_picture_application);
    ESP_LOGI(TAG, "registered");
}
