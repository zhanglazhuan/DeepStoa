// apps/settings/subpages/view_update.c
// System Update —— 查更新 / 二次确认 / 下载进度 / 重启确认
//
// 交互链路（两次弹窗都是有意的，刷机不可逆，必须让用户明确点两次）：
//
//   [System Update 页] 点 Check for Update
//        → 有新版本就在页面里出一张描边卡片（版本 / 日期 / 大小 / changelog）
//        → 其余结果（已是最新 / 没网 / 服务端错误）走 toast，不在页面留残留文字
//   [卡片] 点 Download & Install
//        → 弹窗 1：升级须知（保持供电、耗时、电量要求）→ Cancel / Confirm
//   [Confirm]
//        → 跳进度页，后台任务下载 + 写 flash，进度条走 0..100
//   [写完]
//        → 弹窗 2：Restart now / Later —— 不自动重启，什么时候生效由用户定
//
// 墨水屏注意：进度页用 epd_region_* 把刷新限制在进度条那一小块，
// 否则每秒整屏闪一次。

#include <stdio.h>
#include <string.h>

#include <lvgl.h>
#include "esp_log.h"

#include "lv_bottom_sheet.h"
#include "lv_epd_region.h"
#include "lv_page.h"
#include "lv_toast.h"
#include "lv_ui_style_guide.h"
#include "page_navigator.h"
#include "settings_app.h"
#include "settings_controller.h"
#include "settings_model.h"
#include "settings_view.h"
#include "view_update.h"

#include "ota.h"
#include "wifi_manager.h"

static const char *TAG = "settings_update";

/* ── 页面态 ───────────────────────────────────────────────────────────
 * 页面每次导航都会重建，这些指针在 builder 里赋值、在 LV_EVENT_DELETE 里清空。
 * 异步回调回来时必须先判空 —— 用户完全可能在查更新的过程中退出去。 */
static lv_obj_t          *s_result_zone   = NULL;   /* 主页动态区 */
static lv_obj_t          *s_check_btn     = NULL;
static lv_bottom_sheet_t *s_sheet         = NULL;

static lv_obj_t          *s_progress_zone = NULL;   /* 进度页局部刷新区 */
static lv_obj_t          *s_bar           = NULL;
static lv_obj_t          *s_pct_label     = NULL;
static lv_obj_t          *s_hint_label    = NULL;
static lv_obj_t          *s_action_zone   = NULL;
static lv_timer_t        *s_poll          = NULL;
static int                s_shown_pct     = -1;

/* 用户在弹窗 1 里确认要装的那一版 —— 从 pending 拷一份，避免后台再查一次把它换掉 */
static ota_update_info_t  s_target;

/* ── 小工具 ─────────────────────────────────────────────────────────── */

static void toast(const char *msg) { lv_toast_show(msg, 3000); }

static const char *manifest_url_of(SettingsApp *app)
{
    /* 空串 = 用 ota.h 里的编译期默认地址 */
    return app->model->update.manifest_url[0] ? app->model->update.manifest_url : NULL;
}

/** 当前电量，-1 表示拿不到。
 *  system/battery 目前还没进构建（状态栏用的是 mock），所以这里返回 -1，
 *  弹窗只提示电量要求、不做硬拦截。接进来之后改成 battery_get_percent() 即可。 */
static int current_battery_percent(void) { return -1; }

static void size_to_text(int bytes, char *out, size_t max)
{
    if (bytes <= 0) snprintf(out, max, "unknown size");
    else if (bytes < 1024 * 1024) snprintf(out, max, "%d KB", bytes / 1024);
    else snprintf(out, max, "%d.%d MB", bytes / (1024 * 1024),
                  (bytes % (1024 * 1024)) / (1024 * 1024 / 10));
}

static void close_sheet(void)
{
    if (s_sheet) {
        lv_bottom_sheet_close(s_sheet);
        s_sheet = NULL;
    }
}

/** sheet 的 X 按钮、点遮罩、以及所属 screen 被删都会把 overlay 删掉，
 *  同时 lv_free 掉 lv_bottom_sheet_t 本身 —— 不清指针后面 close 就是野指针。 */
static void on_sheet_deleted(lv_event_t *e)
{
    (void)e;
    s_sheet = NULL;
}

/** 建一个 bottom sheet，返回配好 flex 的 content 容器。 */
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

/** 清空动态区（版本卡片 / 提示文字）。 */
static void clear_result_zone(void)
{
    if (!s_result_zone || !lv_obj_is_valid(s_result_zone)) return;
    lv_obj_clean(s_result_zone);
}

