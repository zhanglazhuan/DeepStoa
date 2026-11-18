/**
 * @file view_wifi.c
 * @brief Wi-Fi 设置页
 *
 * Ported from D:\Codes\EPOS\epos\apps\settings\subpages\view_wifi.c
 *
 * 相对 EPOS 的改动：
 *   - EPOS 从 label 文本反解析 SSID（顺着 parent 往上摸三层）。这里改成把
 *     扫描列表的下标存进按钮的 user_data，SSID 带空格也不会出错。
 *   - 信息按钮使用项目的 40x40 图标，连接状态使用 LVGL 内置符号。
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
#include "lv_bottom_sheet.h"
#include "lv_floating_button.h"
#include "lv_toast.h"

#include "settings_app.h"
#include "settings_model.h"
#include "settings_view.h"
#include "controller_wifi.h"
#include "view_wifi.h"

LV_IMAGE_DECLARE(refresh);
LV_IMAGE_DECLARE(ic_info);
LV_IMAGE_DECLARE(wifi_1bar);
LV_IMAGE_DECLARE(wifi_2bar);
LV_IMAGE_DECLARE(wifi_3bar);

static const char *TAG = "settings_view_wifi";

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

/* 使用 28x28 图标，以格数表示信号强弱。 */
static const lv_image_dsc_t *rssi_icon(int8_t rssi)
{
    if (rssi > -50) return &wifi_3bar;
    if (rssi > -65) return &wifi_2bar;
    return &wifi_1bar;
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

    PAGE_NAVIGATE_TO(app, PAGE_WIFI_PASSWORD, (void *)n->ssid);
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
 * 这里改成用 28x28 Wi-Fi 图标的格数表达强弱。
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
    /* A long press opens details; only a short click may start connecting. */
    lv_obj_add_event_cb(row, on_network_clicked, LV_EVENT_SHORT_CLICKED, (void *)tag);
    lv_obj_add_event_cb(row, on_info_clicked, LV_EVENT_LONG_PRESSED, (void *)tag);

    lv_obj_t *sig = lv_image_create(row);
    lv_image_set_src(sig, rssi_icon(n->rssi));

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, n->ssid);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(name, 1);

    lv_obj_t *info_btn = lv_btn_create(row);
    lv_obj_remove_style_all(info_btn);
    lv_obj_set_size(info_btn, 40, 40);
    lv_obj_remove_flag(info_btn, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *info_icon = lv_image_create(info_btn);
    lv_image_set_src(info_icon, &ic_info);
    lv_obj_center(info_icon);
    lv_obj_add_event_cb(info_btn, on_info_clicked, LV_EVENT_CLICKED, (void *)tag);
}

/* ── 三个刷新入口 ─────────────────────────────────────────────────── */

