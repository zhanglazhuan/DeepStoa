#include "model.h"

#include <stdlib.h>

bool pomodoro_model_init(PomodoroModel **out)
{
    if (!out) return false;
    pomodoro_service_init();
    *out = calloc(1, sizeof(**out));
    if (!*out) return false;
    pomodoro_model_sync(*out);
    return true;
}

void pomodoro_model_deinit(PomodoroModel *model)
{
    free(model);
}

bool pomodoro_model_sync(PomodoroModel *model)
{
    if (!model) return false;
    pomodoro_state_t state = pomodoro_service_state();
    uint32_t remaining = pomodoro_service_remaining_s();
    bool changed = model->state != state || model->remaining_s != remaining;
    model->state = state;
    model->remaining_s = remaining;
    return changed;
}
