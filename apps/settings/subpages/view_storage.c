// apps/settings/subpages/view_storage.c
// SD card capacity from the mounted filesystem.

#include <lvgl.h>
#include "lv_page.h"
#include "lv_floating_button.h"
#include "page_navigator.h"
#include "settings_app.h"
#include "settings_view.h"
#include "fs_control.h"

LV_IMAGE_DECLARE(refresh);

static void refresh_storage(lv_obj_t *label, lv_obj_t *bar)
{
    uint64_t total = 0, used = 0;
    if (fs_control_is_mounted()) fs_control_get_usage(&total, &used);
    if (total == 0) {
        lv_label_set_text(label, fs_control_is_mounted()
                          ? "SD card capacity unavailable"
                          : "SD card not mounted");
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        return;
    }
    lv_label_set_text_fmt(label, "Used: %llu / %llu MiB",
                          (unsigned long long)(used / (1024U * 1024U)),
                          (unsigned long long)(total / (1024U * 1024U)));
    lv_bar_set_value(bar, (int)((double)used * 100.0 / (double)total),
                     LV_ANIM_OFF);
}

static void on_refresh(lv_event_t *event)
{
    lv_obj_t *bar = lv_event_get_user_data(event);
    refresh_storage(lv_obj_get_user_data(bar), bar);
}

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
    lv_label_set_text(title, "SD card");

    lv_obj_t *label = lv_label_create(row);

    lv_obj_t *bar = lv_bar_create(row);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_user_data(bar, label);
    refresh_storage(label, bar);

    lv_floating_button_create_image(cont, &refresh, on_refresh, bar);

    return page.screen;
}

void settings_view_storage_init_registry(SettingsApp *app) {
    PAGE_REGISTE(app, PAGE_STORAGE, build_storage_page);
}
