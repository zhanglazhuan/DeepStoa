#ifndef DEEPSTOA_LV_WEATHER_PANEL_H
#define DEEPSTOA_LV_WEATHER_PANEL_H

#include "lvgl.h"

typedef enum {
    LV_WEATHER_IDLE,
    LV_WEATHER_LOADING,
    LV_WEATHER_READY,
    LV_WEATHER_OFFLINE,
    LV_WEATHER_ERROR,
} lv_weather_state_t;

typedef struct {
    lv_weather_state_t state;
    const char *ssid;
    const char *city;
    const char *type;
    const char *temperature;
    const char *humidity;
    const char *quality;
    const char *high;
    const char *low;
} lv_weather_display_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *city;
    lv_obj_t *refresh;
    lv_obj_t *status;
    lv_obj_t *image;
} lv_weather_panel_t;

void lv_weather_panel_create(lv_obj_t *parent, lv_weather_panel_t *panel,
                             const char *city_options);
void lv_weather_panel_set_refresh_cb(lv_weather_panel_t *panel,
                                      lv_event_cb_t callback, void *user_data);
unsigned lv_weather_panel_get_city(const lv_weather_panel_t *panel);
void lv_weather_panel_set_city(lv_weather_panel_t *panel, unsigned index);
void lv_weather_panel_render(lv_weather_panel_t *panel,
                             const lv_weather_display_t *data);

#endif
