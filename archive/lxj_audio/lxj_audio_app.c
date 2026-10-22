#include "lxj_audio_app.h"

#include "app_manager.h"
#include "audio_meta.h"
#include "audio_service.h"
#include "esp_log.h"
#include "fs_control.h"
#include "lvgl.h"
#include "sd_control.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "lxj_audio";
static lv_obj_t *s_root;
static lv_obj_t *s_track_label;
static lv_obj_t *s_state_label;
static lv_obj_t *s_volume_label;
static lv_obj_t *s_file_list;
static lv_obj_t *s_file_status;
static lv_timer_t *s_timer;

static void lxj_audio_refresh(lv_timer_t *timer);

static bool lxj_audio_file(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".wav") == 0;
}

static void lxj_audio_file_event(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target(event);
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (!label) return;

    const char *name = lv_label_get_text(label);
    char path[AUDIO_PATH_MAX] = {0};
    snprintf(path, sizeof(path), "/sdcard/music/%s", name);
    audio_service_play_single(path, name);
    lxj_audio_refresh(NULL);
}

static void lxj_audio_refresh_files(void)
{
    if (!s_file_list || !lv_obj_is_valid(s_file_list)) return;
    lv_obj_clean(s_file_list);
    if (!fs_control_is_mounted()) {
        lv_label_set_text(s_file_status, "Storage is not mounted");
        return;
    }

    file_list_t files = {0};
    if (!sd_control_get_dir_list("/sdcard/music", &files, NULL)) {
        lv_label_set_text(s_file_status, "No /sdcard/music directory");
        return;
    }

    unsigned shown = 0;
    for (uint32_t i = 0; i < files.count && shown < 8; ++i) {
        if (files.nodes[i].type != NODE_TYPE_FILE || !lxj_audio_file(files.nodes[i].name)) continue;
        lv_obj_t *button = lv_button_create(s_file_list);
        lv_obj_set_width(button, LV_PCT(100));
        lv_obj_set_height(button, 34);
        lv_obj_add_event_cb(button, lxj_audio_file_event, LV_EVENT_CLICKED, NULL);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, files.nodes[i].name);
        lv_obj_center(label);
        ++shown;
    }
    lv_label_set_text_fmt(s_file_status, "%u audio file(s)", shown);
    sd_control_free_dir_list(&files);
}

static void lxj_audio_refresh_files_event(lv_event_t *event)
{
    (void)event;
    lxj_audio_refresh_files();
}

static void lxj_audio_refresh(lv_timer_t *timer)
{
    (void)timer;
    if (!s_root || !lv_obj_is_valid(s_root)) return;

    const audio_track_t *track = audio_service_current();
    if (s_track_label && lv_obj_is_valid(s_track_label)) {
        lv_label_set_text(s_track_label, track ? track->title : "No track selected");
    }
    if (s_state_label && lv_obj_is_valid(s_state_label)) {
        const char *state = "Stopped";
        if (audio_service_state() == AUDIO_STATE_PLAYING) state = "Playing";
        else if (audio_service_state() == AUDIO_STATE_PAUSED) state = "Paused";
        lv_label_set_text_fmt(s_state_label, "%s  %lu/%lu s",
                              state,
                              (unsigned long)(audio_service_position_ms() / 1000U),
                              (unsigned long)(audio_service_duration_ms() / 1000U));
    }
    if (s_volume_label && lv_obj_is_valid(s_volume_label)) {
        lv_label_set_text_fmt(s_volume_label, "Volume %u%%", audio_service_volume());
    }
}

static void lxj_audio_button_event(lv_event_t *event)
{
    uintptr_t action = (uintptr_t)lv_event_get_user_data(event);
    switch (action) {
    case 1: audio_service_toggle(); break;
    case 2: audio_service_prev(); break;
    case 3: audio_service_next(); break;
    case 4: audio_service_stop(); break;
    case 5: audio_service_volume_down(); break;
    case 6: audio_service_volume_up(); break;
    case 7: app_manager_exit_app(); return;
    default: break;
    }
    lxj_audio_refresh(NULL);
}

