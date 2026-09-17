// apps/settings/subpages/view_general.c
// General settings — wallpaper + datetime + about (Phase 1)
// Folder selector deferred to Phase 2 (needs filesystem port)
// Ported from D:\Codes\EPOS\epos\apps\settings\subpages\view_general.c

#include <string.h>
#include <stdio.h>
#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "esp_log.h"
#include "lv_page.h"
#include "lv_bottom_sheet.h"
#include "lv_toast.h"
#include "lv_folder_selector.h"
#include "page_navigator.h"
#include "settings_app.h"
#include "settings_model.h"
#include "settings_view.h"
#include "settings_controller.h"
#include "flash_control.h"
#include "ota.h"

static const char *TAG = "settings_general";

// ── Timezone option string builder ─────────────────────────────────────

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

// ── Navigation handlers ────────────────────────────────────────────────

static void on_click_wallpaper(lv_event_t *e) {
    PAGE_NAVIGATE_TO((SettingsApp *)lv_event_get_user_data(e), PAGE_GENERAL_WALLPAPER, NULL);
}
static void on_click_datetime(lv_event_t *e) {
    PAGE_NAVIGATE_TO((SettingsApp *)lv_event_get_user_data(e), PAGE_GENERAL_DATETIME, NULL);
}
static void on_click_about(lv_event_t *e) {
    PAGE_NAVIGATE_TO((SettingsApp *)lv_event_get_user_data(e), PAGE_GENERAL_ABOUT, NULL);
}

// ── Reboot：二次确认（lv_bottom_sheet）─────────────────────────────────

/* sheet 里的动作按钮：primary = 实心（危险/主动作），否则描边。
 * 并排放在按钮行里，尺寸见 UI_SHEET_BTN_*。 */
/* 弹层的动作按钮行：横排，Cancel 在左、主按钮在右。
 * SPACE_EVENLY 让两枚朝中间收，两侧留出边距，不贴屏幕边。
 * 尺寸/约定见 lv_ui_style_guide.h 的 UI_SHEET_BTN_*。 */
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

/* 建弹层壳子：标题 + 正文，返回内容区供调用方接着加按钮。
 * 刻意不调 lv_bottom_sheet_add_header() —— 它自带关闭 ×，会变成第三个出口，
 * 用户还得先分辨"哪个是取消"。点遮罩关闭是 widget 自带的无害默认退出。 */
