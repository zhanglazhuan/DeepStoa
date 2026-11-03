// view_general: one navigable page.
#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "esp_log.h"
#include "lv_page.h"
#include "lv_bottom_sheet.h"
#include "page_navigator.h"
#include "settings_app.h"
#include "settings_view.h"
#include "settings_controller.h"
#include "view_general.h"

static const char *TAG = "settings_general";

extern const lv_image_dsc_t ic_wallpaper;

extern const lv_image_dsc_t ic_timezone;

extern const lv_image_dsc_t ic_info;

extern const lv_image_dsc_t ic_reboot;

static void on_click_wallpaper(lv_event_t *e) {
    PAGE_NAVIGATE_TO((SettingsApp *)lv_event_get_user_data(e), PAGE_GENERAL_WALLPAPER, NULL);
}

static void on_click_datetime(lv_event_t *e) {
    PAGE_NAVIGATE_TO((SettingsApp *)lv_event_get_user_data(e), PAGE_GENERAL_DATETIME, NULL);
}

static void on_click_about(lv_event_t *e) {
    PAGE_NAVIGATE_TO((SettingsApp *)lv_event_get_user_data(e), PAGE_GENERAL_ABOUT, NULL);
}

static lv_obj_t *reboot_btn_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

static lv_obj_t *reboot_action_btn(lv_obj_t *parent, const char *text, bool primary,
                        lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    if (primary) ui_style_set_btn_primary(btn);
    else         ui_style_set_btn_secondary(btn);
    lv_obj_set_size(btn, UI_SHEET_BTN_W, UI_SHEET_BTN_H);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, LV_FONT_SMALL, 0);
    lv_obj_center(lbl);
    return btn;
}

static void reboot_cancel_cb(lv_event_t *e) {
    lv_bottom_sheet_close((lv_bottom_sheet_t *)lv_event_get_user_data(e));
}

static void reboot_confirm_cb(lv_event_t *e) {
    (void)e;
    ESP_LOGW(TAG, "user confirmed reboot");
    esp_restart();   /* 不返回，弹层不用收拾 */
}

static void on_click_reboot(lv_event_t *e) {
    (void)e;
    /* 挂 lv_layer_top：不随 screen 走，也盖得住状态栏 */
    lv_bottom_sheet_t *bs = lv_bottom_sheet_create(lv_layer_top());
    if (!bs) return;

    lv_bottom_sheet_add_header(bs, "Reboot device?");
    lv_obj_t *c = lv_bottom_sheet_get_content(bs);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 12, 0);
    lv_obj_t *message = lv_label_create(c);
    lv_obj_set_width(message, LV_PCT(100));
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_label_set_text(message, "The device will restart now.");
    /* 横排：Cancel 左、主按钮右 */
    lv_obj_t *row = reboot_btn_row(c);
    reboot_action_btn(row, "Cancel", false, reboot_cancel_cb,  bs);
    reboot_action_btn(row, "Reboot", true,  reboot_confirm_cb, NULL);
}

static lv_obj_t *build_general_page(struct SettingsApp *app) {
    Page page = lv_page_create("General", true, page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t *cont = page.container;
    lv_obj_t *list = lv_list_create(cont);
    lv_obj_set_width(list, LV_PCT(100));
    /* 列表只占三项的高度，后面的 Reboot 按钮紧跟 About。 */
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_t *w = lv_list_add_button(list, &ic_wallpaper, "Wallpaper");
    lv_obj_add_event_cb(w, on_click_wallpaper, LV_EVENT_CLICKED, app);
    lv_obj_t *dt = lv_list_add_button(list, &ic_timezone, "Date & Time");
    lv_obj_add_event_cb(dt, on_click_datetime, LV_EVENT_CLICKED, app);
    lv_obj_t *a = lv_list_add_button(list, &ic_info, "About");
    lv_obj_add_event_cb(a, on_click_about, LV_EVENT_CLICKED, app);
    lv_obj_t *rb = lv_button_create(cont);
    ui_style_set_btn_secondary(rb);
    lv_obj_set_size(rb, LV_PCT(100), UI_BTN_H_LARGE);
    lv_obj_set_style_shadow_width(rb, 0, 0);
    lv_obj_set_style_margin_top(rb, 16, 0);
    lv_obj_set_flex_flow(rb, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rb, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(rb, 8, 0);
    lv_obj_add_event_cb(rb, on_click_reboot, LV_EVENT_CLICKED, app);

    lv_obj_t *ri = lv_image_create(rb);
    lv_image_set_src(ri, &ic_reboot);
    lv_obj_t *rt = lv_label_create(rb);
    lv_label_set_text(rt, "Reboot");
    lv_obj_set_style_text_font(rt, LV_FONT_SMALL, 0);

    lv_obj_add_event_cb(page.screen, settings_controller_on_general_exit,
                        LV_EVENT_DELETE, app);
    return page.screen;
}

void settings_view_general_init_registry(struct SettingsApp *app)
{
    PAGE_REGISTE(app, PAGE_GENERAL, build_general_page);
}