static lv_obj_t *lxj_audio_button(lv_obj_t *parent, const char *text, uintptr_t action)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 104, 48);
    lv_obj_add_event_cb(button, lxj_audio_button_event, LV_EVENT_CLICKED,
                        (void *)action);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

static void lxj_audio_start(lv_obj_t *root, lv_group_t *group)
{
    (void)group;
    ESP_LOGI(TAG, "start");

    s_root = lv_obj_create(root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_root);
    lv_obj_set_style_pad_all(s_root, 24, 0);

    lv_obj_t *title = lv_label_create(s_root);
    lv_label_set_text(title, "LXJ Audio");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    s_file_status = lv_label_create(s_root);
    lv_obj_align(s_file_status, LV_ALIGN_TOP_LEFT, 12, 54);
    s_file_list = lv_obj_create(s_root);
    lv_obj_set_size(s_file_list, LV_PCT(100), 210);
    lv_obj_align(s_file_list, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_flex_flow(s_file_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_file_list, 4, 0);

    lv_obj_t *refresh_files = lv_button_create(s_root);
    lv_obj_set_size(refresh_files, 90, 40);
    lv_obj_align(refresh_files, LV_ALIGN_TOP_RIGHT, -24, 48);
    lv_obj_add_event_cb(refresh_files, lxj_audio_refresh_files_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_label = lv_label_create(refresh_files);
    lv_label_set_text(refresh_label, "Scan");
    lv_obj_center(refresh_label);

    s_track_label = lv_label_create(s_root);
    lv_obj_align(s_track_label, LV_ALIGN_CENTER, 0, 100);
    s_state_label = lv_label_create(s_root);
    lv_obj_align(s_state_label, LV_ALIGN_CENTER, 0, 128);
    s_volume_label = lv_label_create(s_root);
    lv_obj_align(s_volume_label, LV_ALIGN_CENTER, 0, 154);

    lv_obj_t *prev = lxj_audio_button(s_root, "Prev", 2);
    lv_obj_align(prev, LV_ALIGN_CENTER, -170, 68);
    lv_obj_t *play = lxj_audio_button(s_root, "Play/Pause", 1);
    lv_obj_align(play, LV_ALIGN_CENTER, -55, 68);
    lv_obj_t *next = lxj_audio_button(s_root, "Next", 3);
    lv_obj_align(next, LV_ALIGN_CENTER, 65, 68);
    lv_obj_t *stop = lxj_audio_button(s_root, "Stop", 4);
    lv_obj_align(stop, LV_ALIGN_CENTER, 175, 68);

    lv_obj_t *down = lxj_audio_button(s_root, "Vol -", 5);
    lv_obj_align(down, LV_ALIGN_BOTTOM_LEFT, 24, -18);
    lv_obj_t *up = lxj_audio_button(s_root, "Vol +", 6);
    lv_obj_align(up, LV_ALIGN_BOTTOM_LEFT, 136, -18);
    lv_obj_t *close = lxj_audio_button(s_root, "Close", 7);
    lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, -24, -18);

    lxj_audio_refresh_files();
    lxj_audio_refresh(NULL);
    s_timer = lv_timer_create(lxj_audio_refresh, 1000, NULL);
}

static void lxj_audio_stop(void)
{
    ESP_LOGI(TAG, "stop");
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_root && lv_obj_is_valid(s_root)) lv_obj_delete(s_root);
    s_root = NULL;
    s_track_label = NULL;
    s_state_label = NULL;
    s_volume_label = NULL;
    s_file_list = NULL;
    s_file_status = NULL;
}

static bool lxj_audio_back(void)
{
    return false;
}

static application_t s_lxj_audio_application = {
    .name = "LXJ Audio",
    .icon = NULL,
    .start_func = lxj_audio_start,
    .stop_func = lxj_audio_stop,
    .back_func = lxj_audio_back,
    .category = APP_CATEGORY_SYSTEM,
};

void lxj_audio_init(void)
{
    app_manager_add_application(&s_lxj_audio_application);
    ESP_LOGI(TAG, "registered");
}
