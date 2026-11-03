// view_wallpaper: one navigable page.
#include <string.h>
#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "lv_page.h"
#include "page_navigator.h"
#include "settings_app.h"
#include "settings_model.h"
#include "settings_view.h"
#include "settings_controller.h"
#include "view_wallpaper.h"

static void on_click_pick_folder(lv_event_t *e) {
    PAGE_NAVIGATE_TO((SettingsApp *)lv_event_get_user_data(e),
                     PAGE_GENERAL_FOLDER_SELECTOR, NULL);
}

static void wallpaper_lock_changed(lv_event_t *e) {
    lv_obj_t *dd = lv_event_get_target(e);
    lv_obj_t *slideshow_cont = (lv_obj_t *)lv_event_get_user_data(e);
    char sel[32];
    lv_dropdown_get_selected_str(dd, sel, sizeof(sel));
    if (strcmp(sel, "Slideshow") == 0)
        lv_obj_clear_flag(slideshow_cont, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(slideshow_cont, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *build_wallpaper_page(struct SettingsApp *app) {
    Page page = lv_page_create("Wallpaper", true, page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t *cont = page.container;

    lv_obj_t *lock_lbl = lv_label_create(cont);
    lv_label_set_text(lock_lbl, "Lock Screen");

    lv_obj_t *lock_dd = lv_dropdown_create(cont);
    lv_obj_set_width(lock_dd, LV_PCT(100));
    ui_style_set_dropdown(lock_dd);
    lv_dropdown_set_options(lock_dd, "Default\nDatetime\nSlideshow");

    lv_obj_t *slideshow_cont = lv_obj_create(cont);
    lv_obj_remove_style_all(slideshow_cont);
    lv_obj_set_width(slideshow_cont, LV_PCT(100));
    lv_obj_set_height(slideshow_cont, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(slideshow_cont, LV_FLEX_FLOW_COLUMN);

    bool is_slideshow = (strcmp(app->model->wallpaper.lock_screen_type, "Slideshow") == 0);
    if (!is_slideshow) lv_obj_add_flag(slideshow_cont, LV_OBJ_FLAG_HIDDEN);

    if (app->model->wallpaper.lock_screen_type[0] != '\0') {
        lv_dropdown_set_selected(lock_dd,
            strcmp(app->model->wallpaper.lock_screen_type, "Default")   == 0 ? 0 :
            strcmp(app->model->wallpaper.lock_screen_type, "Datetime")  == 0 ? 1 : 2);
    }
    lv_obj_add_event_cb(lock_dd, settings_controller_set_wallpaper_lock_screen,
                        LV_EVENT_VALUE_CHANGED, app);
    lv_obj_add_event_cb(lock_dd, wallpaper_lock_changed, LV_EVENT_VALUE_CHANGED, slideshow_cont);

    // Folder (stubbed — folder selector not yet ported)
    lv_obj_t *label_img = lv_label_create(slideshow_cont);
    lv_label_set_text(label_img, "Images");
    lv_obj_set_style_margin_top(label_img, 16, 0);

    /* 整行是个按钮，点了进文件夹选择器。原来这里是个只读 textarea 占位符，
     * 用户既填不进去也点不动。
     * 不用缓存这个 label：选完之后选择器会 pop 回来，本页会重新构建，
     * 直接从 model 读最新值。 */
    lv_obj_t *folder = lv_button_create(slideshow_cont);
    ui_style_set_btn_secondary(folder);
    lv_obj_set_size(folder, LV_PCT(100), UI_BTN_H_LARGE);
    lv_obj_set_style_shadow_width(folder, 0, 0);
    lv_obj_add_event_cb(folder, on_click_pick_folder, LV_EVENT_CLICKED, app);

    lv_obj_t *folder_lbl = lv_label_create(folder);
    lv_obj_set_width(folder_lbl, LV_PCT(100));
    /* 路径可能很长，从头截更没用 —— 用 DOT 从尾部省略，保住最有辨识度的开头 */
    lv_label_set_long_mode(folder_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(folder_lbl, LV_FONT_SMALL, 0);
    lv_label_set_text(folder_lbl,
                      app->model->wallpaper.wallpaper_folder[0] != '\0'
                          ? app->model->wallpaper.wallpaper_folder
                          : LV_SYMBOL_DIRECTORY "  Select image folder");
    lv_obj_align(folder_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    // Frequency
    lv_obj_t *label_freq = lv_label_create(slideshow_cont);
    lv_label_set_text(label_freq, "Frequency");
    lv_obj_set_style_margin_top(label_freq, 16, 0);

    lv_obj_t *freq_dd = lv_dropdown_create(slideshow_cont);
    lv_obj_set_width(freq_dd, LV_PCT(100));
    ui_style_set_dropdown(freq_dd);
    lv_dropdown_set_options(freq_dd, "5 minutes\n25 minutes\n60 minutes\n24 hours");
    if (app->model->wallpaper.frequency[0] != '\0') {
        const char *opts[] = {"5 minutes", "25 minutes", "60 minutes", "24 hours"};
        for (int i = 0; i < 4; i++)
            if (strcmp(app->model->wallpaper.frequency, opts[i]) == 0)
                { lv_dropdown_set_selected(freq_dd, i); break; }
    }
    lv_obj_add_event_cb(freq_dd, settings_controller_set_wallpaper_frequency,
                        LV_EVENT_VALUE_CHANGED, app);

    // Order
    lv_obj_t *label_order = lv_label_create(slideshow_cont);
    lv_label_set_text(label_order, "Order");
    lv_obj_set_style_margin_top(label_order, 16, 0);

    lv_obj_t *order_dd = lv_dropdown_create(slideshow_cont);
    lv_obj_set_width(order_dd, LV_PCT(100));
    ui_style_set_dropdown(order_dd);
    lv_dropdown_set_options(order_dd, "Random\nSequential");
    if (app->model->wallpaper.order[0] != '\0')
        lv_dropdown_set_selected(order_dd, strcmp(app->model->wallpaper.order, "Random") == 0 ? 0 : 1);
    lv_obj_add_event_cb(order_dd, settings_controller_set_wallpaper_order,
                        LV_EVENT_VALUE_CHANGED, app);

    return page.screen;
}

void settings_view_wallpaper_init_registry(struct SettingsApp *app)
{
    PAGE_REGISTE(app, PAGE_GENERAL_WALLPAPER, build_wallpaper_page);
}
