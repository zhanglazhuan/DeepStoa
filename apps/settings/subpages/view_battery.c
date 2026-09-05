// apps/settings/subpages/view_battery.c
// Battery settings page — Phase 1
// Ported from D:\Codes\EPOS\epos\apps\settings\subpages\view_battery.c

#include <lvgl.h>
#include "lv_page.h"
#include "page_navigator.h"
#include "settings_app.h"
#include "settings_model.h"
#include "settings_view.h"
#include "settings_controller.h"
#include "lv_ui_style_guide.h"

static lv_obj_t *build_battery_page(struct SettingsApp *app) {
    Page page = lv_page_create("Battery", true,
                               page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t *cont = page.container;
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(cont, 16, 0);

    // Percentage row —— 标题在上、控件在下，与 Update 页一致
    lv_obj_t *pr = lv_obj_create(cont);
    lv_obj_remove_style_all(pr);
    lv_obj_set_width(pr, LV_PCT(100));
    lv_obj_set_height(pr, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(pr, 8, 0);

    lv_obj_t *pl = lv_label_create(pr);
    lv_label_set_text(pl, "Battery %");

    lv_obj_t *ps = lv_switch_create(pr);
    ui_style_set_switch(ps);
    if (app->model->battery.battery_percentage_enabled)
        lv_obj_add_state(ps, LV_STATE_CHECKED);
    lv_obj_add_event_cb(ps, settings_controller_set_battery_percentage,
                        LV_EVENT_VALUE_CHANGED, app);

    // Sleep row
    lv_obj_t *sr = lv_obj_create(cont);
    lv_obj_remove_style_all(sr);
    lv_obj_set_width(sr, LV_PCT(100));
    lv_obj_set_height(sr, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sr, 8, 0);

    lv_obj_t *sl = lv_label_create(sr);
    lv_label_set_text(sl, "Auto Sleep");

    lv_obj_t *sd = lv_dropdown_create(sr);
    lv_dropdown_set_options(sd, "5 min\n15 min\n30 min");
    int sel = (app->model->battery.auto_sleep_minutes == 5)  ? 0 :
              (app->model->battery.auto_sleep_minutes == 15) ? 1 : 2;
    lv_dropdown_set_selected(sd, sel);
    lv_obj_add_event_cb(sd, settings_controller_set_auto_sleep,
                        LV_EVENT_VALUE_CHANGED, app);

    lv_obj_add_event_cb(page.screen, settings_controller_on_battery_exit,
                        LV_EVENT_DELETE, app);
    return page.screen;
}

void settings_view_battery_init_registry(SettingsApp *app) {
    PAGE_REGISTE(app, PAGE_BATTERY, build_battery_page);
}
