// Wi-Fi password entry page.
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <lvgl.h>
#include "esp_log.h"
#include "lv_ui_style_guide.h"
#include "lv_theme_hardcore.h"
#include "lv_toast.h"
#include "lv_epd_region.h"
#include "widgets/lv_keyboard.h"
#include "settings_app.h"
#include "settings_model.h"
#include "settings_view.h"
#include "controller_wifi.h"
#include "view_wifi_password.h"

static const char *TAG = "settings_wifi_password";

typedef struct {
    lv_obj_t *pwd_input;
    lv_obj_t *keyboard;
    char      ssid[WIFI_SSID_MAX];
} ViewWifiPwdCtx;

static void pwd_cancel_cb(lv_event_t *e)
{
    SettingsApp *app = lv_event_get_user_data(e);
    epd_region_end();
    page_navigator_navigate_pop(&app->view->page_nav, app);
}

static void pwd_confirm_cb(lv_event_t *e)
{
    SettingsApp *app = lv_event_get_user_data(e);
    ViewWifiPwdCtx *ctx = (ViewWifiPwdCtx *)app->view->page_nav.nav_ctx;
    if (!ctx) return;

    const char *pwd = lv_textarea_get_text(ctx->pwd_input);
    if (!pwd || strlen(pwd) < 8) {
        /* WPA2 最短 8 位；提前挡掉，省得白等 15 秒超时 */
        lv_toast_show("Password must be at least 8 characters", 2000);
        return;
    }

    ESP_LOGI(TAG, "connect to %s", ctx->ssid);
    settings_controller_wifi_connect(ctx->ssid, pwd);

    epd_region_end();
    page_navigator_navigate_pop(&app->view->page_nav, app);
}

static void pwd_ta_event_cb(lv_event_t *e)
{
    SettingsApp *app = lv_event_get_user_data(e);
    ViewWifiPwdCtx *ctx = (ViewWifiPwdCtx *)app->view->page_nav.nav_ctx;
    if (!ctx || !ctx->keyboard) return;

    lv_obj_t *ta = lv_event_get_target(e);

    if (lv_obj_has_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN)) {
        lv_keyboard_set_textarea(ctx->keyboard, ta);
        lv_obj_remove_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ctx->keyboard);
        lv_obj_update_layout(ctx->keyboard);

        /* 和 anki / todolist 的表单一致：先整屏打一帧基准，
         * 之后打字只刷输入框那一块，不整屏闪 */
        epd_region_begin_focus(lv_screen_active(), ta);
    } else if (lv_keyboard_get_textarea(ctx->keyboard) != ta) {
        lv_keyboard_set_textarea(ctx->keyboard, ta);
        epd_region_flush_obj(ta);
    }
    lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
}

static void pwd_show_toggle_cb(lv_event_t *e)
{
    SettingsApp *app = lv_event_get_user_data(e);
    ViewWifiPwdCtx *ctx = (ViewWifiPwdCtx *)app->view->page_nav.nav_ctx;
    if (!ctx) return;

    lv_obj_t *checkbox = lv_event_get_target(e);
    bool show = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
    lv_textarea_set_password_mode(ctx->pwd_input, !show);
    epd_region_flush_obj(ctx->pwd_input);
}

static lv_obj_t *build_wifi_password_page(struct SettingsApp *app, void *user_data)
{
    Page page = lv_page_create("WiFi Password", true,
                               page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t *cont = page.container;
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 10, 0);

    ViewWifiPwdCtx *ctx = malloc(sizeof(ViewWifiPwdCtx));
    if (!ctx) {
        ESP_LOGE(TAG, "alloc password ctx failed");
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));
    snprintf(ctx->ssid, sizeof(ctx->ssid), "%s", (const char *)user_data);
    app->view->page_nav.nav_ctx = ctx;

    lv_obj_t *l1 = lv_label_create(cont);
    lv_label_set_text(l1, "SSID");
    lv_obj_set_style_text_font(l1, LV_FONT_SMALL, 0);

    lv_obj_t *ssid_lbl = lv_label_create(cont);
    lv_label_set_text(ssid_lbl, ctx->ssid);
    lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(ssid_lbl, LV_PCT(100));
    lv_obj_set_style_pad_bottom(ssid_lbl, 16, 0);

    lv_obj_t *l2 = lv_label_create(cont);
    lv_label_set_text(l2, "Password");
    lv_obj_set_style_text_font(l2, LV_FONT_SMALL, 0);

    ctx->pwd_input = lv_textarea_create(cont);
    lv_textarea_set_one_line(ctx->pwd_input, true);
    lv_textarea_set_password_mode(ctx->pwd_input, true);
    /* 只设宽度：one_line 已经把高度设成 LV_SIZE_CONTENT，
     * 再写死一个小于 line_height + 2*(pad+border) 的值会让文字上下跳。 */
    lv_obj_set_width(ctx->pwd_input, LV_PCT(100));
    lv_obj_set_style_border_width(ctx->pwd_input, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(ctx->pwd_input, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->pwd_input, 2, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(ctx->pwd_input, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_opa(ctx->pwd_input, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ctx->pwd_input, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_border_opa(ctx->pwd_input, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(ctx->pwd_input, 0, LV_PART_CURSOR);
    lv_obj_add_event_cb(ctx->pwd_input, pwd_ta_event_cb, LV_EVENT_FOCUSED, app);
    lv_obj_add_event_cb(ctx->pwd_input, pwd_ta_event_cb, LV_EVENT_CLICKED, app);

    lv_obj_t *show_checkbox = lv_checkbox_create(cont);
    lv_checkbox_set_text(show_checkbox, "Show password");
    lv_obj_add_event_cb(show_checkbox, pwd_show_toggle_cb, LV_EVENT_VALUE_CHANGED, app);

    lv_obj_t *btn_row = lv_obj_create(cont);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_margin_top(btn_row, 20, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_AROUND,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancel = lv_btn_create(btn_row);
    ui_style_set_btn_secondary(cancel);
    lv_obj_set_size(cancel, 160, 56);
    lv_obj_set_style_radius(cancel, 2, 0);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, "Cancel");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel, pwd_cancel_cb, LV_EVENT_CLICKED, app);

    lv_obj_t *ok = lv_btn_create(btn_row);
    ui_style_set_btn_primary(ok);
    lv_obj_set_size(ok, 160, 56);
    lv_obj_set_style_radius(ok, 2, 0);
    lv_obj_t *ol = lv_label_create(ok);
    lv_label_set_text(ol, "Connect");
    lv_obj_center(ol);
    lv_obj_add_event_cb(ok, pwd_confirm_cb, LV_EVENT_CLICKED, app);

    ctx->keyboard = lv_keyboard_create(page.screen);
    setup_custom_keyboard(ctx->keyboard);
    lv_obj_set_size(ctx->keyboard, LV_PCT(100), 250);
    lv_obj_add_flag(ctx->keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(ctx->keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN);

    return page.screen;
}

void settings_view_wifi_password_init_registry(struct SettingsApp *app)
{
    PAGE_REGISTE(app, PAGE_WIFI_PASSWORD, build_wifi_password_page);
}
