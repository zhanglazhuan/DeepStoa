/*
 * DeepStoa — Global Gesture Detection
 *
 * Reimplemented from EPOS/Zephyr (epos_home_gesture.c) for 480×800 e-ink.
 *
 * Gestures:
 *   Swipe up from bottom  → "Exit App" confirm dialog (lv_bottom_sheet)
 *   Swipe down from top   → Control panel (WiFi / Full Refresh / Rotate)
 *
 * Dependencies:
 *   - display_control.h  (e-ink flush mode & orientation)
 *   - wifi_manager.h     (WiFi state)
 *   - touch_control.h    (global touch_indev)
 *   - ui_fonts.h         (custom icon glyphs)
 *   - lv_theme_hardcore.h (LV_FONT_SMALL, etc.)
 *   - app_manager.h      (app lifecycle)
 *   - launcher.h         (launcher_return_home)
 */

#include "esp_log.h"
#include "lvgl.h"

#include "display_control.h"
#include "touch_control.h"
#include "wifi_manager.h"
#include "app_manager.h"
#include "launcher.h"
#include "ui_fonts.h"
#include "lv_theme_hardcore.h"
#include "lv_bottom_sheet.h"
#include "lv_ui_style_guide.h"

static const char *TAG = "gesture";

/* ── Swipe thresholds (480×800 e-ink) ────────────────────────────────── */

#define SWIPE_UP_DY_THRESHOLD    -60
#define SWIPE_UP_START_Y_MIN      600

#define SWIPE_DOWN_DY_THRESHOLD    30
#define SWIPE_DOWN_START_Y_MAX     240

/* ── Control panel action IDs ─────────────────────────────────────────── */

enum {
    ACTION_WIFI_TOGGLE,
    ACTION_FULL_REFRESH,
    ACTION_ROTATE_SCREEN
};

static lv_obj_t *g_ctrl_panel = NULL;

/* ========================================================================
 * Exit confirm dialog (lv_bottom_sheet)
 * ======================================================================== */

static void exit_yes_cb(lv_event_t *e)
{
    lv_bottom_sheet_t *bs = lv_event_get_user_data(e);
    lv_bottom_sheet_close(bs);
    launcher_return_home();
}

static void exit_cancel_cb(lv_event_t *e)
{
    lv_bottom_sheet_close(lv_event_get_user_data(e));
}

static void show_exit_confirm_dialog(void)
{
    lv_bottom_sheet_t *bs = lv_bottom_sheet_create(lv_layer_top());
    lv_bottom_sheet_add_header(bs, "Exit App");

    lv_obj_t *content = lv_bottom_sheet_get_content(bs);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(content, 24, 0);

    const char *app_name = app_manager_get_current_app_name();
    char msg[64];
    snprintf(msg, sizeof(msg), "Exit \"%s\" and return to home?",
             app_name ? app_name : "App");
    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, msg);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));

    /* 按钮行。
     * 原来是 SPACE_BETWEEN + 120px 按钮：内容区 448 宽减掉两个按钮只剩
     * 208px，全塞在中间，两个按钮被顶到最边上贴着屏幕边缘。
     * 改成 SPACE_EVENLY —— 三段间隙均分，边上的比中间的宽，视觉上按钮是
     * 朝中间收的；再给行加一点左右内边距，离屏幕边缘留出余量。
     * 尺寸对齐 wifi 密码页那个同形态的两按钮行（160x56）。 */
    lv_obj_t *btn_row = lv_obj_create(content);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(btn_row, 12, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *btn_cancel = lv_button_create(btn_row);
    lv_obj_set_size(btn_cancel, UI_SHEET_BTN_W, UI_SHEET_BTN_H);
    lv_obj_add_event_cb(btn_cancel, exit_cancel_cb, LV_EVENT_CLICKED, bs);
    lv_obj_t *cl = lv_label_create(btn_cancel);
    lv_label_set_text(cl, "Cancel"); lv_obj_center(cl);
    ui_style_set_btn_secondary(btn_cancel);

    lv_obj_t *btn_exit = lv_button_create(btn_row);
    lv_obj_set_size(btn_exit, UI_SHEET_BTN_W, UI_SHEET_BTN_H);
    lv_obj_add_event_cb(btn_exit, exit_yes_cb, LV_EVENT_CLICKED, bs);
    lv_obj_t *el = lv_label_create(btn_exit);
    lv_label_set_text(el, "Exit"); lv_obj_center(el);
    ui_style_set_btn_primary(btn_exit);
}

/* ========================================================================
 * Control panel
 * ======================================================================== */

static void ctrl_panel_btn_cb(lv_event_t *e)
{
    int action = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);

    switch (action) {
    case ACTION_WIFI_TOGGLE: {
        bool is_on = lv_obj_has_state(btn, LV_STATE_CHECKED);
        if (is_on) {
            ESP_LOGI(TAG, "WiFi turned ON");
            wifi_start();
        } else {
            ESP_LOGI(TAG, "WiFi turned OFF");
            wifi_stop();
        }
        break;
    }

    case ACTION_FULL_REFRESH:
        ESP_LOGI(TAG, "Manual Full Refresh");
        epd_display_set_flush_mode(EPD_FLUSH_MODE_FULL);
        lv_obj_invalidate(lv_screen_active());
        break;

    case ACTION_ROTATE_SCREEN: {
        epd_orientation_t current = epd_display_get_orientation();
        epd_orientation_t new_orient = (current == EPD_ORIENT_PORTRAIT)
                                           ? EPD_ORIENT_LANDSCAPE
                                           : EPD_ORIENT_PORTRAIT;
        epd_display_set_orientation(new_orient);
        ESP_LOGI(TAG, "Screen Rotated");
        break;
    }
    }
}

