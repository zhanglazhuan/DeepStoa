// apps/settings/settings_controller.h
// Settings controller — event handlers + business logic (Phase 1)

#ifndef SETTINGS_CONTROLLER_H
#define SETTINGS_CONTROLLER_H

#include <lvgl.h>

struct SettingsApp;

typedef struct SettingsController {
    struct SettingsModel *model;
    struct SettingsView  *view;
} SettingsController;

// ── Timezone map ───────────────────────────────────────────────────────
typedef struct {
    const char *ui_label;
    const char *posix_str;
} epos_timezone_map_t;

extern const epos_timezone_map_t epos_timezones[];
extern const uint16_t epos_timezones_count;

// ── Lifecycle ──────────────────────────────────────────────────────────
void settings_controller_init(struct SettingsApp *app);
void settings_controller_deinit(struct SettingsApp *app);

// ── System actions ─────────────────────────────────────────────────────
void settings_controller_reboot(lv_event_t *e);
void settings_controller_shutdown(lv_event_t *e);
void settings_controller_factory_reset(struct SettingsApp *app);

// ── Wallpaper handlers ─────────────────────────────────────────────────
void settings_controller_set_wallpaper_lock_screen(lv_event_t *e);
/** 设置壁纸图片目录并立刻落盘。由文件夹选择器的回调调用。 */
void settings_controller_set_wallpaper_folder(struct SettingsApp *app, const char *path);
void settings_controller_set_wallpaper_frequency(lv_event_t *e);
void settings_controller_set_wallpaper_order(lv_event_t *e);

// ── Datetime handlers ──────────────────────────────────────────────────
void settings_controller_set_date_type(lv_event_t *e);
void settings_controller_set_hour_24(lv_event_t *e);
void settings_controller_set_timezone(lv_event_t *e);
void settings_controller_on_general_exit(lv_event_t *e);

// ── Update (OTA) handlers ──────────────────────────────────────────────
void settings_controller_set_auto_update(lv_event_t *e);

// ── Battery handlers ───────────────────────────────────────────────────
void settings_controller_set_battery_percentage(lv_event_t *e);
void settings_controller_set_auto_sleep(lv_event_t *e);
void settings_controller_on_battery_exit(lv_event_t *e);

#endif