/** 在动态区放一行常驻提示。只用于"检查中"这种持续几秒的状态 ——
 *  查更新的最终结果统一走 toast，和 NomadCast 一致，不在页面里留下残留文字。 */
static void show_status(const char *text)
{
    if (!s_result_zone || !lv_obj_is_valid(s_result_zone)) return;
    lv_obj_clean(s_result_zone);

    lv_obj_t *lbl = lv_label_create(s_result_zone);
    lv_label_set_text(lbl, text);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_text_font(lbl, LV_FONT_TINY, 0);
}

/** changelog 按行拆开渲染，最多 max_lines 行，每行单行截断不换行。 */
static void add_changelog_lines(lv_obj_t *parent, const char *changelog, int max_lines)
{
    if (!changelog || !changelog[0]) return;

    char buf[OTA_CHANGELOG_LEN];
    snprintf(buf, sizeof(buf), "%s", changelog);

    int   shown = 0;
    char *line  = buf;
    for (char *p = buf; ; p++) {
        if (*p != '\n' && *p != '\0') continue;
        bool last = (*p == '\0');
        *p = '\0';
        if (line[0] && shown < max_lines) {
            lv_obj_t *lbl = lv_label_create(parent);
            lv_label_set_text(lbl, line);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
            lv_obj_set_width(lbl, LV_PCT(100));
            lv_obj_set_style_text_font(lbl, LV_FONT_TINY, 0);
            shown++;
        }
        if (last || shown >= max_lines) break;
        line = p + 1;
    }
}

/* ── 弹窗 2：装好了，要不要现在重启 ─────────────────────────────────── */

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

/* ── 进度页 ─────────────────────────────────────────────────────────── */

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

/** 装好之后在页面上也留一份 Restart / Later —— 进度页没有返回按钮，
 *  用户要是把弹窗点掉（X 或点遮罩）就出不去了。 */
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

/** 1 秒轮询一次升级状态。墨水屏刷不动更快，也没必要。 */
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

