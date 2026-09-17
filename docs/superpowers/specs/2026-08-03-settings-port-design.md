# Settings App Port — Design Spec

**Date**: 2026-08-03
**Source**: `D:\Codes\EPOS\epos\apps\settings` (Zephyr RTOS)
**Target**: `D:\Codes\DeepStoa\apps\settings` (ESP-IDF v5.5.3)

## Overview

Port the Settings application from EPOS/Zephyr to DeepStoa/ESP-IDF using the existing MVC architecture (Model-View-Controller). Phase 1 covers 5 core subpages without WiFi/BLE/OTA dependencies.

## Phase 1 Scope

| Subpage | Source File | Description |
|---------|-------------|-------------|
| Home | `subpages/view_home.c` | Settings main menu |
| General | `subpages/view_general.c` | Wallpaper + DateTime |
| Battery | `subpages/view_battery.c` | Battery percentage + auto sleep |
| Storage | `subpages/view_storage.c` | Storage usage info |
| Security | `subpages/view_security.c` | PIN code settings |

**Excluded (Phase 2)**: WiFi, Bluetooth, Updates, OTA download, BLE controller

## File Structure (Target)

```
apps/settings/
├── CMakeLists.txt              ← ESP-IDF component
├── settings_app.h/c            ← Entry: app registration, start/stop/back
├── settings_model.h/c          ← Data model (battery/storage/security/wallpaper/datetime)
├── settings_view.h/c           ← View: owns page_navigator, registers pages
├── settings_controller.h/c     ← Controller: event handlers + business logic
└── subpages/
    ├── view_home.h/c           ← Settings home menu
    ├── view_general.h/c        ← Wallpaper + datetime settings
    ├── view_battery.h/c        ← Battery settings
    ├── view_storage.h/c        ← Storage info
    └── view_security.h/c       ← PIN security settings
```

## Dependency Mapping

| EPOS Source | DeepStoa Target |
|-------------|-----------------|
| `zephyr/kernel.h` / `SYS_INIT()` | `app_manager_add_application()` explicit call |
| `zephyr/logging/log.h` | `esp_log.h` |
| `managers/epos_app_manager.h` | `system/appmgr/app_manager.h` |
| `epos_lv/framework/page_navigator.h` | `system/uilv/framework/page_navigator.h` |
| `epos_lv/theme/lv_theme_hardcore.h` | `system/uilv/theme/ls_theme.h` |
| `epos_lv/widgets/lv_page.h` | `system/uilv/widgets/lv_page.h` |
| `epos_lv/widgets/lv_bottom_sheet.h` | `system/uilv/widgets/lv_bottom_sheet.h` |
| `epos_lv/utils/ui_fonts.h` | `system/uilv/utils/ui_utils.h` |
| `wifi/wifi_comm.h` | Phase 2 — excluded |
| `drivers/epos_flash_control.h` (flashDB) | `system/controller/flash_control.h` |

## New: system/controller

Port EPOS drivers from `D:\Codes\EPOS\epos\drivers` to `system/controller/`:

```
system/controller/
├── CMakeLists.txt
├── flash_control.h/c           ← flashDB KV database (from epos_flash_control)
├── touch_control.h/c           ← FT6336 touch (from epd_touch_control)
├── display_control.h/c         ← E-ink display (from epd_display_control)
├── vibration_control.h/c       ← Vibration motor (from epos_vibration_control)
└── sd_control.h/c              ← SD card (from epos_sd_control)
```

**flashDB integration**: Added as ESP-IDF component via component registry (`FlashDB`). Replaces Zephyr `fal` layer with `esp_partition` API.

## Data Flow

```
LVGL UI Event
  → Settings Controller (handler)
    → Settings Model (read/write struct fields)
      → flash_control (fdb_kvdb_set/get)
        → FlashDB (key-value store)
          → ESP-IDF Partition Table (NVS partition)
```

## Key Adaptations (Zephyr → ESP-IDF)

1. **App registration**: Replace `SYS_INIT()` with explicit `settings_init()` called from main, calling `app_manager_add_application()`

2. **Logging**: `LOG_MODULE_REGISTER()` + `LOG_INF()` → `static const char *TAG` + `ESP_LOGI()`

3. **LVGL images**: `EPOS_LV_IMG_DECLARE()` → `LV_IMG_DECLARE()`

4. **Conditional compilation**: Remove `#if CONFIG_BUILD_WIFI` / `#if CONFIG_BUILD_BLE` — excluded files simply not compiled

5. **Flash persistence**: `epos_flash_control` → `flash_control` (same flashDB API, different init via `esp_partition`)

6. **Memory management**: Model struct stays heap-allocated; pointers wired in `settings_app_start()`
