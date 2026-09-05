# Settings App Port — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port `settings` app from EPOS/Zephyr to DeepStoa/ESP-IDF (Phase 1: 5 core subpages, excluding WiFi/BLE/OTA).

**Architecture:** MVC pattern — Model holds data with flashDB persistence, View manages LVGL page_navigator, Controller handles UI events. App registered via `app_manager_add_application()`.

**Tech Stack:** ESP-IDF v5.5.3, LVGL, FlashDB, FreeRTOS, C

## Global Constraints

- Target: ESP32-S3, ESP-IDF v5.5.3
- Phase 1 scope: Home, General (wallpaper+datetime), Battery, Storage, Security subpages only
- WiFi, BLE, OTA: excluded (not compiled in Phase 1)
- Use `system/appmgr/app_manager.h` for app lifecycle
- Use `system/uilv/` for LVGL framework (page_navigator, theme, widgets)
- Use `system/controller/flash_control` for flashDB persistence
- FlashDB installed as ESP-IDF component via `idf.py add-dependency`

---

### Task 1: Install FlashDB and create flash_control

**Files:**
- Create: `system/controller/CMakeLists.txt`
- Create: `system/controller/flash_control.h`
- Create: `system/controller/flash_control.c`

**Interfaces:**
- Produces: `extern struct fdb_kvdb g_kvdb;` — global KVDB instance (same name as EPOS `kvdb` for source compatibility)
- Produces: `void flash_control_init(void);` — init FAL + flashDB KV
- Produces: `void flash_control_deinit(void);` — deinit flashDB KV

- [ ] **Step 1: Add FlashDB as managed dependency**

Create `apps/settings/idf_component.yml` (or add at project level):
```yaml
dependencies:
  FlashDB:
    version: "*"
```

Alternatively, clone FlashDB into `components/flashdb`:
```bash
cd D:/Codes/DeepStoa
git clone https://github.com/armink/FlashDB.git components/flashdb
```

- [ ] **Step 2: Create system/controller/CMakeLists.txt**

```cmake
# system/controller/CMakeLists.txt
idf_component_register(
    SRCS "flash_control.c"
    INCLUDE_DIRS "."
    REQUIRES driver
)
```

- [ ] **Step 3: Create system/controller/flash_control.h**

```c
// system/controller/flash_control.h
// FlashDB key-value database wrapper for ESP-IDF
// Ported from D:\Codes\EPOS\epos\drivers\epos_flash_control.h

#ifndef FLASH_CONTROL_H
#define FLASH_CONTROL_H

#include <flashdb.h>

// KVDB instance — shared across all modules that need persistent settings
extern struct fdb_kvdb g_kvdb;

// Initialize flashDB (FAL + KVDB). Call once at boot.
void flash_control_init(void);

// Deinitialize flashDB. Call before factory reset or shutdown.
void flash_control_deinit(void);

#endif
```

- [ ] **Step 4: Create system/controller/flash_control.c**

```c
// system/controller/flash_control.c
// FlashDB KV database on ESP-IDF partition "fdb_kvdb"

#include "flash_control.h"
#include <fal.h>
#include <flashdb.h>
#include "esp_log.h"

static const char *TAG = "flash_ctrl";

struct fdb_kvdb g_kvdb;

void flash_control_init(void)
{
    fal_init();
    // "env" = database name, "fdb_kvdb" = partition name in partition table
    fdb_kvdb_init(&g_kvdb, "env", "fdb_kvdb", NULL, NULL);
    ESP_LOGI(TAG, "FlashDB KV initialized on partition 'fdb_kvdb'");
}

void flash_control_deinit(void)
{
    fdb_kvdb_deinit(&g_kvdb);
    ESP_LOGI(TAG, "FlashDB KV deinitialized");
}
```

- [ ] **Step 5: Add fdb_kvdb partition to partition table**

