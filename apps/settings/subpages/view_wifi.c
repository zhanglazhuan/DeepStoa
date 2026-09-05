/**
 * @file view_wifi.c
 * @brief Wi-Fi 设置页 + 密码输入页
 *
 * Ported from D:\Codes\EPOS\epos\apps\settings\subpages\view_wifi.c
 *
 * 相对 EPOS 的改动：
 *   - EPOS 从 label 文本反解析 SSID（顺着 parent 往上摸三层）。这里改成把
 *     扫描列表的下标存进按钮的 user_data，SSID 带空格也不会出错。
 *   - EPOS 用自定义字体的 MY_SYMBOL_INFO / MY_SYMBOL_DOING，这里统一用
 *     LVGL 内置符号，省一套字体。
 *   - 密码页用项目自己的 setup_custom_keyboard + lv_epd_region 局部刷新，
 *     和 anki / todolist 的表单保持一致（墨水屏上打字不整屏闪）。
 *   - 扫描/连接都是异步的（见 controller_wifi.h），页面上用一行状态文字
 *     表示"扫描中…"，不做转圈动画 —— 墨水屏上动画等于持续刷新。
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <lvgl.h>

#include "esp_log.h"
#include "lv_ui_style_guide.h"
#include "lv_theme_hardcore.h"
#include "ui_fonts.h"
#include "lv_bottom_sheet.h"
#include "lv_toast.h"
#include "lv_epd_region.h"
#include "widgets/lv_keyboard.h"

#include "settings_app.h"
#include "settings_model.h"
#include "settings_view.h"
#include "submodules/controller_wifi.h"
#include "view_wifi.h"

static const char *TAG = "settings_view_wifi";

/* 密码页的上下文；SSID 在这里带过去，不从 UI 文本反读 */
typedef struct {
    lv_obj_t *pwd_input;
    lv_obj_t *keyboard;
    char      ssid[WIFI_SSID_MAX];
} ViewWifiPwdCtx;

/* 进密码页时带的 SSID。page builder 的 user_data 只能传一个指针，
 * 而 nav_ctx 又要留给页面自己的上下文，所以这里用一个静态中转。 */
static char s_pending_ssid[WIFI_SSID_MAX];

static ViewWifiCtx *wifi_ctx(struct SettingsApp *app)
{
    if (!app || !app->view) return NULL;
    if (app->view->page_nav.current_page != PAGE_WIFI) return NULL;
    return (ViewWifiCtx *)app->view->page_nav.nav_ctx;
}

/* ── 信号强度 ─────────────────────────────────────────────────────── */

static const char *rssi_text(int8_t rssi)
{
    if (rssi > -50) return "Excellent";
    if (rssi > -60) return "Good";
    if (rssi > -70) return "Fair";
    return "Weak";
}

/* 墨水屏上颜色没用，用格数表示强弱。
 * 用 ui_fonts.h 里的 wifi 图标而不是 ▮▯ —— 后者不在 Montserrat 字库里，
 * 屏上会渲染成方框。用它的 label 必须设 custom_font_normal（它是
 * montserrat 32 的克隆 + 图标字体 fallback 链，ASCII 和图标都能出）。 */
static const char *rssi_bars(int8_t rssi)
{
    if (rssi > -50) return MY_SYMBOL_WIFI_3_BAR;
    if (rssi > -65) return MY_SYMBOL_WIFI_2_BAR;
    return MY_SYMBOL_WIFI_1_BAR;
}

/* ── 网络详情弹窗 ─────────────────────────────────────────────────── */

/* 待忘记的 SSID。弹窗按钮的 user_data 已经占给 sheet 了，用静态中转。 */
static char s_forget_ssid[WIFI_SSID_MAX];

static void forget_network_cb(lv_event_t *e)
{
    lv_bottom_sheet_close((lv_bottom_sheet_t *)lv_event_get_user_data(e));
    settings_controller_wifi_forget(s_forget_ssid);
    lv_toast_show("Network forgotten", 1500);
}

