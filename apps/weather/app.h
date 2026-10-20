#ifndef WEATHER_APP_H
#define WEATHER_APP_H

struct WeatherModel;
struct WeatherController;
struct WeatherView;

typedef struct WeatherApp {
    struct WeatherModel *model;
    struct WeatherController *controller;
    struct WeatherView *view;
} WeatherApp;

extern WeatherApp g_weather_app;

void weather_init(void);

#endif
