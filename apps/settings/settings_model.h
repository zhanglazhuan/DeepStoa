// apps/settings/settings_model.h
// Settings data model — Phase 1 (battery/storage/security/wallpaper/datetime)
// Ported from D:\Codes\EPOS\epos\apps\settings\model.h

#ifndef SETTINGS_MODEL_H
#define SETTINGS_MODEL_H

#include <stdint.h>
#include <stdbool.h>

#include "wifi_manager.h"   /* wifi_info_t */

struct SettingsApp;

// ── Wi-Fi ──────────────────────────────────────────────────────────────
#define WIFI_SSID_MAX   33
#define WIFI_PWD_MAX    65
#define WIFI_SCAN_MAX   16

typedef enum {
    WIFI_UI_DISCONNECTED = 0,
    WIFI_UI_CONNECTING,
    WIFI_UI_CONNECTED,
} wifi_ui_state_t;

typedef struct {
    char    ssid[WIFI_SSID_MAX];
    int8_t  rssi;
    uint8_t channel;
    bool    secured;      /* 非 OPEN 认证 → 需要密码 */
} wifi_scan_entry_t;

typedef struct {
    int  enabled;                 /* 电台开关，持久化 */
    wifi_ui_state_t state;
    bool busy;                    /* 扫描 / 连接进行中：UI 要禁用重复操作 */
    char busy_text[32];           /* "扫描中…" / "连接中…" */
    char last_error[64];          /* 上一次失败原因，空串表示没有 */

    wifi_info_t       current;    /* 当前连接快照 */
    wifi_scan_entry_t scanned[WIFI_SCAN_MAX];
    uint8_t           scanned_count;

    char pending_ssid[WIFI_SSID_MAX];   /* 正在连 / 正在输密码的那个 */
} settings_model_wifi_t;

// ── Wallpaper ──────────────────────────────────────────────────────────
typedef struct {
    char lock_screen_type[16];
    char wallpaper_folder[256];
    char frequency[16];
    char order[16];
} settings_model_wallpaper_t;

// ── Datetime ───────────────────────────────────────────────────────────
typedef struct {
    char date_type[16];
    int  hour_24_enabled;
    char timezone[64];
} settings_model_datetime_t;

// ── Battery ────────────────────────────────────────────────────────────
typedef struct {
    int battery_percentage_enabled;
    int auto_sleep_minutes;
} settings_model_battery_t;

// ── Storage ────────────────────────────────────────────────────────────
typedef struct {
    int storage_used_gb;
    int storage_total_gb;
} settings_model_storage_t;

// ── Update (OTA) ───────────────────────────────────────────────────────
typedef struct {
    int  auto_check_enabled;   // 开机后台自动查一次更新
    char manifest_url[192];    // 空串表示用 OTA_DEFAULT_MANIFEST_URL
} settings_model_update_t;

// ── Security ───────────────────────────────────────────────────────────
typedef struct {
    int  security_pin_enabled;
    char security_pin[5];
    char temp_passcode[5];
    int  personal_info_enabled;
} settings_model_security_t;

// ── Top-level model ────────────────────────────────────────────────────
typedef struct SettingsModel {
    settings_model_wallpaper_t wallpaper;
    settings_model_datetime_t  datetime;
    settings_model_battery_t   battery;
    settings_model_storage_t   storage;
    settings_model_security_t  security;
    settings_model_update_t    update;
    settings_model_wifi_t      wifi;
} SettingsModel;

// ── API ────────────────────────────────────────────────────────────────
void settings_model_init(struct SettingsApp *app);
void settings_model_deinit(struct SettingsApp *app);
void settings_model_save_general(struct SettingsApp *app);
void settings_model_save_battery(struct SettingsApp *app);
void settings_model_save_update(struct SettingsApp *app);
void settings_model_save_wifi(struct SettingsApp *app);

// 不依赖 app 实例读一份持久化的更新设置 —— 开机自动查更新时 Settings app
// 还没启动，模型也还没分配，只能这样拿。
void settings_model_load_update(settings_model_update_t *out);

#endif
