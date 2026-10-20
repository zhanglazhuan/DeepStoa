#ifndef SYS_BATTERY_H
#define SYS_BATTERY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Init ADC + CHAG, take one sample, and start the 5-minute periodic sampler.
 * Fires APP_EVENT_BATTERY_CHANGED when the status-bar icon bucket or charging
 * state changes. Call after LVGL + the status bar are initialized. */
void battery_init(void);

/** Optional board-provided battery gauge backend. Both callbacks are required. */
typedef struct {
    bool (*read_voltage_mv)(int *out_mv, void *user_data);
    bool (*read_charging)(bool *out_charging, void *user_data);
    void *user_data;
} battery_backend_t;

/** Register before battery_init(); no I2C or GPIO is created here. */
void battery_set_backend(const battery_backend_t *backend);

/* Last quantized percent 0..100 (-1 before the first sample). */
int  battery_get_percent(void);

/* Last charging state (CHAG low). */
bool battery_is_charging(void);

#ifdef __cplusplus
}
#endif

#endif /* SYS_BATTERY_H */