static void update_visibility(ViewWifiCtx *ctx, settings_model_wifi_t *w)
{
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
    if (ctx->refresh_btn) {
        if (on && !w->busy) lv_obj_remove_state(ctx->refresh_btn, LV_STATE_DISABLED);
        else                lv_obj_add_state(ctx->refresh_btn, LV_STATE_DISABLED);
        if (on) lv_obj_remove_flag(ctx->refresh_btn, LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_add_flag(ctx->refresh_btn, LV_OBJ_FLAG_HIDDEN);
    }

    /* Wi-Fi 关掉时，开关以下的东西全部收起来 —— 和 NomadCast 里
     * `if (!wifi_on) return page.screen;` 是同一个意思，只是这里页面常驻，
     * 用隐藏而不是不构建。 */
    if (on) {
        lv_obj_remove_flag(ctx->scanned_cont, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->connected_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->scanned_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->scanned_cont, LV_OBJ_FLAG_HIDDEN);
    }
}

void settings_view_wifi_update_visibility(struct SettingsApp *app)
{
    ViewWifiCtx *ctx = wifi_ctx(app);
    if (!ctx) return;
    update_visibility(ctx, &app->model->wifi);
}

static void update_connected(ViewWifiCtx *ctx, settings_model_wifi_t *w)
{
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

void settings_view_wifi_update_connected(struct SettingsApp *app)
{
    ViewWifiCtx *ctx = wifi_ctx(app);
    if (!ctx) return;
    update_connected(ctx, &app->model->wifi);
}

static void update_scanned_list(ViewWifiCtx *ctx, settings_model_wifi_t *w)
{
    if (!ctx || !ctx->scanned_cont) return;

    lv_obj_clean(ctx->scanned_cont);
    lv_obj_add_flag(ctx->scanned_title, LV_OBJ_FLAG_HIDDEN);
    if (!w->enabled) return;

    /* 扫描/连接提示由 status_lbl 统一显示，忙碌时清空旧扫描结果。 */
    if (w->busy) return;

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

    lv_label_set_text_fmt(ctx->scanned_title, "Available Networks (%u)", shown);
    lv_obj_remove_flag(ctx->scanned_title, LV_OBJ_FLAG_HIDDEN);

    for (uint8_t i = 0; i < w->scanned_count; i++) {
        /* 已经连上的那个不在列表里重复出现 */
        if (w->state == WIFI_UI_CONNECTED &&
            strcmp(w->scanned[i].ssid, w->current.ssid) == 0) continue;

        create_network_row(ctx->scanned_cont, &w->scanned[i], (uintptr_t)(i + 1));
    }
}

void settings_view_wifi_update_scanned_list(struct SettingsApp *app)
{
    ViewWifiCtx *ctx = wifi_ctx(app);
    if (!ctx) return;
    update_scanned_list(ctx, &app->model->wifi);
}

/* ── Wi-Fi 主页 ───────────────────────────────────────────────────── */

static void refresh_cb(lv_event_t *e)
{
    (void)e;
    settings_controller_wifi_scan();
}

static void connected_info_cb(lv_event_t *e)
{
    on_info_clicked(e);
}

static lv_obj_t *build_wifi_page(struct SettingsApp *app)
{
    Page page = lv_page_create("Wi-Fi", true,
                               page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t *cont = page.container;
    /* Only the network list scrolls; keep the switch and list title fixed. */
    lv_obj_remove_flag(page.screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
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

    /* 当前连接：无边框，右侧提供网络详情入口。 */
    ctx->connected_row = lv_obj_create(cont);
    lv_obj_remove_style_all(ctx->connected_row);
    lv_obj_set_width(ctx->connected_row, LV_PCT(100));
    lv_obj_set_height(ctx->connected_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctx->connected_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctx->connected_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(ctx->connected_row, 12, 0);
    lv_obj_set_style_pad_column(ctx->connected_row, 10, 0);

    ctx->connected_icon = lv_label_create(ctx->connected_row);
    lv_label_set_text(ctx->connected_icon, LV_SYMBOL_OK);

    ctx->connected_lbl = lv_label_create(ctx->connected_row);
    lv_label_set_long_mode(ctx->connected_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(ctx->connected_lbl, 1);
    lv_label_set_text(ctx->connected_lbl, "");

    lv_obj_t *info_btn = lv_btn_create(ctx->connected_row);
    lv_obj_remove_style_all(info_btn);
    lv_obj_set_size(info_btn, 40, 40);
    lv_obj_t *info_icon = lv_image_create(info_btn);
    lv_image_set_src(info_icon, &ic_info);
    lv_obj_center(info_icon);
    lv_obj_add_event_cb(info_btn, connected_info_cb, LV_EVENT_CLICKED, (void *)0);

    lv_obj_add_flag(ctx->connected_row, LV_OBJ_FLAG_HIDDEN);

    /* 标题在滚动容器之外，扫描结果单独滚动。 */
    ctx->scanned_title = lv_label_create(cont);
    lv_obj_set_width(ctx->scanned_title, LV_PCT(100));
    lv_obj_set_style_text_font(ctx->scanned_title, LV_FONT_SMALL, 0);
    lv_obj_set_style_pad_ver(ctx->scanned_title, 8, 0);
    lv_obj_set_style_pad_left(ctx->scanned_title, 12, 0);
    lv_label_set_text(ctx->scanned_title, "");
    lv_obj_add_flag(ctx->scanned_title, LV_OBJ_FLAG_HIDDEN);

    ctx->scanned_cont = lv_obj_create(cont);
    lv_obj_remove_style_all(ctx->scanned_cont);
    lv_obj_set_width(ctx->scanned_cont, LV_PCT(100));
    lv_obj_set_height(ctx->scanned_cont, 0);
    lv_obj_set_flex_grow(ctx->scanned_cont, 1);
    lv_obj_set_flex_flow(ctx->scanned_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(ctx->scanned_cont, LV_DIR_VER);
    lv_obj_remove_flag(ctx->scanned_cont, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_set_scrollbar_mode(ctx->scanned_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_bottom(ctx->scanned_cont,
                                LV_FLOATING_BUTTON_SIZE + LV_FLOATING_BUTTON_MARGIN, 0);

    ctx->refresh_btn = lv_floating_button_create_image(cont, &refresh,
                                                        refresh_cb, app);

    update_visibility(ctx, &app->model->wifi);
    update_connected(ctx, &app->model->wifi);
    update_scanned_list(ctx, &app->model->wifi);

    /* 进页面自动扫一次，用户不用先点一下"重新扫描" */
    if (app->model->wifi.enabled && app->model->wifi.scanned_count == 0) {
        settings_controller_wifi_scan();
    }

    return page.screen;
}

void settings_view_wifi_init_registry(struct SettingsApp *app)
{
    PAGE_REGISTE(app, PAGE_WIFI, build_wifi_page);
}
