// view_update_download: one navigable page.
#include <stdio.h>
#include <lvgl.h>
#include "lv_bottom_sheet.h"
#include "lv_epd_region.h"
#include "lv_page.h"
#include "lv_toast.h"
#include "lv_ui_style_guide.h"
#include "page_navigator.h"
#include "settings_app.h"
#include "settings_view.h"
#include "ota.h"
#include "view_update_download.h"

static lv_bottom_sheet_t *s_sheet         = NULL;

static lv_obj_t          *s_progress_zone = NULL;   /* 进度页局部刷新区 */

static lv_obj_t          *s_bar           = NULL;

static lv_obj_t          *s_pct_label     = NULL;

static lv_obj_t          *s_hint_label    = NULL;

static lv_obj_t          *s_action_zone   = NULL;

static lv_timer_t        *s_poll          = NULL;

static int                s_shown_pct     = -1;

static ota_update_info_t  s_target;

static void toast(const char *msg) { lv_toast_show(msg, 3000); }

static void close_sheet(void)
{
    if (s_sheet) {
        lv_bottom_sheet_close(s_sheet);
        s_sheet = NULL;
    }
}

static void on_sheet_deleted(lv_event_t *e)
{
    (void)e;
    s_sheet = NULL;
}

static lv_obj_t *open_sheet(const char *title)
{
    close_sheet();
    s_sheet = lv_bottom_sheet_create(lv_screen_active());
    if (!s_sheet) return NULL;

    lv_obj_add_event_cb(s_sheet->overlay, on_sheet_deleted, LV_EVENT_DELETE, NULL);
    lv_bottom_sheet_add_header(s_sheet, title);

    lv_obj_t *content = lv_bottom_sheet_get_content(s_sheet);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 12, 0);
    return content;
}

static void on_restart_now(lv_event_t *e)
{
    (void)e;
    close_sheet();
    ota_apply_and_reboot();
}

static void on_restart_later(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    close_sheet();
    /* 新固件已经写进备用分区了，pending 这条更新的使命到此结束 */
    ota_clear_pending();
    page_navigator_navigate_pop(&app->view->page_nav, app);
    toast("Restart later to finish updating");
}

static void show_restart_sheet(SettingsApp *app)
{
    lv_obj_t *content = open_sheet("Update ready");
    if (!content) return;

    char msg[128];
    snprintf(msg, sizeof(msg),
             "v%s is installed. It takes effect after a restart.", s_target.version);
    lv_obj_t *lbl = lv_label_create(content);
    lv_label_set_text(lbl, msg);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_text_font(lbl, LV_FONT_TINY, 0);

    lv_obj_t *now = lv_button_create(content);
    ui_style_set_btn_primary(now);
    lv_obj_set_size(now, LV_PCT(100), UI_BTN_H_LARGE);
    lv_obj_add_event_cb(now, on_restart_now, LV_EVENT_CLICKED, app);
    lv_obj_t *nl = lv_label_create(now);
    lv_label_set_text(nl, "Restart now");
    lv_obj_center(nl);

    lv_obj_t *later = lv_button_create(content);
    ui_style_set_btn_secondary(later);
    lv_obj_set_size(later, LV_PCT(100), UI_BTN_H_LARGE);
    lv_obj_add_event_cb(later, on_restart_later, LV_EVENT_CLICKED, app);
    lv_obj_t *ll = lv_label_create(later);
    lv_label_set_text(ll, "Later");
    lv_obj_center(ll);
}

static void on_retry_clicked(lv_event_t *e)
{
    (void)e;
    if (s_action_zone && lv_obj_is_valid(s_action_zone)) lv_obj_clean(s_action_zone);
    s_shown_pct = -1;
    if (s_bar) lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    if (s_hint_label) lv_label_set_text(s_hint_label, "Downloading...");
    if (!ota_install_async(&s_target)) toast(ota_get_error());
}

static void on_back_clicked(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    page_navigator_navigate_pop(&app->view->page_nav, app);
}

static void show_failure_actions(SettingsApp *app)
{
    if (!s_action_zone || !lv_obj_is_valid(s_action_zone)) return;
    lv_obj_clean(s_action_zone);

    lv_obj_t *retry = lv_button_create(s_action_zone);
    ui_style_set_btn_primary(retry);
    lv_obj_set_size(retry, LV_PCT(100), UI_BTN_H_LARGE);
    lv_obj_add_event_cb(retry, on_retry_clicked, LV_EVENT_CLICKED, app);
    lv_obj_t *rl = lv_label_create(retry);
    lv_label_set_text(rl, "Retry");
    lv_obj_center(rl);

    lv_obj_t *back = lv_button_create(s_action_zone);
    ui_style_set_btn_secondary(back);
    lv_obj_set_size(back, LV_PCT(100), UI_BTN_H_LARGE);
    lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, app);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "Back");
    lv_obj_center(bl);
}