static void show_network_info(struct SettingsApp *app, const char *ssid,
                              int8_t rssi, uint8_t channel, bool secured)
{
    lv_bottom_sheet_t *sheet = lv_bottom_sheet_create(NULL);
    lv_obj_t *content = lv_bottom_sheet_get_content(sheet);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 8, 0);

    lv_obj_t *title = lv_label_create(content);
    lv_label_set_text(title, "WiFi Details");

    lv_obj_t *body = lv_label_create(content);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_style_text_font(body, LV_FONT_SMALL, 0);
    lv_label_set_text_fmt(body,
        "SSID: %s\nSignal: %s (%d dBm)\nChannel: %u\nSecurity: %s\nSaved: %s",
        ssid, rssi_text(rssi), rssi, channel,
        secured ? "Secured" : "Open",
        settings_controller_wifi_is_saved(ssid) ? "Yes" : "No");

    /* 已保存的网络必须给"忘记"入口 —— 否则密码输错过一次，之后每次都会
     * 拿旧密码自动重连，用户永远没机会改。 */
    if (settings_controller_wifi_is_saved(ssid)) {
        snprintf(s_forget_ssid, sizeof(s_forget_ssid), "%s", ssid);

        lv_obj_t *btn = lv_btn_create(content);
        ui_style_set_btn_secondary(btn);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 50);
        lv_obj_set_style_radius(btn, 2, 0);
        lv_obj_set_style_margin_top(btn, 8, 0);
        lv_obj_t *bl = lv_label_create(btn);
        lv_label_set_text(bl, LV_SYMBOL_TRASH "  Forget This Network");
        lv_obj_center(bl);
        lv_obj_add_event_cb(btn, forget_network_cb, LV_EVENT_CLICKED, sheet);
    }
    (void)app;
}

/* ── 列表行 ───────────────────────────────────────────────────────── */

/* 按钮的 user_data 存扫描列表下标 +1（0 留给"当前连接"那一行） */
static void on_network_clicked(lv_event_t *e)
{
    SettingsApp *app = &g_settings_app;
    uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);
    if (tag == 0 || !app->model) return;

    settings_model_wifi_t *w = &app->model->wifi;
    uint8_t idx = (uint8_t)(tag - 1);
    if (idx >= w->scanned_count) return;

    const wifi_scan_entry_t *n = &w->scanned[idx];

    if (settings_controller_wifi_busy()) {
        lv_toast_show("Busy, please wait", 1500);
        return;
    }

    /* 已经连着这个就什么也不做 */
    if (w->state == WIFI_UI_CONNECTED && strcmp(w->current.ssid, n->ssid) == 0) {
        lv_toast_show("Already connected", 1200);
        return;
    }

    /* 开放网络直接连；已保存凭据的直接重连；否则去输密码 */
    if (!n->secured) {
        settings_controller_wifi_connect(n->ssid, "");
        return;
    }
    if (settings_controller_wifi_is_saved(n->ssid)) {
        ESP_LOGI(TAG, "reconnect with saved credentials: %s", n->ssid);
        settings_controller_wifi_connect(n->ssid, "");
        return;
    }

    snprintf(s_pending_ssid, sizeof(s_pending_ssid), "%s", n->ssid);
    PAGE_NAVIGATE_TO(app, PAGE_WIFI_PASSWORD, NULL);
}

static void on_info_clicked(lv_event_t *e)
{
    SettingsApp *app = &g_settings_app;
    uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);
    if (!app->model) return;
    settings_model_wifi_t *w = &app->model->wifi;

    if (tag == 0) {
        /* 当前连接那一行 */
        show_network_info(app, w->current.ssid, w->current.rssi,
                          w->current.channel, w->current.auth_mode != WIFI_AUTH_OPEN);
        return;
    }
    uint8_t idx = (uint8_t)(tag - 1);
    if (idx >= w->scanned_count) return;
    const wifi_scan_entry_t *n = &w->scanned[idx];
    show_network_info(app, n->ssid, n->rssi, n->channel, n->secured);
}

