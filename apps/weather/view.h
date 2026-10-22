#ifndef WEATHER_VIEW_H
#define WEATHER_VIEW_H

#include <stdbool.h>
#include "framework/page_navigator.h"

struct WeatherApp;

enum {
    WEATHER_PAGE_NONE = 0,
    WEATHER_PAGE_MAIN,
};

typedef struct WeatherView {
    page_navigator_t page_nav;
    lv_obj_t *root;
    lv_obj_t *city;
    lv_obj_t *refresh;
    lv_obj_t *status;
    lv_obj_t *image;
} WeatherView;

bool weather_view_init(struct WeatherApp *app);
void weather_view_deinit(struct WeatherApp *app);
void weather_view_render(struct WeatherApp *app);
unsigned weather_view_selected_city(struct WeatherApp *app);

#endif
