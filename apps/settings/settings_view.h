// apps/settings/settings_view.h
// Settings view — page_navigator + subpage registration (Phase 1)

#ifndef SETTINGS_VIEW_H
#define SETTINGS_VIEW_H

#include <lvgl.h>
#include "lv_theme_hardcore.h"
#include "lv_page.h"
#include "page_navigator.h"
#include "settings_model.h"

struct SettingsApp;

enum settings_page_id_t {
    PAGE_NONE = 0,
    PAGE_HOME,
    PAGE_GENERAL,
    PAGE_GENERAL_WALLPAPER,
    PAGE_GENERAL_DATETIME,
    PAGE_GENERAL_ABOUT,
    PAGE_GENERAL_FOLDER_SELECTOR,  // Phase 2
    PAGE_WIFI,                      // Phase 2
    PAGE_WIFI_PASSWORD,             // Phase 2
    PAGE_BLUETOOTH,                 // Phase 2
    PAGE_BATTERY,
    PAGE_STORAGE,
    PAGE_SECURITY,
    PAGE_SECURITY_CREATE,
    PAGE_SECURITY_VERIFY,
    PAGE_UPDATES,                   // Phase 2
    PAGE_UPDATES_DOWNLOAD,          // Phase 2
    PAGE_UPDATES_SD_SELECT,         // Phase 2
};

#define SETTINGS_PAGE_ID_MAX    20
#define SETTINGS_MAX_NAV_STACK  10

typedef struct SettingsView {
    page_navigator_t page_nav;
} SettingsView;

void settings_view_init(struct SettingsApp *app);
void settings_view_deinit(struct SettingsApp *app);

#endif
