#ifndef DEEPSTOA_WEATHER_SERVICE_H
#define DEEPSTOA_WEATHER_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char city[48];
    char type[32];
    char temperature[16];
    char humidity[16];
    char quality[32];
    char high[16];
    char low[16];
} weather_report_t;

typedef struct {
    uint32_t token;
    bool success;
    weather_report_t report;
} weather_result_t;

/* Initializes one background worker. Safe to call on each app launch. */
bool weather_service_init(void);

/* Queues a bounded HTTP request; returns immediately. */
bool weather_service_request(const char *city_code, uint32_t token);

/* Nonblocking result read, intended for an app's LVGL timer. */
bool weather_service_try_take_result(weather_result_t *out);

#endif
