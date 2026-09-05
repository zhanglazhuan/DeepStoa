// apps/settings/settings_model.c
// Settings data model with FlashDB persistence (Phase 1)

#include <string.h>
#include <stdlib.h>
#include <flashdb.h>
#include "esp_log.h"
#include "settings_app.h"
#include "settings_model.h"

static const char *TAG = "settings_model";

// FlashDB keys (same as EPOS)
#define CFG_KEY_WALLPAPER  "cfg_wp"
#define CFG_KEY_DATETIME   "cfg_dt"
#define CFG_KEY_BATTERY    "cfg_bat"
#define CFG_KEY_UPDATE     "cfg_upd"
#define CFG_KEY_WIFI       "cfg_wifi"

extern struct fdb_kvdb g_kvdb;

void settings_model_init(struct SettingsApp *app)
{
    app->model = (SettingsModel *)malloc(sizeof(SettingsModel));
    memset(app->model, 0, sizeof(SettingsModel));

    struct fdb_blob blob;
    size_t read_len;

    // ── Wallpaper (load from flashDB or use defaults) ──────────────
    fdb_blob_make(&blob, &app->model->wallpaper, sizeof(app->model->wallpaper));
    read_len = fdb_kv_get_blob(&g_kvdb, CFG_KEY_WALLPAPER, &blob);
    if (read_len == 0) {
        strcpy(app->model->wallpaper.lock_screen_type, "Default");
        app->model->wallpaper.wallpaper_folder[0] = '\0';
        strcpy(app->model->wallpaper.frequency, "5 minutes");
        strcpy(app->model->wallpaper.order, "Random");
    }

    // ── Datetime ────────────────────────────────────────────────────
    fdb_blob_make(&blob, &app->model->datetime, sizeof(app->model->datetime));
    read_len = fdb_kv_get_blob(&g_kvdb, CFG_KEY_DATETIME, &blob);
    if (read_len == 0) {
        strcpy(app->model->datetime.date_type, "YYYY-MM-DD");
        app->model->datetime.hour_24_enabled = 1;
        strcpy(app->model->datetime.timezone,
               "(UTC+08:00) Beijing, Singapore, Kuala Lumpur, Perth");
    }

    // ── Wi-Fi（只持久化电台开关；凭据由 wifi_manager 自己存，
    //    扫描结果是易失的，不入库）────────────────────────────────
    {
        int enabled = 0;
        fdb_blob_make(&blob, &enabled, sizeof(enabled));
        read_len = fdb_kv_get_blob(&g_kvdb, CFG_KEY_WIFI, &blob);
        app->model->wifi.enabled = (read_len == sizeof(enabled)) ? enabled : 0;
        app->model->wifi.state   = WIFI_UI_DISCONNECTED;
    }

    // ── Battery ─────────────────────────────────────────────────────
    fdb_blob_make(&blob, &app->model->battery, sizeof(app->model->battery));
    read_len = fdb_kv_get_blob(&g_kvdb, CFG_KEY_BATTERY, &blob);
    if (read_len == 0) {
        app->model->battery.battery_percentage_enabled = 1;
        app->model->battery.auto_sleep_minutes = 15;
    }

    // ── Update (OTA) ────────────────────────────────────────────────
    settings_model_load_update(&app->model->update);

    // ── Storage (hardcoded defaults, not persisted) ─────────────────
    app->model->storage.storage_used_gb  = 512;
    app->model->storage.storage_total_gb = 2048;

    // ── Security (defaults, not yet persisted to flashDB) ───────────
    app->model->security.security_pin_enabled = 0;
    app->model->security.security_pin[0] = '\0';
    app->model->security.personal_info_enabled = 0;

    ESP_LOGI(TAG, "Model initialized from FlashDB");
}

void settings_model_load_update(settings_model_update_t *out)
{
    struct fdb_blob blob;
    fdb_blob_make(&blob, out, sizeof(*out));
    if (fdb_kv_get_blob(&g_kvdb, CFG_KEY_UPDATE, &blob) == 0) {
        out->auto_check_enabled = 1;
        out->manifest_url[0] = '\0';   // 空 = 用 OTA_DEFAULT_MANIFEST_URL
    }
}

void settings_model_deinit(struct SettingsApp *app)
{
    if (app->model) {
        free(app->model);
        app->model = NULL;
    }
}

void settings_model_save_general(struct SettingsApp *app)
{
    struct fdb_blob blob;

    fdb_blob_make(&blob, &app->model->wallpaper, sizeof(app->model->wallpaper));
    fdb_kv_set_blob(&g_kvdb, CFG_KEY_WALLPAPER, &blob);

    fdb_blob_make(&blob, &app->model->datetime, sizeof(app->model->datetime));
    fdb_kv_set_blob(&g_kvdb, CFG_KEY_DATETIME, &blob);

    ESP_LOGI(TAG, "General settings saved to FlashDB");
}

void settings_model_save_battery(struct SettingsApp *app)
{
    struct fdb_blob blob;
    fdb_blob_make(&blob, &app->model->battery, sizeof(app->model->battery));
    fdb_kv_set_blob(&g_kvdb, CFG_KEY_BATTERY, &blob);
    ESP_LOGI(TAG, "Battery settings saved to FlashDB");
}

void settings_model_save_update(struct SettingsApp *app)
{
    struct fdb_blob blob;
    fdb_blob_make(&blob, &app->model->update, sizeof(app->model->update));
    fdb_kv_set_blob(&g_kvdb, CFG_KEY_UPDATE, &blob);
    ESP_LOGI(TAG, "Update settings saved to FlashDB");
}

void settings_model_save_wifi(struct SettingsApp *app)
{
    /* 只存电台开关。SSID/密码由 wifi_manager 走 NVS 自己管，
     * 扫描结果每次进页面都会重扫，没有持久化的意义。 */
    struct fdb_blob blob;
    int enabled = app->model->wifi.enabled;
    fdb_blob_make(&blob, &enabled, sizeof(enabled));
    fdb_kv_set_blob(&g_kvdb, CFG_KEY_WIFI, &blob);
    ESP_LOGI(TAG, "Wi-Fi enabled=%d saved", enabled);
}
