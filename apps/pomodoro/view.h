#ifndef POMODORO_VIEW_H
#define POMODORO_VIEW_H

#include <stdbool.h>
#include "framework/page_navigator.h"
#include "widgets/lv_pomodoro_panel.h"

struct PomodoroApp;

typedef struct PomodoroView {
    page_navigator_t page_nav;
    lv_pomodoro_panel_t panel;
} PomodoroView;

bool pomodoro_view_init(struct PomodoroApp *app);
void pomodoro_view_deinit(struct PomodoroApp *app);
void pomodoro_view_render(struct PomodoroApp *app);

#endif
