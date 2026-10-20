#ifndef WEATHER_CONTROLLER_H
#define WEATHER_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

struct WeatherApp;

typedef struct WeatherController {
    lv_timer_t *poll_timer;
    uint32_t pending_token;
    bool busy;
} WeatherController;

bool weather_controller_init(struct WeatherApp *app);
void weather_controller_deinit(struct WeatherApp *app);
void weather_controller_start(struct WeatherApp *app);
void weather_controller_refresh(lv_event_t *event);
void weather_controller_close(lv_event_t *event);

#endif
