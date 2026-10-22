#ifndef POMODORO_MODEL_H
#define POMODORO_MODEL_H

#include <stdbool.h>
#include <stdint.h>
#include "pomodoro_service.h"

typedef struct PomodoroModel {
    pomodoro_state_t state;
    uint32_t remaining_s;
} PomodoroModel;

bool pomodoro_model_init(PomodoroModel **out);
void pomodoro_model_deinit(PomodoroModel *model);
bool pomodoro_model_sync(PomodoroModel *model);

#endif