Add to project partition table (e.g., `partitions.csv`):
```
# Name,      Type, SubType, Offset,   Size,   Flags
fdb_kvdb,    data, fat,     0x200000, 128K,
```

Note: Offset/size depends on flash layout. For 16MB flash, this is a safe default.

- [ ] **Step 6: Commit**

```bash
git add system/controller/
git commit -m "feat(controller): add flash_control (FlashDB KV wrapper)"
```

---

### Task 2: Create apps/settings component skeleton

**Files:**
- Create: `apps/settings/CMakeLists.txt`
- Create: `apps/settings/settings_app.h`
- Create: `apps/settings/settings_app.c`
- Create: `apps/settings/settings_model.h`
- Create: `apps/settings/settings_model.c`
- Create: `apps/settings/settings_controller.h`
- Create: `apps/settings/settings_controller.c`
- Create: `apps/settings/settings_view.h`
- Create: `apps/settings/settings_view.c`
- Create: `apps/settings/subpages/view_home.h`
- Create: `apps/settings/subpages/view_home.c`
- Create: `apps/settings/subpages/view_general.h`
- Create: `apps/settings/subpages/view_general.c`
- Create: `apps/settings/subpages/view_battery.h`
- Create: `apps/settings/subpages/view_battery.c`
- Create: `apps/settings/subpages/view_storage.h`
- Create: `apps/settings/subpages/view_storage.c`
- Create: `apps/settings/subpages/view_security.h`
- Create: `apps/settings/subpages/view_security.c`

**Interfaces:**
- Produces: `SettingsApp g_settings_app;` — global app instance
- Produces: `void settings_init(void);` — register app with app_manager
- Produces: `settings_app_start/stop/back` — lifecycle callbacks matching `application_t` callbacks

- [ ] **Step 1: Create CMakeLists.txt**

```cmake
# apps/settings/CMakeLists.txt
idf_component_register(
    SRCS
        "settings_app.c"
        "settings_model.c"
        "settings_controller.c"
        "settings_view.c"
        "subpages/view_home.c"
        "subpages/view_general.c"
        "subpages/view_battery.c"
        "subpages/view_storage.c"
        "subpages/view_security.c"
    INCLUDE_DIRS "."
    REQUIRES driver lvgl
    PRIV_REQUIRES appmgr uilv controller flashdb
)
```

- [ ] **Step 2: Create empty stub files so build passes**

Create all the `.h` and `.c` files with minimal stubs (header guards, empty init functions). Details filled in subsequent tasks.

`settings_app.h`:
```c
#ifndef SETTINGS_APP_H
#define SETTINGS_APP_H
#include <lvgl.h>
#include "system/uilv/framework/page_navigator.h"

struct SettingsModel;
struct SettingsView;
struct SettingsController;

typedef struct SettingsApp {
    struct SettingsModel      *model;
    struct SettingsView       *view;
    struct SettingsController *controller;
} SettingsApp;

extern SettingsApp g_settings_app;
void settings_init(void);
#endif
```

- [ ] **Step 3: Commit skeleton**

```bash
git add apps/settings/
git commit -m "feat(settings): add component skeleton"
```

---

### Task 3: Port settings_model.h/c (data model + flashDB persistence)

**Files:**
- Modify: `apps/settings/settings_model.h`
- Modify: `apps/settings/settings_model.c`

**Interfaces:**
- Consumes: `struct fdb_kvdb g_kvdb;` from flash_control
- Produces: `void settings_model_init(SettingsApp *app);` — malloc model, load from flashDB
- Produces: `void settings_model_deinit(SettingsApp *app);` — free model
- Produces: `void settings_model_save_general(SettingsApp *app);` — persist wallpaper+datetime to flashDB
- Produces: `void settings_model_save_battery(SettingsApp *app);` — persist battery to flashDB

- [ ] **Step 1: Write settings_model.h (Phase 1 structs only)**

