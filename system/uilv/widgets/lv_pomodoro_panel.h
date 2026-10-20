#ifndef LV_POMODORO_PANEL_H
#define LV_POMODORO_PANEL_H

#include <stdint.h>
#include "lvgl.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *time;
    lv_obj_t *state;
    lv_obj_t *toggle_label;
} lv_pomodoro_panel_t;

void lv_pomodoro_panel_create(lv_obj_t *parent, lv_pomodoro_panel_t *panel,
                              lv_event_cb_t toggle_cb, lv_event_cb_t reset_cb);
void lv_pomodoro_panel_render(lv_pomodoro_panel_t *panel, uint32_t remaining_s,
                              const char *state, const char *toggle_text);

#endif
