#include "lv_weather_panel.h"

#include <string.h>

#include "lv_theme_hardcore.h"
#include "lxj_weather_bmp.h"

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

void lv_weather_panel_create(lv_obj_t *parent, lv_weather_panel_t *panel,
                             const char *city_options)
{
    memset(panel, 0, sizeof(*panel));
    panel->root = lv_obj_create(parent);
    lv_obj_set_width(panel->root, LV_PCT(100));
    lv_obj_set_flex_grow(panel->root, 1);
    lv_obj_set_style_border_width(panel->root, 0, 0);
    lv_obj_set_flex_flow(panel->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel->root, 12, 0);

    lv_obj_t *toolbar = lv_obj_create(panel->root);
    lv_obj_set_width(toolbar, LV_PCT(100));
    lv_obj_set_height(toolbar, 64);
    lv_obj_set_style_border_width(toolbar, 0, 0);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    panel->city = lv_dropdown_create(toolbar);
    lv_dropdown_set_options(panel->city, city_options ? city_options : "");
    lv_obj_set_width(panel->city, 220);

    panel->refresh = lv_button_create(toolbar);
    lv_obj_set_size(panel->refresh, 150, 48);
    lv_obj_t *refresh_label = lv_label_create(panel->refresh);
    lv_label_set_text(refresh_label, "Refresh");
    lv_obj_center(refresh_label);

    lv_obj_t *body = lv_obj_create(panel->root);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);

    panel->status = lv_label_create(body);
    lv_obj_set_flex_grow(panel->status, 1);
    lv_label_set_long_mode(panel->status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(panel->status, LV_FONT_SMALL, 0);
    panel->image = lv_image_create(body);
    lv_image_set_src(panel->image, &clear);
    lv_obj_set_size(panel->image, 108, 108);
    lv_label_set_text(panel->status, "Choose a city and refresh");
}

void lv_weather_panel_set_refresh_cb(lv_weather_panel_t *panel,
                                      lv_event_cb_t callback, void *user_data)
{
    if (panel && panel->refresh && callback) {
        lv_obj_add_event_cb(panel->refresh, callback, LV_EVENT_CLICKED, user_data);
    }
}

unsigned lv_weather_panel_get_city(const lv_weather_panel_t *panel)
{
    return panel && panel->city ? lv_dropdown_get_selected(panel->city) : 0;
}

void lv_weather_panel_set_city(lv_weather_panel_t *panel, unsigned index)
{
    if (panel && panel->city) lv_dropdown_set_selected(panel->city, index);
}

void lv_weather_panel_render(lv_weather_panel_t *panel,
                             const lv_weather_display_t *data)
{
    if (!panel || !panel->status || !data) return;
    lv_obj_remove_state(panel->refresh, LV_STATE_DISABLED);
    lv_obj_remove_state(panel->city, LV_STATE_DISABLED);
    switch (data->state) {
    case LV_WEATHER_LOADING:
        lv_obj_add_state(panel->refresh, LV_STATE_DISABLED);
        lv_obj_add_state(panel->city, LV_STATE_DISABLED);
        lv_label_set_text(panel->status, "Fetching weather...");
        break;
    case LV_WEATHER_READY:
        lv_label_set_text_fmt(panel->status,
            "Wi-Fi: %s\n%s\nTemperature: %s C\nHumidity: %s\n"
            "Today: %s\nHigh/low: %s / %s\nAir quality: %s",
            data->ssid ? data->ssid : "", data->city ? data->city : "",
            data->temperature ? data->temperature : "",
            data->humidity ? data->humidity : "",
            data->type ? data->type : "", data->high ? data->high : "",
            data->low ? data->low : "", data->quality ? data->quality : "");
        lv_image_set_src(panel->image, weather_icon(data->type));
        break;
    case LV_WEATHER_OFFLINE:
        lv_label_set_text(panel->status, "Network offline\nConnect Wi-Fi before fetching weather");
        break;
    case LV_WEATHER_ERROR:
        lv_label_set_text_fmt(panel->status,
                              "Wi-Fi: %s\nWeather request failed\nSelect a city and retry",
                              data->ssid ? data->ssid : "");
        break;
    default:
        lv_label_set_text(panel->status, "Choose a city and refresh");
        break;
    }
}
