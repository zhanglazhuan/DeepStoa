#ifndef WEATHER_MODEL_H
#define WEATHER_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#include "weather_service.h"

typedef enum {
    WEATHER_IDLE,
    WEATHER_LOADING,
    WEATHER_READY,
    WEATHER_OFFLINE,
    WEATHER_ERROR,
} weather_state_t;

typedef struct WeatherModel {
    weather_report_t report;
    weather_state_t state;
    uint8_t city_index;
    char ssid[33];
} WeatherModel;

bool weather_model_init(WeatherModel **out);
void weather_model_deinit(WeatherModel *model);
const char *weather_city_code(unsigned index);
const char *weather_city_options(void);

#endif