static lv_obj_t *build_download_page(struct SettingsApp *app)
{
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

/* ── 弹窗 1：升级前的二次确认 ───────────────────────────────────────── */

static void on_confirm_install(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    close_sheet();

    ESP_LOGI(TAG, "user confirmed install of v%s", s_target.version);
    PAGE_NAVIGATE_TO(app, PAGE_UPDATES_DOWNLOAD, NULL);

    if (!ota_install_async(&s_target)) {
        toast(ota_get_error()[0] ? ota_get_error() : "Cannot start update");
    }
}

static void on_cancel_install(lv_event_t *e)
{
    (void)e;
    close_sheet();
}

static void show_confirm_sheet(SettingsApp *app)
{
    char title[64];
    snprintf(title, sizeof(title), "Install v%s?", s_target.version);

    lv_obj_t *content = open_sheet(title);
    if (!content) return;

    int battery = current_battery_percent();
    int min_batt = s_target.min_battery > 0 ? s_target.min_battery : 30;

    char notes[288];
    char size_text[32];
    size_to_text(s_target.size, size_text, sizeof(size_text));
    /* 两条硬性要求放最前面 —— 用户大概率只看第一行 */
    snprintf(notes, sizeof(notes),
             "%s"
             "Battery must be at least %d%%.\n"
             "Do not power off during the update.\n"
             "\n"
             "Downloads %s over Wi-Fi, about 2 minutes.\n"
             "The device restarts to apply the update.",
             s_target.mandatory ? "This is a required update.\n\n" : "",
             min_batt, size_text);

    lv_obj_t *lbl = lv_label_create(content);
    lv_label_set_text(lbl, notes);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_text_font(lbl, LV_FONT_TINY, 0);

    bool battery_ok = (battery < 0) || (battery >= min_batt);
    if (!battery_ok) {
        lv_obj_t *warn = lv_label_create(content);
        lv_label_set_text_fmt(warn, "Battery is %d%% - charge before updating.", battery);
        lv_label_set_long_mode(warn, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(warn, LV_PCT(100));
        lv_obj_set_style_text_font(warn, LV_FONT_TINY, 0);
    }

    lv_obj_t *confirm = lv_button_create(content);
    ui_style_set_btn_primary(confirm);
    lv_obj_set_size(confirm, LV_PCT(100), UI_BTN_H_LARGE);
    lv_obj_t *cl = lv_label_create(confirm);
    lv_label_set_text(cl, "Confirm");
    lv_obj_center(cl);
    if (battery_ok) {
        lv_obj_add_event_cb(confirm, on_confirm_install, LV_EVENT_CLICKED, app);
    } else {
        lv_obj_add_state(confirm, LV_STATE_DISABLED);
    }

    lv_obj_t *cancel = lv_button_create(content);
    ui_style_set_btn_secondary(cancel);
    lv_obj_set_size(cancel, LV_PCT(100), UI_BTN_H_LARGE);
    lv_obj_add_event_cb(cancel, on_cancel_install, LV_EVENT_CLICKED, app);
    lv_obj_t *xl = lv_label_create(cancel);
    lv_label_set_text(xl, "Cancel");
    lv_obj_center(xl);
}

/* ── 主页：新版本卡片 ───────────────────────────────────────────────── */

static void on_install_clicked(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);

    const ota_update_info_t *info = ota_get_pending();
    if (!info) { toast("No update available"); return; }

    s_target = *info;          /* 拷一份，后面全程用这份，不再看 pending */
    show_confirm_sheet(app);
}

static void render_update_card(SettingsApp *app, const ota_update_info_t *info)
{
    if (!s_result_zone || !lv_obj_is_valid(s_result_zone)) return;
    lv_obj_clean(s_result_zone);

    /* 描边卡片。NomadCast 用 0xCCCCCC 的浅灰边把"新版本"这块圈出来，
     * 1bit 屏上没有灰，改 1px 黑边 —— 和本工程"普通卡片 1px"的约定一致。 */
    lv_obj_t *card = lv_obj_create(s_result_zone);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_black(), 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    /* 信息区和按钮区分开，信息区行距更紧 */
    lv_obj_t *info_box = lv_obj_create(card);
    lv_obj_remove_style_all(info_box);
    lv_obj_set_width(info_box, LV_PCT(100));
    lv_obj_set_height(info_box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(info_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(info_box, 4, 0);
    lv_obj_remove_flag(info_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(info_box, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(info_box);
    lv_label_set_text_fmt(title, "Version %s", info->version);
    lv_obj_set_style_text_font(title, LV_FONT_SMALL, 0);

    if (info->date[0]) {
        lv_obj_t *date = lv_label_create(info_box);
        lv_label_set_text_fmt(date, "Released: %s", info->date);
        lv_obj_set_style_text_font(date, LV_FONT_TINY, 0);
    }

    char size_text[32];
    size_to_text(info->size, size_text, sizeof(size_text));
    lv_obj_t *size_lbl = lv_label_create(info_box);
    lv_label_set_text_fmt(size_lbl, "Size: %s", size_text);
    lv_obj_set_style_text_font(size_lbl, LV_FONT_TINY, 0);

    if (info->mandatory) {
        lv_obj_t *req = lv_label_create(info_box);
        lv_label_set_text(req, LV_SYMBOL_WARNING " Required update");
        lv_obj_set_style_text_font(req, LV_FONT_TINY, 0);
    }

    /* changelog 最多 3 条，每条单行截断 —— 卡片高度可预期，不会把按钮挤出屏幕 */
    add_changelog_lines(info_box, info->changelog, 3);

    lv_obj_t *install = lv_button_create(card);
    ui_style_set_btn_primary(install);
    lv_obj_set_size(install, LV_PCT(100), UI_BTN_H_LARGE);
    lv_obj_add_event_cb(install, on_install_clicked, LV_EVENT_CLICKED, app);
    lv_obj_t *il = lv_label_create(install);
    lv_label_set_text(il, "Download & Install");
    lv_obj_center(il);
}

/* ── 主页：查更新 ───────────────────────────────────────────────────── */

static void on_check_done(ota_check_result_t result, const ota_update_info_t *info,
                          void *user_data)
{
    SettingsApp *app = (SettingsApp *)user_data;

    /* 查更新过程中用户可能已经退出这一页了 */
    if (!s_result_zone || !lv_obj_is_valid(s_result_zone)) return;
    if (s_check_btn && lv_obj_is_valid(s_check_btn)) {
        lv_obj_clear_state(s_check_btn, LV_STATE_DISABLED);
    }

    switch (result) {
    case OTA_CHECK_UPDATE_AVAILABLE:
        render_update_card(app, info);
        break;
    case OTA_CHECK_UP_TO_DATE:
        clear_result_zone();
        toast("Already up to date");
        break;
    case OTA_CHECK_ERR_OFFLINE:
        clear_result_zone();
        toast("No network connection");
        break;
    case OTA_CHECK_ERR_NO_URL:
        clear_result_zone();
        toast("Update URL not configured");
        break;
    case OTA_CHECK_ERR_NETWORK:
        clear_result_zone();
        toast("Server unreachable");
        break;
    case OTA_CHECK_ERR_PARSE:
        clear_result_zone();
        toast("Invalid server response");
        break;
    case OTA_CHECK_ERR_BUSY:
        clear_result_zone();
        toast("A check is already running");
        break;
    }
}

static void on_check_clicked(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);

    if (!wifi_is_connected()) {
        toast("No network connection");
        return;
    }

    /* 这条是常驻的：查更新要几秒，墨水屏上没有转圈动画，
     * 必须留一行字告诉用户点下去了 —— toast 3 秒就没了，撑不到结果回来。 */
    show_status("Checking for updates...");
    if (s_check_btn) lv_obj_add_state(s_check_btn, LV_STATE_DISABLED);

    if (!ota_check_async(manifest_url_of(app), on_check_done, app)) {
        if (s_check_btn) lv_obj_clear_state(s_check_btn, LV_STATE_DISABLED);
    }
}

static void on_update_page_delete(lv_event_t *e)
{
    (void)e;
    s_result_zone = NULL;
    s_check_btn   = NULL;
    close_sheet();
}

static lv_obj_t *build_update_page(struct SettingsApp *app)
{
    Page page = lv_page_create("Update", true,
                               page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t *cont = page.container;
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(cont, 16, 0);

    /* 当前版本 */
    lv_obj_t *cur_title = lv_label_create(cont);
    lv_label_set_text(cur_title, "Current version");

    lv_obj_t *cur_ver = lv_label_create(cont);
    lv_label_set_text(cur_ver, ota_running_version());

    /* 自动更新：标题在上，开关在下。开关只管"开机后台查一次"，
     * 查到了也不会自己刷 —— 刷机必须用户点确认。 */
    lv_obj_t *auto_row = lv_obj_create(cont);
    lv_obj_remove_style_all(auto_row);
    lv_obj_set_width(auto_row, LV_PCT(100));
    lv_obj_set_height(auto_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(auto_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(auto_row, 8, 0);

    lv_obj_t *auto_lbl = lv_label_create(auto_row);
    lv_label_set_text(auto_lbl, "Auto Update");

    lv_obj_t *auto_sw = lv_switch_create(auto_row);
    ui_style_set_switch(auto_sw);
    if (app->model->update.auto_check_enabled) lv_obj_add_state(auto_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(auto_sw, settings_controller_set_auto_update,
                        LV_EVENT_VALUE_CHANGED, app);

    /* 查更新 —— 白底描边，不抢 Download & Install 的强调 */
    s_check_btn = lv_button_create(cont);
    ui_style_set_btn_secondary(s_check_btn);
    lv_obj_set_style_bg_opa(s_check_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_check_btn, lv_color_white(), 0);
    lv_obj_set_size(s_check_btn, LV_PCT(100), UI_BTN_H_LARGE);
    lv_obj_add_event_cb(s_check_btn, on_check_clicked, LV_EVENT_CLICKED, app);
    lv_obj_t *cb_lbl = lv_label_create(s_check_btn);
    lv_label_set_text(cb_lbl, "Check for Update");
    lv_obj_center(cb_lbl);

    /* 动态区：检查结果 / 新版本卡片 */
    s_result_zone = lv_obj_create(cont);
    lv_obj_remove_style_all(s_result_zone);
    lv_obj_set_width(s_result_zone, LV_PCT(100));
    lv_obj_set_height(s_result_zone, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_result_zone, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_result_zone, 8, 0);

    /* 开机自动查到的结果直接摆出来，用户不用再点一次 */
    const ota_update_info_t *pending = ota_get_pending();
    if (pending) render_update_card(app, pending);

    lv_obj_add_event_cb(page.screen, on_update_page_delete, LV_EVENT_DELETE, NULL);
    return page.screen;
}

/* ── 注册 ───────────────────────────────────────────────────────────── */

void settings_view_update_init_registry(SettingsApp *app)
{
    PAGE_REGISTE(app, PAGE_UPDATES, build_update_page);
    PAGE_REGISTE(app, PAGE_UPDATES_DOWNLOAD, build_download_page);
    /* PAGE_UPDATES_SD_SELECT（SD 卡离线升级）暂未实现 */
}
