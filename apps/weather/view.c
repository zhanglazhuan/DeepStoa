#include "view.h"

#include <stdlib.h>

#include "app.h"
#include "controller.h"
#include "model.h"
#include "widgets/lv_page.h"

static lv_weather_state_t panel_state(weather_state_t state)
{
    switch (state) {
    case WEATHER_LOADING: return LV_WEATHER_LOADING;
    case WEATHER_READY: return LV_WEATHER_READY;
    case WEATHER_OFFLINE: return LV_WEATHER_OFFLINE;
    case WEATHER_ERROR: return LV_WEATHER_ERROR;
    default: return LV_WEATHER_IDLE;
    }
}

static lv_obj_t *build_main_page(void *handle, void *user_data)
{
    (void)user_data;
    WeatherApp *app = handle;
    Page page = lv_page_create("Weather", false, NULL, NULL);
    lv_obj_t *close = lv_button_create(page.header_right);
    lv_obj_set_size(close, 40, 40);
    lv_obj_add_event_cb(close, weather_controller_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(close);
    lv_label_set_text(label, "X");
    lv_obj_center(label);

    lv_weather_panel_create(page.container, &app->view->panel,
                            weather_city_options());
    lv_weather_panel_set_city(&app->view->panel, app->model->city_index);
    lv_weather_panel_set_refresh_cb(&app->view->panel,
                                     weather_controller_refresh, NULL);
    weather_view_render(app);
    return page.screen;
}

bool weather_view_init(WeatherApp *app)
{
    if (!app) return false;
    app->view = calloc(1, sizeof(WeatherView));
    if (!app->view) return false;
    page_navigator_page_t *registry = calloc(1, sizeof(*registry));
    if (!registry) {
        free(app->view);
        app->view = NULL;
        return false;
    }
    page_navigator_init(&app->view->page_nav, registry, 1, app);
    page_navigator_register_page(&app->view->page_nav,
                                  WEATHER_PAGE_MAIN, build_main_page);
    return true;
}

void weather_view_deinit(WeatherApp *app)
{
    if (!app || !app->view) return;
    free(app->view->page_nav.registry);
    page_navigator_deinit(&app->view->page_nav);
    free(app->view);
    app->view = NULL;
}

unsigned weather_view_selected_city(WeatherApp *app)
{
    return app && app->view ? lv_weather_panel_get_city(&app->view->panel) : 0;
}

void weather_view_render(WeatherApp *app)
{
    if (!app || !app->view || !app->model || !app->view->panel.root) return;
    const WeatherModel *model = app->model;
    const weather_report_t *report = &model->report;
    lv_weather_display_t data = {
        .state = panel_state(model->state),
        .ssid = model->ssid,
        .city = report->city,
        .type = report->type,
        .temperature = report->temperature,
        .humidity = report->humidity,
        .quality = report->quality,
        .high = report->high,
        .low = report->low,
    };
    lv_weather_panel_render(&app->view->panel, &data);
}