static void show_ready_actions(SettingsApp *app)
{
    if (!s_action_zone || !lv_obj_is_valid(s_action_zone)) return;
    lv_obj_clean(s_action_zone);

    lv_obj_t *now = lv_button_create(s_action_zone);
    ui_style_set_btn_primary(now);
    lv_obj_set_size(now, LV_PCT(100), UI_BTN_H_LARGE);
    lv_obj_add_event_cb(now, on_restart_now, LV_EVENT_CLICKED, app);
    lv_obj_t *nl = lv_label_create(now);
    lv_label_set_text(nl, "Restart now");
    lv_obj_center(nl);

    lv_obj_t *later = lv_button_create(s_action_zone);
    ui_style_set_btn_secondary(later);
    lv_obj_set_size(later, LV_PCT(100), UI_BTN_H_LARGE);
    lv_obj_add_event_cb(later, on_restart_later, LV_EVENT_CLICKED, app);
    lv_obj_t *ll = lv_label_create(later);
    lv_label_set_text(ll, "Later");
    lv_obj_center(ll);
}

static void poll_progress(lv_timer_t *t)
{
    SettingsApp *app = (SettingsApp *)lv_timer_get_user_data(t);
    if (!s_bar || !lv_obj_is_valid(s_bar)) return;

    switch (ota_get_state()) {
    case OTA_DOWNLOADING: {
        int pct = ota_get_progress();
        if (pct == s_shown_pct) return;          /* 没变就不刷，省一次局部刷新 */
        s_shown_pct = pct;
        lv_bar_set_value(s_bar, pct, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_pct_label, "%d%%", pct);
        break;
    }
    case OTA_READY:
        lv_bar_set_value(s_bar, 100, LV_ANIM_OFF);
        lv_label_set_text(s_pct_label, "100%");
        lv_label_set_text(s_hint_label, "Installed");
        lv_timer_del(t);
        s_poll = NULL;
        epd_region_end();                        /* 退出局部刷新，整屏刷一次再弹窗 */
        show_ready_actions(app);
        show_restart_sheet(app);
        break;
    case OTA_FAILED:
        lv_label_set_text(s_hint_label, ota_get_error()[0] ? ota_get_error()
                                                           : "Update failed");
        lv_timer_del(t);
        s_poll = NULL;
        epd_region_end();
        show_failure_actions(app);
        break;
    default:
        break;
    }
}

static void on_download_page_delete(lv_event_t *e)
{
    (void)e;
    if (s_poll) { lv_timer_del(s_poll); s_poll = NULL; }
    epd_region_end();
    s_progress_zone = NULL;
    s_bar = NULL;
    s_pct_label = NULL;
    s_hint_label = NULL;
    s_action_zone = NULL;
    close_sheet();
}

static lv_obj_t *build_download_page(struct SettingsApp *app, void *user_data)
{
    s_target = *(const ota_update_info_t *)user_data;
    /* 刷机中不给返回按钮 —— 误触返回不会中断后台任务，但会让用户以为取消了 */
    Page page = lv_page_create("Updating", false, NULL, NULL);
    lv_obj_t *cont = page.container;
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(cont, 24, 0);
    lv_obj_set_style_pad_row(cont, 24, 0);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *warn = lv_label_create(cont);
    lv_label_set_text(warn, "Do not power off");
    lv_obj_set_style_text_font(warn, LV_FONT_SMALL, 0);

    char sub[96];
    snprintf(sub, sizeof(sub), "Installing v%s", s_target.version);
    lv_obj_t *ver = lv_label_create(cont);
    lv_label_set_text(ver, sub);
    lv_obj_set_style_text_font(ver, LV_FONT_TINY, 0);

    /* 进度条 + 百分比包成一块，局部刷新只推这一块 */
    s_progress_zone = lv_obj_create(cont);
    lv_obj_remove_style_all(s_progress_zone);
    lv_obj_set_width(s_progress_zone, LV_PCT(100));
    lv_obj_set_height(s_progress_zone, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_progress_zone, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_progress_zone, 12, 0);
    lv_obj_set_flex_align(s_progress_zone, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_bar = lv_bar_create(s_progress_zone);
    lv_obj_set_size(s_bar, LV_PCT(90), 16);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);

    s_pct_label = lv_label_create(s_progress_zone);
    lv_label_set_text(s_pct_label, "0%");
    lv_obj_set_style_text_font(s_pct_label, LV_FONT_SMALL, 0);

    s_hint_label = lv_label_create(cont);
    lv_label_set_text(s_hint_label, "Downloading...");
    lv_label_set_long_mode(s_hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_hint_label, LV_PCT(100));
    lv_obj_set_style_text_align(s_hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_hint_label, LV_FONT_TINY, 0);

    /* 失败时在这里放 Retry / Back */
    s_action_zone = lv_obj_create(cont);
    lv_obj_remove_style_all(s_action_zone);
    lv_obj_set_width(s_action_zone, LV_PCT(100));
    lv_obj_set_height(s_action_zone, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_action_zone, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_action_zone, 12, 0);

    s_shown_pct = -1;
    lv_obj_add_event_cb(page.screen, on_download_page_delete, LV_EVENT_DELETE, NULL);

    /* 整页只有进度条那块在变 —— 把面板刷新窗口钉死在那儿，
     * 否则每秒整屏闪一次，墨水屏上非常难看。 */
    epd_region_begin_focus(page.screen, s_progress_zone);
    s_poll = lv_timer_create(poll_progress, 1000, app);

    return page.screen;
}

void settings_view_update_download_init_registry(struct SettingsApp *app)
{
    PAGE_REGISTE(app, PAGE_UPDATES_DOWNLOAD, build_download_page);
}