```c
#ifndef SETTINGS_MODEL_H
#define SETTINGS_MODEL_H

#include <stdint.h>

struct SettingsApp;

// Wallpaper model
typedef struct {
    char lock_screen_type[16];
    char wallpaper_folder[256];
    char frequency[16];
    char order[16];
} settings_model_wallpaper_t;

// Datetime model
typedef struct {
    char date_type[16];
    int  hour_24_enabled;
    char timezone[64];
} settings_model_datetime_t;

// Battery model
typedef struct {
    int battery_percentage_enabled;
    int auto_sleep_minutes;
} settings_model_battery_t;

// Storage model
typedef struct {
    int storage_used_gb;
    int storage_total_gb;
} settings_model_storage_t;

// Security model
typedef struct {
    int  security_pin_enabled;
    char security_pin[5];
    char temp_passcode[5];
    int  personal_info_enabled;
} settings_model_security_t;

// SettingsModel (Phase 1 — WiFi/BLE deferred to Phase 2)
typedef struct SettingsModel {
    settings_model_wallpaper_t wallpaper;
    settings_model_datetime_t  datetime;
    settings_model_battery_t   battery;
    settings_model_storage_t   storage;
    settings_model_security_t  security;
} SettingsModel;

void settings_model_init(struct SettingsApp *app);
void settings_model_deinit(struct SettingsApp *app);
void settings_model_save_general(struct SettingsApp *app);
void settings_model_save_battery(struct SettingsApp *app);

#endif
```

- [ ] **Step 2: Write settings_model.c**

```c
#include <string.h>
#include <stdlib.h>
#include <flashdb.h>
#include "esp_log.h"
#include "settings_app.h"
#include "settings_model.h"

static const char *TAG = "settings_model";

// FlashDB keys (same as EPOS)
#define CFG_KEY_WALLPAPER "cfg_wp"
#define CFG_KEY_DATETIME  "cfg_dt"
#define CFG_KEY_BATTERY   "cfg_bat"

extern struct fdb_kvdb g_kvdb;

void settings_model_init(struct SettingsApp *app)
{
    app->model = (SettingsModel *)malloc(sizeof(SettingsModel));
    memset(app->model, 0, sizeof(SettingsModel));

    struct fdb_blob blob;
    size_t read_len;

    // --- Wallpaper ---
    fdb_blob_make(&blob, &app->model->wallpaper, sizeof(app->model->wallpaper));
    read_len = fdb_kv_get_blob(&g_kvdb, CFG_KEY_WALLPAPER, &blob);
    if (read_len == 0) {
        strcpy(app->model->wallpaper.lock_screen_type, "Default");
        app->model->wallpaper.wallpaper_folder[0] = '\0';
        strcpy(app->model->wallpaper.frequency, "5 minutes");
        strcpy(app->model->wallpaper.order, "Random");
    }

    // --- Datetime ---
    fdb_blob_make(&blob, &app->model->datetime, sizeof(app->model->datetime));
    read_len = fdb_kv_get_blob(&g_kvdb, CFG_KEY_DATETIME, &blob);
    if (read_len == 0) {
        strcpy(app->model->datetime.date_type, "YYYY-MM-DD");
        app->model->datetime.hour_24_enabled = 1;
        strcpy(app->model->datetime.timezone,
               "(UTC+08:00) Beijing, Singapore, Kuala Lumpur, Perth");
    }

    // --- Battery ---
    fdb_blob_make(&blob, &app->model->battery, sizeof(app->model->battery));
    read_len = fdb_kv_get_blob(&g_kvdb, CFG_KEY_BATTERY, &blob);
    if (read_len == 0) {
        app->model->battery.battery_percentage_enabled = 1;
        app->model->battery.auto_sleep_minutes = 15;
    }

    // --- Storage (hardcoded defaults) ---
    app->model->storage.storage_used_gb = 512;
    app->model->storage.storage_total_gb = 2048;

    // --- Security (defaults) ---
    app->model->security.security_pin_enabled = 0;
    app->model->security.security_pin[0] = '\0';
    app->model->security.personal_info_enabled = 0;

    ESP_LOGI(TAG, "Model initialized");
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
```

