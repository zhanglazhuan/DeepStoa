// view_datetime: one navigable page.
#include <string.h>
#include <stdio.h>
#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "lv_page.h"
#include "page_navigator.h"
#include "settings_app.h"
#include "settings_model.h"
#include "settings_view.h"
#include "settings_controller.h"
#include "time_service.h"
#include "view_datetime.h"

static char *generate_timezone_options(void) {
    static char opts[2048];
    size_t used = 0;
    opts[0] = '\0';
    for (int i = 0; i < (int)epos_timezones_count; i++) {
        int n = snprintf(opts + used, sizeof(opts) - used, "%s%s",
                         epos_timezones[i].ui_label,
                         i < (int)epos_timezones_count - 1 ? "\n" : "");
        if (n < 0 || (size_t)n >= sizeof(opts) - used) {
            opts[sizeof(opts) - 1] = '\0';
            break;
        }
        used += (size_t)n;
    }
    return opts;
}

static void on_click_datetime_edit(lv_event_t *e) {
    PAGE_NAVIGATE_TO((SettingsApp *)lv_event_get_user_data(e), PAGE_GENERAL_DATETIME_EDIT, NULL);
}

static lv_obj_t *build_datetime_page(struct SettingsApp *app) {
    Page page = lv_page_create("Date & Time", true, page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t *cont = page.container;

    lv_obj_t *ld = lv_label_create(cont);
    lv_label_set_text(ld, "Date Type");
    lv_obj_t *dd_date = lv_dropdown_create(cont);
    lv_obj_set_width(dd_date, LV_PCT(100));
    ui_style_set_dropdown(dd_date);
    lv_dropdown_set_options(dd_date, "YYYY-MM-DD\nMM/DD/YYYY\nDD/MM/YYYY");
    if (app->model->datetime.date_type[0] != '\0') {
        lv_dropdown_set_selected(dd_date,
            strcmp(app->model->datetime.date_type, "YYYY-MM-DD") == 0 ? 0 :
            strcmp(app->model->datetime.date_type, "MM/DD/YYYY") == 0 ? 1 : 2);
    }
    lv_obj_add_event_cb(dd_date, settings_controller_set_date_type,
                        LV_EVENT_VALUE_CHANGED, app);

    lv_obj_t *lh = lv_label_create(cont);
    lv_label_set_text(lh, "Time Format");
    lv_obj_set_style_margin_top(lh, 16, 0);
    lv_obj_t *format_dd = lv_dropdown_create(cont);
    lv_obj_set_width(format_dd, LV_PCT(100));
    ui_style_set_dropdown(format_dd);
    lv_dropdown_set_options(format_dd, "24-hour\n12-hour");
    lv_dropdown_set_selected(format_dd, time_service_get_format_24h() ? 0 : 1);
    lv_obj_add_event_cb(format_dd, settings_controller_set_hour_24,
                        LV_EVENT_VALUE_CHANGED, app);

    lv_obj_t *ltz = lv_label_create(cont);
    lv_label_set_text(ltz, "Time Zone");
    lv_obj_set_style_margin_top(ltz, 16, 0);
    lv_obj_t *tz_dd = lv_dropdown_create(cont);
    lv_obj_set_width(tz_dd, LV_PCT(100));
    ui_style_set_dropdown(tz_dd);
    lv_obj_set_style_text_font(tz_dd, LV_FONT_SMALL, LV_PART_MAIN);
    lv_dropdown_set_options(tz_dd, generate_timezone_options());
    lv_obj_set_style_text_font(lv_dropdown_get_list(tz_dd), LV_FONT_SMALL, LV_PART_MAIN);
    uint16_t sel = 4; // default UTC+08:00
    if (app->model->datetime.timezone[0] != '\0') {
        for (uint16_t i = 0; i < epos_timezones_count; i++) {
            if (strcmp(app->model->datetime.timezone, epos_timezones[i].ui_label) == 0)
                { sel = i; break; }
        }
    }
    lv_dropdown_set_selected(tz_dd, sel);
    lv_obj_add_event_cb(tz_dd, settings_controller_set_timezone,
                        LV_EVENT_VALUE_CHANGED, app);

    lv_obj_t *edit_btn = lv_button_create(cont);
    ui_style_set_btn_secondary(edit_btn);
    lv_obj_set_width(edit_btn, LV_PCT(100));
    ui_style_set_btn_size_large(edit_btn);
    lv_obj_set_style_margin_top(edit_btn, 20, 0);
    lv_obj_t *edit_label = lv_label_create(edit_btn);
    lv_label_set_text(edit_label, "Set Date & Time");
    lv_obj_center(edit_label);
    lv_obj_add_event_cb(edit_btn, on_click_datetime_edit, LV_EVENT_CLICKED, app);

    return page.screen;
}

void settings_view_datetime_init_registry(struct SettingsApp *app)
{
    PAGE_REGISTE(app, PAGE_GENERAL_DATETIME, build_datetime_page);
}
