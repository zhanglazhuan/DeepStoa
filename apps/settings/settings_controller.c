// apps/settings/settings_controller.c
// Settings controller — event handlers (Phase 1)
// Ported from D:\Codes\EPOS\epos\apps\settings\controller.c

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <lvgl.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_sleep.h"
#include <flashdb.h>

#include "settings_controller.h"
#include "settings_app.h"
#include "settings_model.h"
#include "time_service.h"
#include "flash_control.h"
#include "controller_wifi.h"

static const char *TAG = "settings_ctrl";

// ── Timezone table ─────────────────────────────────────────────────────

const epos_timezone_map_t epos_timezones[] = {
    {"(UTC+12:00) Auckland, Wellington, Fiji",              "UTC-12"},
    {"(UTC+11:00) Solomon Islands, New Caledonia",           "UTC-11"},
    {"(UTC+10:00) Sydney, Melbourne, Guam, Brisbane",        "UTC-10"},
    {"(UTC+09:00) Tokyo, Seoul, Osaka, Sapporo",             "UTC-9"},
    {"(UTC+08:00) Beijing, Singapore, Kuala Lumpur, Perth",  "UTC-8"},
    {"(UTC+07:00) Bangkok, Hanoi, Jakarta",                  "UTC-7"},
    {"(UTC+06:00) Almaty, Dhaka",                            "UTC-6"},
    {"(UTC+05:00) Islamabad, Karachi, Tashkent",             "UTC-5"},
    {"(UTC+04:00) Abu Dhabi, Muscat, Tbilisi",               "UTC-4"},
    {"(UTC+03:00) Moscow, St. Petersburg, Kuwait, Nairobi",  "UTC-3"},
    {"(UTC+02:00) Athens, Istanbul, Jerusalem, Cairo",       "UTC-2"},
    {"(UTC+01:00) Berlin, Rome, Paris, Madrid, Warsaw",      "UTC-1"},
    {"(UTC+00:00) London, Dublin, Lisbon, Casablanca",       "UTC0"},
    {"(UTC-01:00) Azores, Cape Verde",                       "UTC+1"},
    {"(UTC-02:00) Mid-Atlantic",                             "UTC+2"},
    {"(UTC-03:00) Brasilia, Buenos Aires, Greenland",        "UTC+3"},
    {"(UTC-04:00) Atlantic Time (Canada), Caracas",          "UTC+4"},
    {"(UTC-05:00) Eastern Time (US & Canada), Bogota",       "UTC+5"},
    {"(UTC-06:00) Central Time (US & Canada), Mexico City",  "UTC+6"},
    {"(UTC-07:00) Mountain Time (US & Canada), Chihuahua",   "UTC+7"},
    {"(UTC-08:00) Pacific Time (US & Canada), Tijuana",      "UTC+8"},
    {"(UTC-09:00) Alaska",                                   "UTC+9"},
    {"(UTC-10:00) Hawaii",                                   "UTC+10"},
    {"(UTC-11:00) Midway Island, Samoa",                     "UTC+11"},
    {"(UTC-12:00) International Date Line West",             "UTC+12"},
    {"(UTC-13:00) ",                                          "UTC+13"},
    {"(UTC-14:00) Line Islands",                             "UTC+14"},
};

const uint16_t epos_timezones_count =
    sizeof(epos_timezones) / sizeof(epos_timezones[0]);

// ── Lifecycle ──────────────────────────────────────────────────────────

void settings_controller_init(struct SettingsApp *app)
{
    app->controller = (SettingsController *)malloc(sizeof(SettingsController));
    memset(app->controller, 0, sizeof(SettingsController));

    /* Wi-Fi 的扫描/连接是阻塞的，交给一个独立 worker 任务跑，
     * 结果再 lv_async_call 弹回 LVGL 线程 —— 见 controller_wifi.h */
    settings_controller_wifi_init(app);
}

void settings_controller_deinit(struct SettingsApp *app)
{
    settings_controller_wifi_deinit();

    if (app->controller) {
        free(app->controller);
        app->controller = NULL;
    }
}

// ── System actions ─────────────────────────────────────────────────────

void settings_controller_reboot(lv_event_t *e)
{
    (void)e;
    ESP_LOGW(TAG, "reboot requested");
    esp_restart();
}

/* 目前**没有**任何 UI 绑到这个函数上，这是有意的 —— 别看它还在就直接挂回按钮。
 *
 * 这块硬件没有真正的关机，esp_deep_sleep_start() 只是深睡：
 *   1. 调用前没有配置任何唤醒源（全工程 grep 不到 esp_sleep_enable_*），
 *      所以睡下去就只能靠物理复位/断电把设备弄回来 —— 对用户是变砖。
 *   2. 墨水屏会保留最后一帧，"关机"后屏幕还停在 Settings 页面，
 *      没有任何反馈，看上去更像死机而不是关机。
 *   3. 深睡仍然耗电，长期放着电池照样会空。
 *
 * 真要做关机，顺序是：先接上唤醒源（电源键 ext0/ext1 或 RTC 定时），
 * 睡前画一屏 "Powered off — press power to wake"，再把 system/sleep/
 * 那套 sleep_monitor 接进构建（它现在不在根 CMakeLists 里，是死代码）。
 * 三件事齐了再放按钮。 */