- [ ] **Step 3: Commit**

```bash
git add apps/settings/settings_model.h apps/settings/settings_model.c
git commit -m "feat(settings): port settings_model with flashDB persistence"
```

---

### Task 4: Port settings_controller.h/c

**Files:**
- Modify: `apps/settings/settings_controller.h`
- Modify: `apps/settings/settings_controller.c`

**Interfaces:**
- Consumes: `SettingsApp *app` containing model pointer
- Produces: `void settings_controller_init(SettingsApp *app);`
- Produces: `void settings_controller_deinit(SettingsApp *app);`
- Produces: Event handler functions for all Phase 1 settings

- [ ] **Step 1: Write settings_controller.h (Phase 1)**

```c
#ifndef SETTINGS_CONTROLLER_H
#define SETTINGS_CONTROLLER_H

#include <lvgl.h>

struct SettingsApp;

typedef struct SettingsController {
    struct SettingsModel *model;
    struct SettingsView  *view;
} SettingsController;

// Timezone map entry
typedef struct {
    const char *ui_label;
    const char *posix_str;
} epos_timezone_map_t;

extern const epos_timezone_map_t epos_timezones[];
extern const uint16_t epos_timezones_count;

void settings_controller_init(struct SettingsApp *app);
void settings_controller_deinit(struct SettingsApp *app);

// Reboot / shutdown
void settings_controller_reboot(lv_event_t *e);
void settings_controller_shutdown(lv_event_t *e);
void settings_controller_factory_reset(struct SettingsApp *app);

// Wallpaper
void settings_controller_set_wallpaper_lock_screen(lv_event_t *e);
void settings_controller_set_wallpaper_folder(lv_event_t *e);
void settings_controller_set_wallpaper_frequency(lv_event_t *e);
void settings_controller_set_wallpaper_order(lv_event_t *e);

// Datetime
void settings_controller_set_date_type(lv_event_t *e);
void settings_controller_set_hour_24(lv_event_t *e);
void settings_controller_set_timezone(lv_event_t *e);

// General exit (save)
void settings_controller_on_general_exit(lv_event_t *e);

// Battery
void settings_controller_set_battery_percentage(lv_event_t *e);
void settings_controller_set_auto_sleep(lv_event_t *e);
void settings_controller_on_battery_exit(lv_event_t *e);

#endif
```

- [ ] **Step 2: Write settings_controller.c (Phase 1 — adapt from EPOS)**

Key adaptations from EPOS source:
- `k_malloc` → `malloc`
- `k_free` → `free`
- `LOG_INF/LOG_ERR` → `ESP_LOGI/ESP_LOGE`
- `sys_reboot()` → `esp_restart()`
- `sys_poweroff()` → `esp_deep_sleep_start()` (or stub)
- `epos_clock_set_timezone()` → stub for now (clock system not yet ported)
- `epos_retained_ram_update()` → removed (ESP-IDF uses NVS)
- `flash_area_open()` factory reset → use flashDB partition erase

