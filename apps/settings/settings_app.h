// apps/settings/settings_app.h
// Settings app entry — app_manager registration
// Ported from D:\Codes\EPOS\epos\apps\settings\app.h

#ifndef SETTINGS_APP_H
#define SETTINGS_APP_H

#include <lvgl.h>

struct SettingsModel;
struct SettingsView;
struct SettingsController;

typedef struct SettingsApp {
    struct SettingsModel      *model;
    struct SettingsView       *view;
    struct SettingsController *controller;
} SettingsApp;

extern SettingsApp g_settings_app;

// Register with app_manager. Call once at boot.
void settings_init(void);

#endif
