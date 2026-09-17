// apps/settings/subpages/view_home.c
// Settings home menu — Phase 1 (WiFi/BT buttons disabled)
// Ported from D:\Codes\EPOS\epos\apps\settings\subpages\view_home.c

#include <lvgl.h>
#include "esp_log.h"
#include "lv_page.h"
#include "page_navigator.h"
#include "settings_app.h"
#include "settings_model.h"
#include "settings_view.h"
#include "settings_controller.h"

static const char *TAG = "settings_home";

/* ── 48×48 image icons (Google Material Symbols via LVGLImage.py) ── */
extern const lv_image_dsc_t ic_settings;
extern const lv_image_dsc_t ic_battery_android_6;
extern const lv_image_dsc_t ic_home_storage;
extern const lv_image_dsc_t ic_admin_panel_settings;

static void on_click_general(lv_event_t *e) {
    PAGE_NAVIGATE_TO((SettingsApp *)lv_event_get_user_data(e), PAGE_GENERAL, NULL);
}
static void on_click_wifi(lv_event_t *e) {
    PAGE_NAVIGATE_TO((SettingsApp *)lv_event_get_user_data(e), PAGE_WIFI, NULL);
}
static void on_click_battery(lv_event_t *e) {
    PAGE_NAVIGATE_TO((SettingsApp *)lv_event_get_user_data(e), PAGE_BATTERY, NULL);
}
static void on_click_storage(lv_event_t *e) {
    PAGE_NAVIGATE_TO((SettingsApp *)lv_event_get_user_data(e), PAGE_STORAGE, NULL);
}
static void on_click_security(lv_event_t *e) {
    PAGE_NAVIGATE_TO((SettingsApp *)lv_event_get_user_data(e), PAGE_SECURITY, NULL);
}
static void on_click_updates(lv_event_t *e) {
    PAGE_NAVIGATE_TO((SettingsApp *)lv_event_get_user_data(e), PAGE_UPDATES, NULL);
}

static lv_obj_t *build_home_page(struct SettingsApp *app) {
    Page page = lv_page_create("Settings", false,
                               page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t *cont = page.container;
    lv_obj_t *list = lv_list_create(cont);

    /* Break top-level flex collapse: container's cross-axis doesn't stretch,
     * so list shrink-wraps. Force full width so LV_PCT(100%) resolves correctly. */
    lv_obj_set_width(list, LV_PCT(100));

    /* 高度必须显式给。lv_list_class 的默认高是 LV_DPI_DEF*2 = 260px，
     * 而 6 个 item（每个 44px + 4px 行距）要 284px —— 差这一点点就够让列表
     * 变成内部可滚动的，一眼看过去只有四五项，前面的被滚到可视区外面去，
     * 同时容器下方 450px 白白空着。
     * 容器是 flex column，flex_grow 让列表吃满剩余高度，所有项一屏排下，
     * 也不会因为以后加减项又变成可滚动。 */
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_t *b1 = lv_list_add_button(list, &ic_settings, "General");
    lv_obj_add_event_cb(b1, on_click_general, LV_EVENT_CLICKED, app);

    lv_obj_t *b2 = lv_list_add_button(list, &ic_settings, "Wi-Fi");
    lv_obj_add_event_cb(b2, on_click_wifi, LV_EVENT_CLICKED, app);

    lv_obj_t *b4 = lv_list_add_button(list, &ic_battery_android_6, "Battery");
    lv_obj_add_event_cb(b4, on_click_battery, LV_EVENT_CLICKED, app);

    lv_obj_t *b5 = lv_list_add_button(list, &ic_home_storage, "Storage");
    lv_obj_add_event_cb(b5, on_click_storage, LV_EVENT_CLICKED, app);

    lv_obj_t *b6 = lv_list_add_button(list, &ic_admin_panel_settings, "Security");
    lv_obj_add_event_cb(b6, on_click_security, LV_EVENT_CLICKED, app);

    lv_obj_t *b7 = lv_list_add_button(list, &ic_settings, "Update");
    lv_obj_add_event_cb(b7, on_click_updates, LV_EVENT_CLICKED, app);

    ESP_LOGI(TAG, "Home page built");
    return page.screen;
}

void settings_view_home_init_registry(SettingsApp *app) {
    PAGE_REGISTE(app, PAGE_HOME, build_home_page);
}