Full implementation:
```c
#include <string.h>
#include <stdlib.h>
#include <lvgl.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_sleep.h"
#include "flashdb.h"

#include "settings_controller.h"
#include "settings_app.h"
#include "settings_model.h"
#include "flash_control.h"

static const char *TAG = "settings_ctrl";

const epos_timezone_map_t epos_timezones[] = {
    {"(UTC+12:00) Auckland, Wellington, Fiji", "UTC-12"},
    {"(UTC+11:00) Solomon Islands, New Caledonia", "UTC-11"},
    {"(UTC+10:00) Sydney, Melbourne, Guam, Brisbane", "UTC-10"},
    {"(UTC+09:00) Tokyo, Seoul, Osaka, Sapporo", "UTC-9"},
    {"(UTC+08:00) Beijing, Singapore, Kuala Lumpur, Perth", "UTC-8"},
    {"(UTC+07:00) Bangkok, Hanoi, Jakarta", "UTC-7"},
    {"(UTC+06:00) Almaty, Dhaka", "UTC-6"},
    {"(UTC+05:00) Islamabad, Karachi, Tashkent", "UTC-5"},
    {"(UTC+04:00) Abu Dhabi, Muscat, Tbilisi", "UTC-4"},
    {"(UTC+03:00) Moscow, St. Petersburg, Kuwait, Nairobi", "UTC-3"},
    {"(UTC+02:00) Athens, Istanbul, Jerusalem, Cairo", "UTC-2"},
    {"(UTC+01:00) Berlin, Rome, Paris, Madrid, Warsaw", "UTC-1"},
    {"(UTC+00:00) London, Dublin, Lisbon, Casablanca", "UTC0"},
    {"(UTC-01:00) Azores, Cape Verde", "UTC+1"},
    {"(UTC-02:00) Mid-Atlantic", "UTC+2"},
    {"(UTC-03:00) Brasilia, Buenos Aires, Greenland", "UTC+3"},
    {"(UTC-04:00) Atlantic Time (Canada), Caracas", "UTC+4"},
    {"(UTC-05:00) Eastern Time (US & Canada), Bogota", "UTC+5"},
    {"(UTC-06:00) Central Time (US & Canada), Mexico City", "UTC+6"},
    {"(UTC-07:00) Mountain Time (US & Canada), Chihuahua", "UTC+7"},
    {"(UTC-08:00) Pacific Time (US & Canada), Tijuana", "UTC+8"},
    {"(UTC-09:00) Alaska", "UTC+9"},
    {"(UTC-10:00) Hawaii", "UTC+10"},
    {"(UTC-11:00) Midway Island, Samoa", "UTC+11"},
    {"(UTC-12:00) International Date Line West", "UTC+12"},
    {"(UTC-13:00) ", "UTC+13"},
    {"(UTC-14:00) Line Islands", "UTC+14"}
};

const uint16_t epos_timezones_count =
    sizeof(epos_timezones) / sizeof(epos_timezones[0]);

void settings_controller_init(struct SettingsApp *app)
{
    app->controller = (SettingsController *)malloc(sizeof(SettingsController));
    memset(app->controller, 0, sizeof(SettingsController));
}

void settings_controller_deinit(struct SettingsApp *app)
{
    if (app->controller) {
        free(app->controller);
        app->controller = NULL;
    }
}

void settings_controller_reboot(lv_event_t *e)
{
    esp_restart();
}

void settings_controller_shutdown(lv_event_t *e)
{
    esp_deep_sleep_start();
}

void settings_controller_factory_reset(struct SettingsApp *app)
{
    ESP_LOGI(TAG, "Factory reset: erasing flashDB partition...");
    flash_control_deinit();

    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                 ESP_PARTITION_SUBTYPE_ANY, "fdb_kvdb");
    if (part) {
        esp_partition_erase_range(part, 0, part->size);
        ESP_LOGI(TAG, "FlashDB partition erased. Rebooting...");
    } else {
        ESP_LOGE(TAG, "fdb_kvdb partition not found!");
    }
    esp_restart();
}

// ── Wallpaper handlers ────────────────────────────────────────────────

void settings_controller_set_wallpaper_lock_screen(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    lv_obj_t *dd = lv_event_get_target(e);
    char sel[32];
    lv_dropdown_get_selected_str(dd, sel, sizeof(sel));
    strncpy(app->model->wallpaper.lock_screen_type, sel,
            sizeof(app->model->wallpaper.lock_screen_type) - 1);
}

void settings_controller_set_wallpaper_folder(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    const char *text = lv_textarea_get_text(lv_event_get_target(e));
    strncpy(app->model->wallpaper.wallpaper_folder, text,
            sizeof(app->model->wallpaper.wallpaper_folder) - 1);
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

// ── Datetime handlers ─────────────────────────────────────────────────

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
    app->model->datetime.hour_24_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
}

void settings_controller_set_timezone(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    uint16_t idx = lv_dropdown_get_selected(lv_event_get_target(e));
    if (idx < epos_timezones_count) {
        strncpy(app->model->datetime.timezone,
                epos_timezones[idx].ui_label,
                sizeof(app->model->datetime.timezone) - 1);
        // TODO Phase 2: call clock system to apply timezone
        ESP_LOGI(TAG, "Timezone: %s", epos_timezones[idx].posix_str);
    }
}

void settings_controller_on_general_exit(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    settings_model_save_general(app);
}

// ── Battery handlers ──────────────────────────────────────────────────

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
```