/**
 * 一行网络。排版跟 NomadCast 的 view_wifi 一致：单行、扁平、无边框卡片，
 * 整行可点即连接。
 *
 * 但配色没有照搬 —— NomadCast 用 0x4CAF50 / 0x8BC34A / 0x9E9E9E 三档颜色表示
 * 信号强弱，在 1bit 墨水屏上会被抖成一样的灰，完全区分不出来。
 * 这里改成用格子数（▮▮▮ / ▮▮▯ / ▮▯▯）表达强弱，形状在黑白屏上才可读。
 */
static void create_network_row(lv_obj_t *parent, const wifi_scan_entry_t *n,
                               uintptr_t tag)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 12, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_black(), 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, on_network_clicked, LV_EVENT_CLICKED, (void *)tag);

    /* 信号：用格子数而不是颜色 */
    lv_obj_t *sig = lv_label_create(row);
    lv_label_set_text(sig, rssi_bars(n->rssi));
    /* 格子字符在 fallback 链里，必须用 custom_font_normal 才画得出来 */
    lv_obj_set_style_text_font(sig, &custom_font_normal, 0);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, n->ssid);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(name, 1);

    if (n->secured) {
        lv_obj_t *lock = lv_label_create(row);
        lv_label_set_text(lock, "*");
    }

    /* 详情 / 忘记网络。NomadCast 没有这个入口，但这里必须有 ——
     * 密码输错过一次之后会一直自动重连，没有"忘记"就永远改不了。 */
    lv_obj_t *btn_info = lv_btn_create(row);
    ui_style_set_btn_secondary(btn_info);
    lv_obj_set_size(btn_info, 48, 40);
    lv_obj_set_style_radius(btn_info, 2, 0);
    lv_obj_t *il = lv_label_create(btn_info);
    lv_label_set_text(il, LV_SYMBOL_LIST);
    lv_obj_center(il);
    lv_obj_add_event_cb(btn_info, on_info_clicked, LV_EVENT_CLICKED, (void *)tag);
}

/* ── 三个刷新入口 ─────────────────────────────────────────────────── */