static lv_obj_t *reboot_sheet_body(lv_bottom_sheet_t *bs, const char *title, const char *message)
{
    lv_obj_t *c = lv_bottom_sheet_get_content(bs);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c, 12, 0);

    lv_obj_t *t = lv_label_create(c);
    lv_obj_set_width(t, LV_PCT(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(t, LV_FONT_NORMAL, 0);
    lv_label_set_text(t, title);

    lv_obj_t *m = lv_label_create(c);
    lv_obj_set_width(m, LV_PCT(100));
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(m, LV_FONT_SMALL, 0);
    lv_obj_set_style_pad_bottom(m, 8, 0);
    lv_label_set_text(m, message);
    return c;
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

    lv_obj_t *c = reboot_sheet_body(bs, "Reboot device?",
                                    "The device will restart now.");
    /* 横排：Cancel 左、主按钮右 */
    lv_obj_t *row = reboot_btn_row(c);
    reboot_action_btn(row, "Cancel", false, reboot_cancel_cb,  bs);
    reboot_action_btn(row, "Reboot", true,  reboot_confirm_cb, NULL);
}

// ── Build: General (list) page ─────────────────────────────────────────

static lv_obj_t *build_general_page(struct SettingsApp *app) {
    Page page = lv_page_create("General", true, page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t *cont = page.container;
    lv_obj_t *list = lv_list_create(cont);
    lv_obj_set_width(list, LV_PCT(100));
    /* 同 view_home.c：不给高度就会用 lv_list_class 的默认 260px，
     * 项一多就在列表内部滚动，而容器下面大片空着。 */
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_t *w = lv_list_add_button(list, LV_SYMBOL_IMAGE, "Wallpaper");
    lv_obj_add_event_cb(w, on_click_wallpaper, LV_EVENT_CLICKED, app);
    lv_obj_t *dt = lv_list_add_button(list, LV_SYMBOL_SETTINGS, "Date & Time");
    lv_obj_add_event_cb(dt, on_click_datetime, LV_EVENT_CLICKED, app);
    lv_obj_t *a = lv_list_add_button(list, LV_SYMBOL_IMAGE, "About");
    lv_obj_add_event_cb(a, on_click_about, LV_EVENT_CLICKED, app);

    /* ── 底部：Reboot ──────────────────────────────────────────────────
     * 原来是两个 120x120 的圆形按钮（Reboot / Shutdown）。改成 style guide
     * 里的矩形描边按钮，和全 app 其它按钮统一。
     *
     * Shutdown 去掉了，理由见 settings_controller_shutdown() 上的注释：
     * 这块硬件没有真正的关机，那个按钮按下去等于把设备变砖到下次物理复位，
     * 而墨水屏还停在原画面，用户看不出发生了什么。要恢复它的话，先解决
     * 唤醒源的问题，再在这里加回一个同样样式的按钮即可。 */
    lv_obj_t *bottom = lv_obj_create(page.screen);
    lv_obj_remove_style_all(bottom);
    lv_obj_set_width(bottom, LV_PCT(100));
    lv_obj_set_height(bottom, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(bottom, 16, 0);

    lv_obj_t *rb = lv_button_create(bottom);
    ui_style_set_btn_secondary(rb);          /* 透明底 + 2px 黑边，矩形 */
    lv_obj_set_size(rb, LV_PCT(100), UI_BTN_H_LARGE);
    lv_obj_set_style_shadow_width(rb, 0, 0);
    lv_obj_add_event_cb(rb, on_click_reboot, LV_EVENT_CLICKED, app);

    lv_obj_t *rt = lv_label_create(rb);
    lv_label_set_text(rt, LV_SYMBOL_REFRESH "  Reboot");
    lv_obj_set_style_text_font(rt, LV_FONT_SMALL, 0);
    lv_obj_center(rt);

    lv_obj_add_event_cb(page.screen, settings_controller_on_general_exit,
                        LV_EVENT_DELETE, app);
    return page.screen;
}

// ── 壁纸图片目录：文件夹选择器 ─────────────────────────────────────────

static void on_click_pick_folder(lv_event_t *e) {
    PAGE_NAVIGATE_TO((SettingsApp *)lv_event_get_user_data(e),
                     PAGE_GENERAL_FOLDER_SELECTOR, NULL);
}

/* 选中目录后的回调。
 * nav_data 传的是 &app->view->page_nav，所以 app 从 nav->app_handler 取
 * —— 和 anki/view_file_selector.c 一样，因为同一个 nav_data 还要喂给
 * back_cb（page_navigator_navigate_back 要求的就是 page_navigator_t*）。 */
static void on_folder_selected(const fs_selection_t *selection, void *user_data) {
    page_navigator_t *nav = (page_navigator_t *)user_data;
    if (!nav) return;

    if (selection && selection->count > 0) {
        SettingsApp *app = (SettingsApp *)nav->app_handler;
        settings_controller_set_wallpaper_folder(app, selection->paths[0]);

        /* mock 树里的路径存下来也没意义，提示一下免得用户以为设好了 */
        if (folder_selector_using_mock()) {
            lv_toast_show("No filesystem mounted - mock path saved", 2000);
        }
    }

    /* 回到 Wallpaper 页。那一页会重新构建，直接显示 model 里的新路径。 */
    page_navigator_navigate_pop(nav, nav->app_handler);
}

static lv_obj_t *build_folder_selector_page(struct SettingsApp *app) {
    if (folder_selector_using_mock()) {
        ESP_LOGW(TAG, "No filesystem mounted at %s - listing built-in mock tree", FSEL_ROOT);
    }

    /* 选目录不是选文件，所以是 SINGLE_DIR；目录不受后缀过滤影响，用不过滤的版本。
     * 已经设过就从那个目录开始，省得每次从根目录点起。 */
    const char *start = app->model->wallpaper.wallpaper_folder[0] != '\0'
                            ? app->model->wallpaper.wallpaper_folder
                            : FSEL_ROOT;

    return folder_selector_create(FS_SEL_MODE_SINGLE_DIR, start,
                                  on_folder_selected,
                                  page_navigator_navigate_back,
                                  &app->view->page_nav);
}

// ── Build: Wallpaper page ──────────────────────────────────────────────

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

// ── Build: Datetime page ───────────────────────────────────────────────

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
    lv_label_set_text(lh, "24 Hour Time");
    lv_obj_set_style_margin_top(lh, 16, 0);
    lv_obj_t *sw24 = lv_switch_create(cont);
    ui_style_set_switch(sw24);
    lv_obj_set_width(sw24, 60);
    if (app->model->datetime.hour_24_enabled) lv_obj_add_state(sw24, LV_STATE_CHECKED);
    else lv_obj_clear_state(sw24, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw24, settings_controller_set_hour_24,
                        LV_EVENT_VALUE_CHANGED, app);

    lv_obj_t *ltz = lv_label_create(cont);
    lv_label_set_text(ltz, "Time Zone");
    lv_obj_set_style_margin_top(ltz, 16, 0);
    lv_obj_t *tz_dd = lv_dropdown_create(cont);
    lv_obj_set_width(tz_dd, LV_PCT(100));
    ui_style_set_dropdown(tz_dd);
    lv_dropdown_set_options(tz_dd, generate_timezone_options());
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

    return page.screen;
}

// ── Build: About page ──────────────────────────────────────────────────

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

// ── Registry ───────────────────────────────────────────────────────────

void settings_view_general_init_registry(SettingsApp *app) {
    PAGE_REGISTE(app, PAGE_GENERAL, build_general_page);
    PAGE_REGISTE(app, PAGE_GENERAL_WALLPAPER, build_wallpaper_page);
    PAGE_REGISTE(app, PAGE_GENERAL_DATETIME, build_datetime_page);
    PAGE_REGISTE(app, PAGE_GENERAL_ABOUT, build_about_page);
    PAGE_REGISTE(app, PAGE_GENERAL_FOLDER_SELECTOR, build_folder_selector_page);
}