- [ ] **Step 3: Commit**

```bash
git add apps/settings/settings_controller.h apps/settings/settings_controller.c
git commit -m "feat(settings): port settings_controller (Phase 1)"
```

---

### Task 5: Port subpage views (Phase 1: 5 subpages)

**Files:**
- Create: `apps/settings/subpages/view_home.h` / `.c`
- Create: `apps/settings/subpages/view_general.h` / `.c`
- Create: `apps/settings/subpages/view_battery.h` / `.c`
- Create: `apps/settings/subpages/view_storage.h` / `.c`
- Create: `apps/settings/subpages/view_security.h` / `.c`

**Interfaces:**
- Each subpage produces: `void settings_view_<name>_init_registry(SettingsApp *app);` — registers page builder with page_navigator
- Each subpage produces: `lv_obj_t *settings_view_<name>_build(SettingsApp *app, void *user_data);` — creates LVGL page

**Porting approach**: Copy the EPOS source code for each subpage, then apply mechanical transformations:
- `#include <zephyr/logging/log.h>` → `#include "esp_log.h"`
- `LOG_MODULE_REGISTER(...)` → `static const char *TAG = "settings_<name>";`
- `LOG_INF/LOG_DBG` → `ESP_LOGI/ESP_LOGD`
- `#include "epos_lv/theme/lv_theme_hardcore.h"` → `#include "system/uilv/theme/ls_theme.h"`
- `#include "epos_lv/widgets/lv_page.h"` → `#include "system/uilv/widgets/lv_page.h"`
- `#include "epos_lv/utils/ui_fonts.h"` → `#include "system/uilv/utils/ui_utils.h"`
- `#include "../../.."` relative paths → correct paths to app/model/controller headers

- [ ] **Step 1: Port view_home.c from EPOS**

Read source: `D:\Codes\EPOS\epos\apps\settings\subpages\view_home.c`
Apply mechanical transformations above.
Save to: `apps/settings/subpages/view_home.c`

- [ ] **Step 2: Port view_general.c from EPOS**

Read source: `D:\Codes\EPOS\epos\apps\settings\subpages\view_general.c`
Apply transformations.
Save to: `apps/settings/subpages/view_general.c`

- [ ] **Step 3: Port view_battery.c from EPOS**

Read source: `D:\Codes\EPOS\epos\apps\settings\subpages\view_battery.c`
Apply transformations.
Save to: `apps/settings/subpages/view_battery.c`

- [ ] **Step 4: Port view_storage.c from EPOS**

Read source: `D:\Codes\EPOS\epos\apps\settings\subpages\view_storage.c`
Apply transformations.
Save to: `apps/settings/subpages/view_storage.c`

- [ ] **Step 5: Port view_security.c from EPOS**

Read source: `D:\Codes\EPOS\epos\apps\settings\subpages\view_security.c`
Apply transformations.
Save to: `apps/settings/subpages/view_security.c`

- [ ] **Step 6: Commit**

```bash
git add apps/settings/subpages/
git commit -m "feat(settings): port Phase 1 subpage views"
```

---

### Task 6: Port settings_view.h/c (page navigator wiring)

**Files:**
- Modify: `apps/settings/settings_view.h`
- Modify: `apps/settings/settings_view.c`