static void close_panel_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    if (target == g_ctrl_panel) {
        lv_obj_del(g_ctrl_panel);
        g_ctrl_panel = NULL;
    }
}

/* Helper: create a large circular status button for the control panel */
static lv_obj_t *create_ctrl_button(lv_obj_t *parent, const char *icon_str,
                                    const char *label_str, int action_id,
                                    bool is_toggle, bool init_state)
{
    uint16_t btn_size = 60;

    /* Vertical container: button on top, label below */
    lv_obj_t *item_cont = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(item_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(item_cont, 0, 0);
    lv_obj_set_style_pad_all(item_cont, 0, 0);
    lv_obj_clear_flag(item_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(item_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(item_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(item_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(item_cont, 8, 0);

    /* Circular button */
    lv_obj_t *btn = lv_button_create(item_cont);
    lv_obj_set_size(btn, btn_size, btn_size);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);

    /* OFF state (default): white bg, black border */
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 2, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn, lv_color_black(), LV_STATE_DEFAULT);

    /* ON state (checked): black bg, white text, no border */
    lv_obj_set_style_bg_color(btn, lv_color_black(), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btn, lv_color_white(), LV_STATE_CHECKED);
    lv_obj_set_style_border_width(btn, 0, LV_STATE_CHECKED);

    /* Press feedback */
    lv_obj_set_style_bg_color(btn, lv_color_black(), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_white(), LV_STATE_PRESSED);

    if (is_toggle) {
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
        if (init_state) {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        }
    }
    lv_obj_add_event_cb(btn, ctrl_panel_btn_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)action_id);

    /* Icon inside button */
    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, icon_str);
    lv_obj_set_style_text_font(icon, &custom_font_normal, 0);
    lv_obj_center(icon);

    /* Label below button */
    lv_obj_t *label = lv_label_create(item_cont);
    lv_label_set_text(label, label_str);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, LV_FONT_NORMAL, 0);

    return btn;
}

static void show_control_panel(void)
{
    if (g_ctrl_panel != NULL) return;

    /* Full-screen transparent overlay */
    g_ctrl_panel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_ctrl_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(g_ctrl_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_ctrl_panel, 0, 0);
    lv_obj_set_style_pad_all(g_ctrl_panel, 0, 0);
    lv_obj_add_flag(g_ctrl_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_ctrl_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_ctrl_panel, close_panel_cb, LV_EVENT_CLICKED, NULL);

    /* Panel content (white, fixed height, anchored to top) */
    lv_obj_t *panel_content = lv_obj_create(g_ctrl_panel);
    lv_obj_set_size(panel_content, LV_PCT(100), 180);
    lv_obj_align(panel_content, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_set_style_bg_color(panel_content, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(panel_content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel_content, 2, 0);
    lv_obj_set_style_border_side(panel_content, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(panel_content, lv_color_black(), 0);
    lv_obj_set_style_pad_all(panel_content, 10, 0);

    lv_obj_add_flag(panel_content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(panel_content, LV_OBJ_FLAG_SCROLLABLE);

    /* Flex row: space buttons evenly */
    lv_obj_set_flex_flow(panel_content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(panel_content, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Read initial states */
    wifi_info_t wifi_info;
    wifi_get_info(&wifi_info);
    bool is_wifi_on = wifi_info.is_connected;

    epd_orientation_t current_orient = epd_display_get_orientation();
    bool is_landscape = (current_orient == EPD_ORIENT_LANDSCAPE ||
                         current_orient == EPD_ORIENT_LANDSCAPE_INVERTED);

    /* Three control buttons */
    create_ctrl_button(panel_content, LV_SYMBOL_WIFI, "WiFi",
                       ACTION_WIFI_TOGGLE, true, is_wifi_on);
    create_ctrl_button(panel_content, LV_SYMBOL_REFRESH, "Flush",
                       ACTION_FULL_REFRESH, false, false);
    create_ctrl_button(panel_content, MY_SYMBOL_LANDSCAPE, "Rotate",
                       ACTION_ROTATE_SCREEN, true, is_landscape);
}

/* ========================================================================
 * Global indev monitor
 * ======================================================================== */

static void global_indev_event_cb(lv_event_t *e)
{
    static lv_point_t start_pt;

    lv_indev_t *indev = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(indev, &start_pt);
    } else if (code == LV_EVENT_RELEASED) {
        lv_point_t end_pt;
        lv_indev_get_point(indev, &end_pt);

        int32_t dy = end_pt.y - start_pt.y;

        /* Swipe UP from bottom → exit app */
        if (dy < SWIPE_UP_DY_THRESHOLD && start_pt.y > SWIPE_UP_START_Y_MIN) {
            if (app_manager_is_app_running()) {
                show_exit_confirm_dialog();
                ESP_LOGI(TAG, "Show exit confirm dialog");
            }
        }
        /* Swipe DOWN from top → control panel */
        else if (dy > SWIPE_DOWN_DY_THRESHOLD &&
                 start_pt.y < SWIPE_DOWN_START_Y_MAX) {
            show_control_panel();
            ESP_LOGI(TAG, "Show control panel");
        }
    }
}

/* ========================================================================
 * Public API
 * ======================================================================== */

void launcher_gesture_init(void)
{
    if (touch_indev != NULL) {
        lv_indev_add_event_cb(touch_indev, global_indev_event_cb,
                              LV_EVENT_ALL, NULL);
        ESP_LOGI(TAG, "Gesture hooks installed on touch indev");
    } else {
        ESP_LOGE(TAG, "Failed to hook gestures: touch_indev is NULL");
    }
}
