#ifndef POMODORO_CONTROLLER_H
#define POMODORO_CONTROLLER_H

#include <stdbool.h>
#include "lvgl.h"

struct PomodoroApp;

typedef struct PomodoroController {
    lv_timer_t *poll_timer;
} PomodoroController;

bool pomodoro_controller_init(struct PomodoroApp *app);
void pomodoro_controller_deinit(struct PomodoroApp *app);
void pomodoro_controller_toggle(lv_event_t *event);
void pomodoro_controller_reset(lv_event_t *event);
void pomodoro_controller_close(lv_event_t *event);

#endif
