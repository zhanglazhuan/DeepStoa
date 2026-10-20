#include "controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_manager.h"
#include "app.h"
#include "model.h"
#include "view.h"
#include "weather_service.h"
#include "wifi_manager.h"

static uint32_t s_next_token;

static void request_weather(WeatherApp *app)
{
    if (!app || !app->model || !app->controller || app->controller->busy) return;
    WeatherModel *model = app->model;
    unsigned selected = weather_view_selected_city(app);
    const char *code = weather_city_code(selected);
    if (!code) return;
    model->city_index = (uint8_t)selected;

    wifi_info_t wifi = {0};
    if (wifi_get_info(&wifi) != ESP_OK || !wifi.is_connected) {
        model->state = WEATHER_OFFLINE;
        weather_view_render(app);
        return;
    }
    snprintf(model->ssid, sizeof(model->ssid), "%s", wifi.ssid);
    uint32_t token = ++s_next_token;
    if (token == 0) token = ++s_next_token;
    if (!weather_service_request(code, token)) {
        model->state = WEATHER_ERROR;
        weather_view_render(app);
        return;
    }
    app->controller->pending_token = token;
    app->controller->busy = true;
    model->state = WEATHER_LOADING;
    weather_view_render(app);
}

static void poll_result(lv_timer_t *timer)
{
    WeatherApp *app = lv_timer_get_user_data(timer);
    weather_result_t result;
    while (weather_service_try_take_result(&result)) {
        if (!app || !app->model || !app->controller ||
            !app->controller->busy ||
            result.token != app->controller->pending_token) continue;
        app->controller->busy = false;
        if (!wifi_is_connected()) {
            app->model->state = WEATHER_OFFLINE;
        } else if (result.success) {
            app->model->report = result.report;
            app->model->state = WEATHER_READY;
        } else {
            app->model->state = WEATHER_ERROR;
        }
        weather_view_render(app);
    }
}

bool weather_controller_init(WeatherApp *app)
{
    if (!app || !weather_service_init()) return false;
    app->controller = calloc(1, sizeof(WeatherController));
    return app->controller != NULL;
}

void weather_controller_deinit(WeatherApp *app)
{
    if (!app || !app->controller) return;
    if (app->controller->poll_timer) lv_timer_delete(app->controller->poll_timer);
    free(app->controller);
    app->controller = NULL;
}

void weather_controller_start(WeatherApp *app)
{
    if (!app || !app->controller) return;
    app->controller->poll_timer = lv_timer_create(poll_result, 200, app);
    if (!app->controller->poll_timer) {
        app->model->state = WEATHER_ERROR;
        weather_view_render(app);
        return;
    }
    request_weather(app);
}

void weather_controller_refresh(lv_event_t *event)
{
    (void)event;
    request_weather(&g_weather_app);
}

void weather_controller_close(lv_event_t *event)
{
    (void)event;
    app_manager_exit_app();
}
