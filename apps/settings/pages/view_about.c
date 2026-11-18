// view_about: one navigable page.
#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "lv_page.h"
#include "lv_bottom_sheet.h"
#include "page_navigator.h"
#include "settings_app.h"
#include "settings_view.h"
#include "settings_controller.h"
#include "flash_control.h"
#include "ota.h"
#include "view_about.h"

static void on_confirm_reset(lv_event_t *e) {
    settings_controller_factory_reset((SettingsApp *)lv_event_get_user_data(e));
}

static void on_cancel_reset(lv_event_t *e) {
    lv_bottom_sheet_close((lv_bottom_sheet_t *)lv_event_get_user_data(e));
}

static void on_click_reset_factory(lv_event_t *e) {
    SettingsApp *app = lv_event_get_user_data(e);
    lv_bottom_sheet_t *sheet = lv_bottom_sheet_create(lv_screen_active());
    lv_bottom_sheet_add_header(sheet, "Factory Reset");

    lv_obj_t *content = lv_bottom_sheet_get_content(sheet);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(content, 12, 0);

    /* 说清楚"擦什么、留什么" —— 不可逆操作不该只说一句 Are you sure */
    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label,
        "Erases: Wi-Fi passwords, and every app's settings and history.\n"
        "Keeps: device ID and other factory data, and the installed firmware.\n\n"
        "This cannot be undone. The device will restart.");
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));

    lv_obj_t *idl = lv_label_create(content);
    lv_label_set_text_fmt(idl, "Device ID  %s", flash_sys_device_id());
    lv_obj_set_style_text_font(idl, LV_FONT_SMALL, 0);
    lv_obj_set_style_pad_bottom(idl, 8, 0);

    lv_obj_t *btn_row = lv_obj_create(content);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 12, 0);

    /* 取消放主按钮（实心），确认放次按钮 —— 危险操作不该是最顺手的那个。
     * 墨水屏没有颜色，靠"实心 vs 描边"传达默认选项。 */
    lv_obj_t *cancel_btn = lv_button_create(btn_row);
    ui_style_set_btn_primary(cancel_btn);
    lv_obj_set_flex_grow(cancel_btn, 1);
    ui_style_set_btn_size_large(cancel_btn);
    lv_obj_t *cl = lv_label_create(cancel_btn);
    lv_label_set_text(cl, "Cancel"); lv_obj_center(cl);
    lv_obj_add_event_cb(cancel_btn, on_cancel_reset, LV_EVENT_CLICKED, sheet);

    lv_obj_t *confirm_btn = lv_button_create(btn_row);
    ui_style_set_btn_secondary(confirm_btn);
    lv_obj_set_flex_grow(confirm_btn, 1);
    ui_style_set_btn_size_large(confirm_btn);
    lv_obj_t *cfl = lv_label_create(confirm_btn);
    lv_label_set_text(cfl, "Erase"); lv_obj_center(cfl);
    lv_obj_add_event_cb(confirm_btn, on_confirm_reset, LV_EVENT_CLICKED, app);
}

static lv_obj_t *build_about_page(struct SettingsApp *app) {
    Page page = lv_page_create("About", true, page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t *cont = page.container;
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *l1 = lv_label_create(cont);
    lv_label_set_text(l1, "DeepStoa Version");
    lv_obj_t *v1 = lv_label_create(cont);
    lv_label_set_text(v1, ota_running_version());   // version.txt -> PROJECT_VER
    lv_obj_set_style_pad_bottom(v1, 16, 0);

    /* 机器码来自 fdb_sys（厂家区），恢复出厂设置不会把它擦掉 */
    lv_obj_t *l2 = lv_label_create(cont);
    lv_label_set_text(l2, "Device ID");
    lv_obj_t *v2 = lv_label_create(cont);
    lv_label_set_text(v2, flash_sys_device_id());
    lv_obj_set_style_pad_bottom(v2, 32, 0);

    lv_obj_t *btn_w = lv_obj_create(cont);
    lv_obj_remove_style_all(btn_w);
    lv_obj_set_width(btn_w, LV_PCT(100));
    lv_obj_set_height(btn_w, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_w, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_w, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_margin_top(btn_w, 24, 0);

    /* 矩形按钮，样式全部来自 lv_ui_style_guide —— 不再手动改底色。
     * 原来那个红色圆钮在 1bit 墨水屏上没有意义：LV_PALETTE_RED 会被抖成
     * 一片灰或直接变黑，"危险色"完全传达不出来，反而因为是实心大圆按钮
     * 显得比取消更该点。改成描边矩形，危险性靠文案和二次确认表达。 */
    lv_obj_t *reset_btn = lv_button_create(btn_w);
    ui_style_set_btn_secondary(reset_btn);
    lv_obj_set_width(reset_btn, LV_PCT(100));
    ui_style_set_btn_size_large(reset_btn);
    lv_obj_t *rl = lv_label_create(reset_btn);
    lv_label_set_text(rl, "Factory Reset");
    lv_obj_center(rl);
    lv_obj_add_event_cb(reset_btn, on_click_reset_factory, LV_EVENT_CLICKED, app);

    return page.screen;
}

void settings_view_about_init_registry(struct SettingsApp *app)
{
    PAGE_REGISTE(app, PAGE_GENERAL_ABOUT, build_about_page);
}
