// apps/settings/subpages/view_storage.c
// Storage info page — Phase 1
// Ported from D:\Codes\EPOS\epos\apps\settings\subpages\view_storage.c

#include <lvgl.h>
#include "lv_page.h"
#include "page_navigator.h"
#include "settings_app.h"
#include "settings_model.h"
#include "settings_view.h"

static lv_obj_t *build_storage_page(struct SettingsApp *app) {
    Page page = lv_page_create("Storage", true,
                               page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t *cont = page.container;

    lv_obj_t *row = lv_obj_create(cont);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_margin_left(row, 12, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    lv_obj_t *title = lv_label_create(row);
    lv_label_set_text(title, "Disk space");

    char buf[64];
    lv_snprintf(buf, sizeof(buf), "Used: %d / %d GB",
                app->model->storage.storage_used_gb,
                app->model->storage.storage_total_gb);
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, buf);

    lv_obj_t *bar = lv_bar_create(row);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_bar_set_range(bar, 0, app->model->storage.storage_total_gb);
    lv_bar_set_value(bar, app->model->storage.storage_used_gb, LV_ANIM_OFF);

    return page.screen;
}

void settings_view_storage_init_registry(SettingsApp *app) {
    PAGE_REGISTE(app, PAGE_STORAGE, build_storage_page);
}
