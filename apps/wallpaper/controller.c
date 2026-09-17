#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include "esp_log.h"

#include "controller.h"

static const char *TAG = "wallpaper_controller";

static void monitor_timer_cb(lv_timer_t *timer);
static void slide_timer_cb(lv_timer_t *timer);
static void on_screen_touched_cb(lv_event_t *e);
static bool get_next_image_path(WallpaperController *ctrl, char *out_path, size_t max_len);
static bool get_next_text_line(WallpaperController *ctrl, char *out_buf, size_t max_len);
static bool get_next_text_line_(WallpaperController *ctrl, char *out_buf, size_t max_len);

void wallpaper_controller_init(WallpaperController *ctrl, WallpaperModel *model, WallpaperView *view)
{
    ctrl->model = model;
    ctrl->view = view;

    ctrl->monitor_timer = lv_timer_create(monitor_timer_cb, 30000, ctrl);

    ctrl->slide_timer = lv_timer_create(slide_timer_cb,
                                        model->config.slide_interval_s * 1000, ctrl);
    lv_timer_pause(ctrl->slide_timer);
}

static void monitor_timer_cb(lv_timer_t *timer)
{
    WallpaperController *ctrl = lv_timer_get_user_data(timer);
    WallpaperModel *model = ctrl->model;

    uint32_t inactive_ms = lv_display_get_inactive_time(NULL);

    if (!model->is_showing && (inactive_ms >= model->config.idle_timeout_s * 1000)) {
        ESP_LOGI(TAG, "System idle, triggering wallpaper...");
        model->is_showing = true;

        wallpaper_view_show(ctrl->view, on_screen_touched_cb, ctrl);

        slide_timer_cb(ctrl->slide_timer);

        lv_timer_set_period(ctrl->slide_timer, model->config.slide_interval_s * 1000);
        lv_timer_resume(ctrl->slide_timer);
    }
}

static void slide_timer_cb(lv_timer_t *timer)
{
    WallpaperController *ctrl = lv_timer_get_user_data(timer);

    if (ctrl->model->config.mode == WALLPAPER_MODE_IMAGE) {
        char next_img_path[WALLPAPER_PATH_MAX + 32];
        if (get_next_image_path(ctrl, next_img_path, sizeof(next_img_path))) {
            wallpaper_view_set_image(ctrl->view, next_img_path);
        } else {
            ESP_LOGW(TAG, "No valid images found.");
        }
    } else if (ctrl->model->config.mode == WALLPAPER_MODE_TEXT) {
        char text_line[MAX_TEXT_LINE_LEN];
        if (get_next_text_line_(ctrl, text_line, sizeof(text_line))) {
            wallpaper_view_set_text(ctrl->view, text_line);
        } else {
            ESP_LOGW(TAG, "Could not read text from %s",
                     ctrl->model->config.text_file_path);
        }
    }
}

static bool get_next_text_line(WallpaperController *ctrl, char *out_buf, size_t max_len)
{
    FILE *file = fopen(ctrl->model->config.text_file_path, "r");
    if (!file) return false;

    bool found_line = false;

    while (!found_line) {
        fseek(file, ctrl->model->current_text_offset, SEEK_SET);

        size_t bytes_read = fread(out_buf, 1, max_len - 1, file);

        if (bytes_read == 0) {
            if (ctrl->model->current_text_offset == 0) break;
            ctrl->model->current_text_offset = 0;
            continue;
        }

        size_t line_len = 0;
        for (size_t i = 0; i < bytes_read; i++) {
            if (out_buf[i] == '\n') { line_len = i; break; }
            line_len = i + 1;
        }

        ctrl->model->current_text_offset += line_len;
        if (line_len < bytes_read && out_buf[line_len] == '\n')
            ctrl->model->current_text_offset += 1;

        if (line_len > 0 && out_buf[line_len - 1] == '\r')
            out_buf[line_len - 1] = '\0';
        else
            out_buf[line_len] = '\0';

        if (strlen(out_buf) > 0) found_line = true;
    }

    fclose(file);
    return found_line;
}

static bool get_next_text_line_(WallpaperController *ctrl, char *out_buf, size_t max_len)
{
    static int i = 0;
    if (i++ % 2 == 0) {
        strncpy(out_buf,
                "For God so loved the world, that he gave his only begotten Son, that whosoever believeth in him should not perish, but have everlasting life.",
                max_len - 1);
    } else {
        strncpy(out_buf,
                "Therefore all things whatsoever ye would that men should do to you, do ye even so to them: for this is the law and the prophets.",
                max_len - 1);
    }
    out_buf[max_len - 1] = '\0';
    return true;
}

static void on_screen_touched_cb(lv_event_t *e)
{
    WallpaperController *ctrl = lv_event_get_user_data(e);

    ESP_LOGI(TAG, "User touched screen, hiding wallpaper...");
    ctrl->model->is_showing = false;

    lv_timer_pause(ctrl->slide_timer);
    wallpaper_view_hide(ctrl->view);

    lv_display_trigger_activity(NULL);
}

static bool get_next_image_path(WallpaperController *ctrl, char *out_path, size_t max_len)
{
    DIR *dir = opendir(ctrl->model->config.img_folder_path);
    if (!dir) return false;

    uint32_t count = 0;
    bool found = false;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            if (strstr(entry->d_name, ".png") || strstr(entry->d_name, ".bin")) {
                if (count == ctrl->model->current_file_index) {
                    snprintf(out_path, max_len, "%s/%s",
                             ctrl->model->config.img_folder_path, entry->d_name);
                    found = true;
                    break;
                }
                count++;
            }
        }
    }

    closedir(dir);

    if (found) {
        ctrl->model->current_file_index++;
    } else if (ctrl->model->current_file_index > 0) {
        ctrl->model->current_file_index = 0;
        return get_next_image_path(ctrl, out_path, max_len);
    }

    return found;
}
