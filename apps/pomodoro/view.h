#ifndef POMODORO_VIEW_H
#define POMODORO_VIEW_H

#include <stdbool.h>
#include "framework/page_navigator.h"

struct PomodoroApp;

typedef struct PomodoroView {
    page_navigator_t page_nav;
    lv_obj_t *root;
    lv_obj_t *time;
    lv_obj_t *state;
    lv_obj_t *toggle_label;
} PomodoroView;

bool pomodoro_view_init(struct PomodoroApp *app);
void pomodoro_view_deinit(struct PomodoroApp *app);
void pomodoro_view_render(struct PomodoroApp *app);

#endif