**Interfaces:**
- Produces: `void settings_view_init(SettingsApp *app);` — alloc view, init page_navigator, register pages
- Produces: `void settings_view_deinit(SettingsApp *app);` — deinit + free

- [ ] **Step 1: Write settings_view.h**

```c
#ifndef SETTINGS_VIEW_H
#define SETTINGS_VIEW_H

#include <lvgl.h>
#include "system/uilv/framework/page_navigator.h"
#include "settings_model.h"

struct SettingsApp;

enum settings_page_id_t {
    PAGE_NONE = 0,
    PAGE_HOME,
    PAGE_GENERAL,
    PAGE_GENERAL_WALLPAPER,
    PAGE_GENERAL_DATETIME,
    PAGE_GENERAL_ABOUT,
    PAGE_GENERAL_FOLDER_SELECTOR,
    PAGE_WIFI,
    PAGE_WIFI_PASSWORD,
    PAGE_BLUETOOTH,
    PAGE_BATTERY,
    PAGE_STORAGE,
    PAGE_SECURITY,
    PAGE_SECURITY_CREATE,
    PAGE_SECURITY_VERIFY,
    PAGE_UPDATES,
    PAGE_UPDATES_DOWNLOAD,
    PAGE_UPDATES_SD_SELECT,
};

#define SETTINGS_PAGE_ID_MAX    20
#define SETTINGS_MAX_NAV_STACK  10

typedef struct SettingsView {
    page_navigator_t page_nav;
} SettingsView;

void settings_view_init(struct SettingsApp *app);
void settings_view_deinit(struct SettingsApp *app);

#endif
```

- [ ] **Step 2: Write settings_view.c (Phase 1 — register only 5 subpages)**

```c
#include <string.h>
#include <stdlib.h>
#include <lvgl.h>
#include "esp_log.h"

#include "settings_app.h"
#include "settings_view.h"
#include "settings_controller.h"

// Subpage includes
#include "subpages/view_home.h"
#include "subpages/view_general.h"
#include "subpages/view_battery.h"
#include "subpages/view_storage.h"
#include "subpages/view_security.h"

static const char *TAG = "settings_view";

void settings_view_init(struct SettingsApp *app)
{
    app->view = (SettingsView *)malloc(sizeof(SettingsView));
    memset(app->view, 0, sizeof(SettingsView));

    page_builder_cb_t *registry =
        (page_builder_cb_t *)malloc(sizeof(page_builder_cb_t) * SETTINGS_PAGE_ID_MAX);
    memset(registry, 0, sizeof(page_builder_cb_t) * SETTINGS_PAGE_ID_MAX);

    page_navigator_init(&app->view->page_nav, registry, SETTINGS_PAGE_ID_MAX, app);

    // Register Phase 1 subpages
    settings_view_home_init_registry(app);
    settings_view_general_init_registry(app);
    settings_view_battery_init_registry(app);
    settings_view_storage_init_registry(app);
    settings_view_security_init_registry(app);

    ESP_LOGI(TAG, "View initialized with 5 subpages");
}

void settings_view_deinit(struct SettingsApp *app)
{
    if (app->view) {
        page_navigator_deinit(&app->view->page_nav);
        free(app->view->page_nav.registry);
        free(app->view);
        app->view = NULL;
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add apps/settings/settings_view.h apps/settings/settings_view.c
git commit -m "feat(settings): port settings_view with Phase 1 subpage registration"
```

---

### Task 7: Port settings_app.h/c (entry point + app registration)

**Files:**
- Modify: `apps/settings/settings_app.h`
- Modify: `apps/settings/settings_app.c`

- [ ] **Step 1: Write settings_app.c**

