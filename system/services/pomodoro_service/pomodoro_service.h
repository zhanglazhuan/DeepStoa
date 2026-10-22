#ifndef POMODORO_SERVICE_H
#define POMODORO_SERVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POMODORO_DURATION_S (25U * 60U)

typedef enum {
    POMODORO_READY,
    POMODORO_RUNNING,
    POMODORO_PAUSED,
    POMODORO_FINISHED,
} pomodoro_state_t;

/* All calls run on the LVGL thread. State survives closing the app. */
void pomodoro_service_init(void);
void pomodoro_service_toggle(void);
void pomodoro_service_reset(void);
pomodoro_state_t pomodoro_service_state(void);
uint32_t pomodoro_service_remaining_s(void);

#ifdef __cplusplus
}
#endif

#endif
