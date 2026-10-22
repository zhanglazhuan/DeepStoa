#include "view.h"

#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "controller.h"
#include "model.h"
#include "lv_floating_button.h"
#include "lv_theme_hardcore.h"
#include "lxj_weather_bmp.h"
#include "widgets/lv_page.h"

LV_IMAGE_DECLARE(refresh);

static const lv_image_dsc_t *weather_icon(const char *type)
{
    if (!type) return &clear;
    if (strstr(type, "雷") || strstr(type, "thunder")) return &thunderstorm;
    if (strstr(type, "暴") || strstr(type, "storm")) return &rainstorm;
    if (strstr(type, "大雨") || strstr(type, "heavy")) return &heavy_rain;
    if (strstr(type, "中雨") || strstr(type, "moderate")) return &moderate_rain;
    if (strstr(type, "小雨") || strstr(type, "light")) return &light_rain;
    if (strstr(type, "雨") || strstr(type, "rain")) return &light_rain;
    if (strstr(type, "雪") || strstr(type, "snow")) return &snow;
    if (strstr(type, "雾") || strstr(type, "霾") || strstr(type, "haze")) return &haze;
    if (strstr(type, "沙") || strstr(type, "dust")) return &dust_storm;
    if (strstr(type, "风") || strstr(type, "gale")) return &gale;
    if (strstr(type, "阴") || strstr(type, "overcast")) return &overcast;
    if (strstr(type, "云") || strstr(type, "cloud")) return &partly_cloudy;
    return &clear;
}

static lv_obj_t *build_main_page(void *handle, void *user_data)
{
    (void)user_data;
    WeatherApp *app = handle;
    Page page = lv_page_create("Weather", false, NULL, NULL);
    WeatherView *view = app->view;

    view->root = lv_obj_create(page.container);
    lv_obj_set_width(view->root, LV_PCT(100));
    lv_obj_set_flex_grow(view->root, 1);
    lv_obj_set_style_border_width(view->root, 0, 0);
    lv_obj_set_flex_flow(view->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(view->root, 12, 0);

    lv_obj_t *toolbar = lv_obj_create(view->root);
    lv_obj_set_width(toolbar, LV_PCT(100));
    lv_obj_set_height(toolbar, 64);
    lv_obj_set_style_border_width(toolbar, 0, 0);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    view->city = lv_dropdown_create(toolbar);
    lv_dropdown_set_options(view->city, weather_city_options());
    lv_obj_set_width(view->city, 220);
    lv_dropdown_set_selected(view->city, app->model->city_index);

    lv_obj_t *body = lv_obj_create(view->root);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);

    view->status = lv_label_create(body);
    lv_obj_set_flex_grow(view->status, 1);
    lv_label_set_long_mode(view->status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(view->status, LV_FONT_SMALL, 0);
    view->image = lv_image_create(body);
    lv_image_set_src(view->image, &clear);
    lv_obj_set_size(view->image, 108, 108);
    lv_label_set_text(view->status, "Choose a city and refresh");

    view->refresh = lv_floating_button_create_image(view->root, &refresh,
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
    return app && app->view && app->view->city
        ? lv_dropdown_get_selected(app->view->city) : 0;
}

void weather_view_render(WeatherApp *app)
{
    if (!app || !app->view || !app->model || !app->view->root) return;
    WeatherView *view = app->view;
    const WeatherModel *model = app->model;
    const weather_report_t *report = &model->report;
    lv_obj_remove_state(view->refresh, LV_STATE_DISABLED);
    lv_obj_remove_state(view->city, LV_STATE_DISABLED);
    switch (model->state) {
    case WEATHER_LOADING:
        lv_obj_add_state(view->refresh, LV_STATE_DISABLED);
        lv_obj_add_state(view->city, LV_STATE_DISABLED);
        lv_label_set_text(view->status, "Fetching weather...");
        break;
    case WEATHER_READY:
        lv_label_set_text_fmt(view->status,
            "Wi-Fi: %s\n%s\nTemperature: %s C\nHumidity: %s\n"
            "Today: %s\nHigh/low: %s / %s\nAir quality: %s",
            model->ssid, report->city, report->temperature,
            report->humidity, report->type, report->high,
            report->low, report->quality);
        lv_image_set_src(view->image, weather_icon(report->type));
        break;
    case WEATHER_OFFLINE:
        lv_label_set_text(view->status, "Network offline\nConnect Wi-Fi before fetching weather");
        break;
    case WEATHER_ERROR:
        lv_label_set_text_fmt(view->status,
                              "Wi-Fi: %s\nWeather request failed\nSelect a city and retry",
                              model->ssid);
        break;
    default:
        lv_label_set_text(view->status, "Choose a city and refresh");
        break;
    }
}
