#pragma once

#include <lvgl.h>

/* Passcode field construction shared by creation and verification pages. */
lv_obj_t *settings_passcode_input_create(lv_obj_t *parent, const char *title_text);

struct SettingsApp;
void settings_view_create_passcode_init_registry(struct SettingsApp *app);