void settings_controller_shutdown(lv_event_t *e)
{
    (void)e;
    ESP_LOGW(TAG, "shutdown: no wake source configured, entering deep sleep");
    esp_deep_sleep_start();
}

void settings_controller_factory_reset(struct SettingsApp *app)
{
    (void)app;
    /* 只擦用户区，厂家区（机器码等）保留。实现见 flash_control.c，
     * 函数内部会重启，不返回。 */
    flash_control_factory_reset();
}

// ── Wallpaper handlers ─────────────────────────────────────────────────

void settings_controller_set_wallpaper_lock_screen(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    char sel[32];
    lv_dropdown_get_selected_str(lv_event_get_target(e), sel, sizeof(sel));
    strncpy(app->model->wallpaper.lock_screen_type, sel,
            sizeof(app->model->wallpaper.lock_screen_type) - 1);
}

/* 原来这里是个 lv_event_cb，从只读 textarea 里取文本 —— 那个 textarea 是
 * Phase 1 的占位符，用户根本填不进去。现在改成由文件夹选择器回调直接传路径。 */
void settings_controller_set_wallpaper_folder(struct SettingsApp *app, const char *path)
{
    if (!app || !app->model || !path) return;

    snprintf(app->model->wallpaper.wallpaper_folder,
             sizeof(app->model->wallpaper.wallpaper_folder), "%s", path);

    /* 立刻落盘。不能指望 settings_controller_on_general_exit —— 它绑在
     * General 页的 LV_EVENT_DELETE 上，而进 Wallpaper 页时 General 页就已经
     * 被销毁了，等于在用户改动之前就存过一次，改完反而存不到。 */
    settings_model_save_general(app);
    ESP_LOGI(TAG, "wallpaper folder set: %s", path);
}

void settings_controller_set_wallpaper_frequency(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    char sel[32];
    lv_dropdown_get_selected_str(lv_event_get_target(e), sel, sizeof(sel));
    strncpy(app->model->wallpaper.frequency, sel,
            sizeof(app->model->wallpaper.frequency) - 1);
}

void settings_controller_set_wallpaper_order(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    char sel[32];
    lv_dropdown_get_selected_str(lv_event_get_target(e), sel, sizeof(sel));
    strncpy(app->model->wallpaper.order, sel,
            sizeof(app->model->wallpaper.order) - 1);
}

// ── Datetime handlers ──────────────────────────────────────────────────

void settings_controller_set_date_type(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    char sel[32];
    lv_dropdown_get_selected_str(lv_event_get_target(e), sel, sizeof(sel));
    strncpy(app->model->datetime.date_type, sel,
            sizeof(app->model->datetime.date_type) - 1);
}

void settings_controller_set_hour_24(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    lv_obj_t *sw = lv_event_get_target(e);
    app->model->datetime.hour_24_enabled =
        lv_obj_has_state(sw, LV_STATE_CHECKED);
    /* 时间格式由 system/timeservice 持久化并广播，状态栏跟着它变 */
    time_service_set_format_24h(app->model->datetime.hour_24_enabled != 0);
}

void settings_controller_set_timezone(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    uint16_t idx = lv_dropdown_get_selected(lv_event_get_target(e));
    if (idx < epos_timezones_count) {
        strncpy(app->model->datetime.timezone,
                epos_timezones[idx].ui_label,
                sizeof(app->model->datetime.timezone) - 1);
        app->model->datetime.timezone[sizeof(app->model->datetime.timezone) - 1] = '\0';
        /* 交给时间服务：它负责 setenv+tzset、落盘、并广播一次 tick。
         * 这里自己 setenv 的话，重启后就丢了 —— 时区必须在 app 启动之前生效。 */
        time_service_set_timezone_posix(epos_timezones[idx].posix_str);
        ESP_LOGI(TAG, "Timezone selected: %s", epos_timezones[idx].posix_str);
    }
}

void settings_controller_on_general_exit(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    settings_model_save_general(app);
}

// ── Battery handlers ───────────────────────────────────────────────────

void settings_controller_set_auto_update(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    app->model->update.auto_check_enabled =
        lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    /* 这一页没有统一的退出保存点，改完立刻落盘 */
    settings_model_save_update(app);
}

void settings_controller_set_battery_percentage(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    app->model->battery.battery_percentage_enabled =
        lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
}

void settings_controller_set_auto_sleep(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    uint16_t selected = lv_dropdown_get_selected(lv_event_get_target(e));
    app->model->battery.auto_sleep_minutes =
        (selected == 0) ? 5 : ((selected == 1) ? 15 : 30);
}

void settings_controller_on_battery_exit(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    settings_model_save_battery(app);
}