```c
#include <string.h>
#include <lvgl.h>
#include "esp_log.h"

#include "system/appmgr/app_manager.h"
#include "system/uilv/framework/page_navigator.h"

#include "settings_app.h"
#include "settings_view.h"
#include "settings_controller.h"
#include "settings_model.h"

static const char *TAG = "settings_app";

SettingsApp g_settings_app;

static void settings_app_start(lv_obj_t *root, lv_group_t *group)
{
    ESP_LOGI(TAG, "Starting settings app");

    settings_model_init(&g_settings_app);
    settings_controller_init(&g_settings_app);
    settings_view_init(&g_settings_app);

    g_settings_app.controller->model = g_settings_app.model;
    g_settings_app.controller->view  = g_settings_app.view;

    page_navigator_navigate_to(&g_settings_app.view->page_nav,
                               &g_settings_app, PAGE_HOME, NULL);
}

static void settings_app_stop(void)
{
    ESP_LOGI(TAG, "Stopping settings app");
    settings_controller_deinit(&g_settings_app);
    settings_view_deinit(&g_settings_app);
    settings_model_deinit(&g_settings_app);
    memset(&g_settings_app, 0, sizeof(SettingsApp));
}

static bool settings_app_back(void)
{
    return page_navigator_navigate_pop(&g_settings_app.view->page_nav,
                                       &g_settings_app);
}

static bool settings_app_factory_reset(void)
{
    ESP_LOGI(TAG, "Factory reset requested");
    return true;
}

// LVGL image placeholder — replace with actual icon asset later
LV_IMG_DECLARE(app_settings_logo);

static application_t s_settings_app = {
    .name                = "Settings",
    .icon                = NULL,  // TODO: add settings icon
    .start_func          = settings_app_start,
    .stop_func           = settings_app_stop,
    .back_func           = settings_app_back,
    .factory_reset_func  = settings_app_factory_reset,
    .category            = APP_CATEGORY_SYSTEM,
};

void settings_init(void)
{
    app_manager_add_application(&s_settings_app);
    ESP_LOGI(TAG, "Settings app registered");
}
```

- [ ] **Step 2: Update settings_app.h**

Already correct from skeleton. Header has `SettingsApp` struct, `g_settings_app` extern, and `settings_init()`.

- [ ] **Step 3: Commit**

```bash
git add apps/settings/settings_app.c
git commit -m "feat(settings): port settings_app entry + app_manager registration"
```

---

### Task 8: Compile and verify

- [ ] **Step 1: Configure project to include settings component**

Add to project `CMakeLists.txt` or `main/CMakeLists.txt`:
```cmake
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/../system/controller
    ${CMAKE_SOURCE_DIR}/../system/appmgr
    ${CMAKE_SOURCE_DIR}/../system/uilv
    ${CMAKE_SOURCE_DIR}/../system/clock
    ${CMAKE_SOURCE_DIR}/../system/battery
    ${CMAKE_SOURCE_DIR}/../system/flash
    ${CMAKE_SOURCE_DIR}/../system/sleep
    ${CMAKE_SOURCE_DIR}/../system/launcher
    ${CMAKE_SOURCE_DIR}/../system/events
    ${CMAKE_SOURCE_DIR}/../system/input
    ${CMAKE_SOURCE_DIR}/../system/logs
    ${CMAKE_SOURCE_DIR}/../system/ota
    ${CMAKE_SOURCE_DIR}/../system/wifi
    ${CMAKE_SOURCE_DIR}/../apps/settings
)
```

- [ ] **Step 2: Call settings_init() from app_main()**

In `main/DeepStoa.c`, add:
```c
#include "settings_app.h"

// In app_main(), after app_manager_init():
settings_init();
```

- [ ] **Step 3: Build**

```bash
idf.py build
```

- [ ] **Step 4: Fix any compile errors**

Expected issues:
- Missing include paths → add to CMakeLists
- flashDB FAL partition config → create `fal_cfg.h` with partition table mapping
- LVGL API differences between Zephyr version and ESP-IDF LVGL version → adjust

- [ ] **Step 5: Commit final fixes**

```bash
git add -A
git commit -m "build: wire settings app into project, fix compilation"
```

