#include "app.h"

#include <string.h>

#include "app_manager.h"
#include "esp_log.h"
#include "controller.h"
#include "model.h"
#include "view.h"

static const char *TAG = "weather";
WeatherApp g_weather_app;

static void weather_start(lv_obj_t *root, lv_group_t *group)
{
    (void)group;
    WeatherApp *app = &g_weather_app;
    if (!weather_model_init(&app->model) ||
        !weather_controller_init(app) ||
        !weather_view_init(app)) {
        ESP_LOGE(TAG, "cannot start weather app");
        weather_view_deinit(app);
        weather_controller_deinit(app);
        weather_model_deinit(app->model);
        memset(app, 0, sizeof(*app));
        if (root) {
            lv_obj_t *label = lv_label_create(root);
            lv_label_set_text(label, "Unable to open Weather");
            lv_obj_center(label);
        }
        return;
    }
    page_navigator_navigate_to(&app->view->page_nav, app,
                               WEATHER_PAGE_MAIN, NULL);
    weather_controller_start(app);
}

static void weather_stop(void)
{
    WeatherApp *app = &g_weather_app;
    weather_controller_deinit(app);
    weather_view_deinit(app);
    weather_model_deinit(app->model);
    memset(app, 0, sizeof(*app));
}

static bool weather_back(void)
{
    WeatherApp *app = &g_weather_app;
    return app->view && page_navigator_navigate_pop(&app->view->page_nav, app);
}

static application_t s_application = {
    .name = "Weather",
    .icon = NULL,
    .start_func = weather_start,
    .stop_func = weather_stop,
    .back_func = weather_back,
    .category = APP_CATEGORY_SENSORS,
};

void weather_init(void)
{
    app_manager_add_application(&s_application);
    ESP_LOGI(TAG, "Weather app registered");
}