void settings_view_wifi_update_visibility(struct SettingsApp *app)
{
    ViewWifiCtx *ctx = wifi_ctx(app);
    if (!ctx) return;

    settings_model_wifi_t *w = &app->model->wifi;
    bool on = (w->enabled != 0);

    if (ctx->sw) {
        if (on) lv_obj_add_state(ctx->sw, LV_STATE_CHECKED);
        else    lv_obj_remove_state(ctx->sw, LV_STATE_CHECKED);
        if (w->busy) lv_obj_add_state(ctx->sw, LV_STATE_DISABLED);
        else         lv_obj_remove_state(ctx->sw, LV_STATE_DISABLED);
    }

    /* 状态行：忙 > 错误 > 隐藏。墨水屏上不做转圈，就一行字。 */
    if (ctx->status_lbl) {
        if (w->busy && w->busy_text[0]) {
            lv_label_set_text(ctx->status_lbl, w->busy_text);
            lv_obj_remove_flag(ctx->status_lbl, LV_OBJ_FLAG_HIDDEN);
        } else if (w->last_error[0]) {
            lv_label_set_text_fmt(ctx->status_lbl, LV_SYMBOL_WARNING " %s", w->last_error);
            lv_obj_remove_flag(ctx->status_lbl, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ctx->status_lbl, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* 扫描/连接进行中就禁掉，避免重复触发 */
    if (ctx->scan_btn) {
        if (on && !w->busy) lv_obj_remove_state(ctx->scan_btn, LV_STATE_DISABLED);
        else                lv_obj_add_state(ctx->scan_btn, LV_STATE_DISABLED);
    }

    /* Wi-Fi 关掉时，开关以下的东西全部收起来 —— 和 NomadCast 里
     * `if (!wifi_on) return page.screen;` 是同一个意思，只是这里页面常驻，
     * 用隐藏而不是不构建。 */
    if (on) {
        lv_obj_remove_flag(ctx->scan_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ctx->scanned_cont, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->connected_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->scan_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->scanned_cont, LV_OBJ_FLAG_HIDDEN);
    }
}

void settings_view_wifi_update_connected(struct SettingsApp *app)
{
    ViewWifiCtx *ctx = wifi_ctx(app);
    if (!ctx) return;

    settings_model_wifi_t *w = &app->model->wifi;

    if (!w->enabled || w->state == WIFI_UI_DISCONNECTED) {
        lv_obj_add_flag(ctx->connected_row, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(ctx->connected_row, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(ctx->connected_lbl, w->current.ssid);

    if (w->state == WIFI_UI_CONNECTING) {
        lv_label_set_text(ctx->connected_icon, LV_SYMBOL_REFRESH);
    } else {
        lv_label_set_text(ctx->connected_icon, LV_SYMBOL_OK);
    }
}

void settings_view_wifi_update_scanned_list(struct SettingsApp *app)
{
    ViewWifiCtx *ctx = wifi_ctx(app);
    if (!ctx || !ctx->scanned_cont) return;

    settings_model_wifi_t *w = &app->model->wifi;
    lv_obj_clean(ctx->scanned_cont);

    /* 扫描中不渲染上一次的结果 —— 照 NomadCast 的做法。
     * 留着旧列表会让人以为扫描已经完成，点下去连的还是过期的 AP。 */
    if (w->busy) {
        lv_obj_t *busy = lv_label_create(ctx->scanned_cont);
        lv_label_set_text(busy, w->busy_text[0] ? w->busy_text : "Scanning...");
        lv_obj_set_style_pad_top(busy, 20, 0);
        lv_obj_set_width(busy, LV_PCT(100));
        lv_obj_set_style_text_align(busy, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    /* 先数一遍，标题才能带上数量 */
    uint8_t shown = 0;
    for (uint8_t i = 0; i < w->scanned_count; i++) {
        if (w->state == WIFI_UI_CONNECTED &&
            strcmp(w->scanned[i].ssid, w->current.ssid) == 0) continue;
        shown++;
    }

    if (shown == 0) {
        lv_obj_t *empty = lv_label_create(ctx->scanned_cont);
        lv_label_set_text(empty, "No networks found");
        lv_obj_set_style_pad_top(empty, 20, 0);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    lv_obj_t *title = lv_label_create(ctx->scanned_cont);
    lv_label_set_text_fmt(title, "Available Networks (%u)", shown);
    lv_obj_set_style_text_font(title, LV_FONT_SMALL, 0);
    lv_obj_set_style_pad_ver(title, 8, 0);
    lv_obj_set_style_pad_left(title, 12, 0);

    for (uint8_t i = 0; i < w->scanned_count; i++) {
        /* 已经连上的那个不在列表里重复出现 */
        if (w->state == WIFI_UI_CONNECTED &&
            strcmp(w->scanned[i].ssid, w->current.ssid) == 0) continue;

        create_network_row(ctx->scanned_cont, &w->scanned[i], (uintptr_t)(i + 1));
    }
}

/* ── 密码输入页 ───────────────────────────────────────────────────── */

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

    lv_obj_t *btn = lv_event_get_target(e);
    bool show = lv_obj_has_state(btn, LV_STATE_CHECKED);
    lv_textarea_set_password_mode(ctx->pwd_input, !show);
    epd_region_flush_obj(ctx->pwd_input);
}

static lv_obj_t *build_wifi_password_page(struct SettingsApp *app)
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
    snprintf(ctx->ssid, sizeof(ctx->ssid), "%s", s_pending_ssid);
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
    lv_obj_set_scrollbar_mode(ctx->pwd_input, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_opa(ctx->pwd_input, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(ctx->pwd_input, 0, LV_PART_CURSOR);
    lv_obj_add_event_cb(ctx->pwd_input, pwd_ta_event_cb, LV_EVENT_FOCUSED, app);
    lv_obj_add_event_cb(ctx->pwd_input, pwd_ta_event_cb, LV_EVENT_CLICKED, app);

    lv_obj_t *show_btn = lv_btn_create(cont);
    ui_style_set_btn_secondary(show_btn);
    lv_obj_add_flag(show_btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_size(show_btn, 140, 44);
    lv_obj_set_style_radius(show_btn, 2, 0);
    lv_obj_t *sl = lv_label_create(show_btn);
    lv_label_set_text(sl, LV_SYMBOL_EYE_OPEN "  Show password");
    lv_obj_center(sl);
    lv_obj_add_event_cb(show_btn, pwd_show_toggle_cb, LV_EVENT_VALUE_CHANGED, app);

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

/* ── Wi-Fi 主页 ───────────────────────────────────────────────────── */

static void scan_btn_cb(lv_event_t *e)
{
    (void)e;
    settings_controller_wifi_scan();
}

static void connected_info_cb(lv_event_t *e)
{
    on_info_clicked(e);
}

static void disconnect_cb(lv_event_t *e)
{
    (void)e;
    settings_controller_wifi_disconnect();
}

static lv_obj_t *build_wifi_page(struct SettingsApp *app)
{
    Page page = lv_page_create("Wi-Fi", true,
                               page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t *cont = page.container;
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(cont, 0, 0);   /* 行自己带 12px，通栏分隔线才到边 */
    lv_obj_set_style_pad_row(cont, 0, 0);

    ViewWifiCtx *ctx = malloc(sizeof(ViewWifiCtx));
    if (!ctx) {
        ESP_LOGE(TAG, "alloc wifi ctx failed");
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));
    app->view->page_nav.nav_ctx = ctx;

    /* 开关行 */
    lv_obj_t *sw_row = lv_obj_create(cont);
    lv_obj_remove_style_all(sw_row);
    lv_obj_set_width(sw_row, LV_PCT(100));
    lv_obj_set_height(sw_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sw_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sw_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(sw_row, 12, 0);
    lv_obj_set_style_border_side(sw_row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(sw_row, 1, 0);
    lv_obj_set_style_border_color(sw_row, lv_color_black(), 0);

    lv_obj_t *sw_lbl = lv_label_create(sw_row);
    lv_label_set_text(sw_lbl, "Wi-Fi");

    ctx->sw = lv_switch_create(sw_row);
    ui_style_set_switch(ctx->sw);
    lv_obj_add_event_cb(ctx->sw, settings_controller_wifi_toggle,
                        LV_EVENT_VALUE_CHANGED, app);

    /* 状态行：忙碌 / 错误。默认隐藏。 */
    ctx->status_lbl = lv_label_create(cont);
    lv_label_set_long_mode(ctx->status_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ctx->status_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(ctx->status_lbl, LV_FONT_SMALL, 0);
    lv_obj_set_style_pad_hor(ctx->status_lbl, 12, 0);
    lv_obj_set_style_pad_top(ctx->status_lbl, 8, 0);
    lv_label_set_text(ctx->status_lbl, "");
    lv_obj_add_flag(ctx->status_lbl, LV_OBJ_FLAG_HIDDEN);

    /* 当前连接。NomadCast 用 0xE8F5E9 的浅绿底把它和普通行区分开，
     * 1bit 屏上没有这种中间色，改用 2px 描边卡片 —— 和本工程其他地方
     * "边框 2px = 当前项、1px = 普通项"的约定一致。 */
    ctx->connected_row = lv_obj_create(cont);
    lv_obj_remove_style_all(ctx->connected_row);
    lv_obj_set_width(ctx->connected_row, LV_PCT(100));
    lv_obj_set_height(ctx->connected_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctx->connected_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctx->connected_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(ctx->connected_row, 12, 0);
    lv_obj_set_style_pad_column(ctx->connected_row, 10, 0);
    lv_obj_set_style_radius(ctx->connected_row, 2, 0);
    lv_obj_set_style_border_width(ctx->connected_row, 2, 0);
    lv_obj_set_style_border_color(ctx->connected_row, lv_color_black(), 0);

    ctx->connected_icon = lv_label_create(ctx->connected_row);
    lv_label_set_text(ctx->connected_icon, LV_SYMBOL_OK);

    ctx->connected_lbl = lv_label_create(ctx->connected_row);
    lv_label_set_long_mode(ctx->connected_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(ctx->connected_lbl, 1);
    lv_label_set_text(ctx->connected_lbl, "");

    lv_obj_t *info_btn = lv_btn_create(ctx->connected_row);
    ui_style_set_btn_secondary(info_btn);
    lv_obj_set_size(info_btn, 48, 40);
    lv_obj_set_style_radius(info_btn, 2, 0);
    lv_obj_center(lv_label_create(info_btn));
    lv_label_set_text(lv_obj_get_child(info_btn, 0), LV_SYMBOL_LIST);
    lv_obj_add_event_cb(info_btn, connected_info_cb, LV_EVENT_CLICKED, (void *)0);

    lv_obj_t *dis_btn = lv_btn_create(ctx->connected_row);
    ui_style_set_btn_secondary(dis_btn);
    lv_obj_set_size(dis_btn, 48, 40);
    lv_obj_set_style_radius(dis_btn, 2, 0);
    lv_obj_center(lv_label_create(dis_btn));
    lv_label_set_text(lv_obj_get_child(dis_btn, 0), LV_SYMBOL_CLOSE);
    lv_obj_add_event_cb(dis_btn, disconnect_cb, LV_EVENT_CLICKED, app);

    lv_obj_add_flag(ctx->connected_row, LV_OBJ_FLAG_HIDDEN);

    /* 扫描按钮。这是本页的主操作，用 primary（实心）—— NomadCast 那里是
     * 0x1976D2 的蓝色实心按钮，黑白屏上"实心"就是它的等价物。
     * 分割线去掉了：已连接卡片有边框、列表行有下划线，层次已经够清楚。 */
    ctx->scan_btn = lv_btn_create(cont);
    ui_style_set_btn_primary(ctx->scan_btn);
    lv_obj_set_width(ctx->scan_btn, LV_PCT(100));
    ui_style_set_btn_size_large(ctx->scan_btn);
    lv_obj_set_style_margin_top(ctx->scan_btn, 8, 0);
    lv_obj_t *scan_lbl = lv_label_create(ctx->scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH "  Scan for Networks");
    lv_obj_center(scan_lbl);
    lv_obj_add_event_cb(ctx->scan_btn, scan_btn_cb, LV_EVENT_CLICKED, app);

    /* 扫描结果 */
    ctx->scanned_cont = lv_obj_create(cont);
    lv_obj_remove_style_all(ctx->scanned_cont);
    lv_obj_set_width(ctx->scanned_cont, LV_PCT(100));
    lv_obj_set_flex_grow(ctx->scanned_cont, 1);
    lv_obj_set_flex_flow(ctx->scanned_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(ctx->scanned_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ctx->scanned_cont, LV_SCROLLBAR_MODE_OFF);

    settings_view_wifi_update_visibility(app);
    settings_view_wifi_update_connected(app);
    settings_view_wifi_update_scanned_list(app);

    /* 进页面自动扫一次，用户不用先点一下"重新扫描" */
    if (app->model->wifi.enabled && app->model->wifi.scanned_count == 0) {
        settings_controller_wifi_scan();
    }

    return page.screen;
}

void settings_view_wifi_init_registry(struct SettingsApp *app)
{
    PAGE_REGISTE(app, PAGE_WIFI, build_wifi_page);
    PAGE_REGISTE(app, PAGE_WIFI_PASSWORD, build_wifi_password_page);
}
