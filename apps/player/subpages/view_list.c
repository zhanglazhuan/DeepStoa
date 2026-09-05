/**
 * @file view_list.c
 * @brief 曲目列表页
 *
 * - 曲目来自 PLAYER_MUSIC_DIR，由 player_library 扫描（真实 FS 或 mock）
 * - 每行左侧 checkbox 多选，底部「加入播放列表」批量入队
 * - 每行右侧垃圾桶删除，带二次确认
 * - 不支持的格式仍然列出来（灰显 + 标记），点开时弹窗说明，而不是直接隐藏
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "lv_theme_hardcore.h"
#include "lv_toast.h"
#include "esp_log.h"

#include "../app.h"
#include "../view.h"
#include "../controller.h"
#include "../service/player_library.h"
#include "audio_service.h"

static const char *TAG = "player_view_list";

typedef struct {
    lv_obj_t *list_container;
    lv_obj_t *lbl_summary;
    lv_obj_t *btn_enqueue;
} ViewListCtx;

static ViewListCtx *s_ctx;
static uint16_t     s_pending_delete;   /* 待删除的曲目下标 */

static void rebuild(PlayerApp *app);

/* ── 通用弹窗 ─────────────────────────────────────────────────────── */

static void dialog_dismiss_cb(lv_event_t *e)
{
    lv_obj_delete(lv_event_get_user_data(e));
}

/**
 * @param ok_text  为 NULL 时只有一个「知道了」按钮（纯提示）
 */
static lv_obj_t *make_dialog(const char *msg, const char *ok_text,
                             lv_event_cb_t ok_cb, void *ok_ud)
{
    lv_obj_t *dlg = lv_obj_create(lv_screen_active());
    lv_obj_set_size(dlg, LV_PCT(86), LV_SIZE_CONTENT);
    lv_obj_center(dlg);
    lv_obj_set_flex_flow(dlg, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(dlg, 16, 0);
    lv_obj_set_style_pad_row(dlg, 14, 0);
    lv_obj_set_style_radius(dlg, 2, 0);
    lv_obj_set_style_bg_color(dlg, lv_color_white(), 0);
    lv_obj_set_style_border_width(dlg, 2, 0);
    lv_obj_set_style_border_color(dlg, lv_color_black(), 0);
    lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(dlg);
    lv_label_set_text(lbl, msg);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(100));

    lv_obj_t *row = lv_obj_create(dlg);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, ok_text ? LV_FLEX_ALIGN_SPACE_AROUND : LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancel = lv_btn_create(row);
    ui_style_set_btn_secondary(cancel);
    lv_obj_set_size(cancel, ok_text ? 120 : 160, 48);
    lv_obj_set_style_radius(cancel, 2, 0);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, ok_text ? "Cancel" : "OK");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel, dialog_dismiss_cb, LV_EVENT_CLICKED, dlg);

    if (ok_text) {
        lv_obj_t *ok = lv_btn_create(row);
        ui_style_set_btn_primary(ok);
        lv_obj_set_size(ok, 120, 48);
        lv_obj_set_style_radius(ok, 2, 0);
        lv_obj_t *ol = lv_label_create(ok);
        lv_label_set_text(ol, ok_text);
        lv_obj_center(ol);
        lv_obj_add_event_cb(ok, ok_cb, LV_EVENT_CLICKED, ok_ud ? ok_ud : dlg);
        lv_obj_add_event_cb(ok, dialog_dismiss_cb, LV_EVENT_CLICKED, dlg);
    }
    return dlg;
}

/* ── 删除 ─────────────────────────────────────────────────────────── */

static void confirm_delete_cb(lv_event_t *e)
{
    (void)e;
    PlayerApp *app = &g_player_app;

    if (!player_controller_delete_track(s_pending_delete)) {
        lv_toast_show("Delete failed", 2000);
        return;
    }
    lv_toast_show("Deleted", 1500);
    rebuild(app);
}

static void delete_btn_cb(lv_event_t *e)
{
    uint16_t idx = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    PlayerApp *app = &g_player_app;
    if (idx >= app->model->track_count) return;

    s_pending_delete = idx;

    char msg[192];
    snprintf(msg, sizeof(msg), "Delete \"%s\"?\nThe file will be permanently removed from the SD card.",
             app->model->tracks[idx].title);
    make_dialog(msg, "Delete", confirm_delete_cb, NULL);
}

/* ── 多选 ─────────────────────────────────────────────────────────── */

static void update_summary(PlayerApp *app)
{
    if (!s_ctx) return;

    uint16_t sel = player_model_selected_count(app->model);
    lv_label_set_text_fmt(s_ctx->lbl_summary, "%u tracks  -  %u selected  -  queue %u",
                          app->model->track_count, sel, audio_service_queue_count());

    if (sel > 0) lv_obj_remove_state(s_ctx->btn_enqueue, LV_STATE_DISABLED);
    else         lv_obj_add_state(s_ctx->btn_enqueue, LV_STATE_DISABLED);
}

static void checkbox_cb(lv_event_t *e)
{
    uint16_t idx = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    PlayerApp *app = &g_player_app;
    if (idx >= app->model->track_count) return;

    lv_obj_t *cb = lv_event_get_target(e);
    app->model->tracks[idx].selected = lv_obj_has_state(cb, LV_STATE_CHECKED);
    update_summary(app);
}

static void enqueue_cb(lv_event_t *e)
{
    PlayerApp *app = lv_event_get_user_data(e);

    /* 选中项里有不支持的格式，先说清楚会被跳过 */
    uint16_t skipped = 0;
    for (uint16_t i = 0; i < app->model->track_count; i++) {
        if (app->model->tracks[i].selected && !app->model->tracks[i].supported) skipped++;
    }

    uint16_t added = player_controller_enqueue_selected();

    if (added == 0 && skipped > 0) {
        make_dialog("All selected files are unsupported, nothing was queued.\n"
                    "Supported formats: .mp3 and .wav.", NULL, NULL, NULL);
    } else if (skipped > 0) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "Queued %u track(s).\n%u file(s) skipped - unsupported format.", added, skipped);
        make_dialog(msg, NULL, NULL, NULL);
    } else {
        char t[64];
        snprintf(t, sizeof(t), "Queued %u track(s)", added);
        lv_toast_show(t, 1500);
    }
    rebuild(app);
}

/* ── 点击曲目 ─────────────────────────────────────────────────────── */

static void track_click_cb(lv_event_t *e)
{
    uint16_t idx = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    PlayerApp *app = &g_player_app;
    if (idx >= app->model->track_count) return;

    audio_err_t err = player_controller_play_track(idx);

    if (err == AUDIO_ERR_FORMAT) {
        char msg[224];
        snprintf(msg, sizeof(msg),
                 "\"%s\" is not a supported audio format.\nSupported formats: .mp3 and .wav.",
                 app->model->tracks[idx].title);
        make_dialog(msg, NULL, NULL, NULL);
        return;
    }
    if (err != AUDIO_OK) {
        make_dialog(audio_err_text(err), NULL, NULL, NULL);
        return;
    }

    PAGE_NAVIGATE_TO(app, PAGE_PLAYER_PLAY, NULL);
}

/* 进播放页查看当前曲目（不重新打开，保留播放位置） */
static void open_now_playing_cb(lv_event_t *e)
{
    PlayerApp *app = lv_event_get_user_data(e);
    if (!audio_service_current()) {
        lv_toast_show("The queue is empty", 1500);
        return;
    }
    PAGE_NAVIGATE_TO(app, PAGE_PLAYER_PLAY, NULL);
}

/* ── 刷新与重扫 ───────────────────────────────────────────────────── */

static void rescan_cb(lv_event_t *e)
{
    PlayerApp *app = lv_event_get_user_data(e);
    player_controller_rescan();
    rebuild(app);
    lv_toast_show(player_library_using_mock() ? "Rescanned (mock data)" : "Rescanned", 1500);
}

static void fmt_size(uint32_t bytes, char *out, size_t n)
{
    if (bytes >= 1024u * 1024u) snprintf(out, n, "%lu.%lu MB",
                                         (unsigned long)(bytes / (1024u * 1024u)),
                                         (unsigned long)((bytes % (1024u * 1024u)) / 104858u));
    else                        snprintf(out, n, "%lu KB", (unsigned long)(bytes / 1024u));
}

static void rebuild(PlayerApp *app)
{
    if (!s_ctx || !s_ctx->list_container) return;
    lv_obj_clean(s_ctx->list_container);

    PlayerModel *m = app->model;

    if (m->track_count == 0) {
        lv_obj_t *empty = lv_label_create(s_ctx->list_container);
        lv_label_set_text_fmt(empty, "No tracks in %s\nPut .mp3 / .wav files in this folder",
                              PLAYER_MUSIC_DIR);
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(empty, 40, 0);
        update_summary(app);
        return;
    }

    const audio_track_t *cur = audio_service_current();

    for (uint16_t i = 0; i < m->track_count; i++) {
        TrackInfo *t = &m->tracks[i];
        bool playing = (cur && strcmp(cur->path, t->filepath) == 0);
        bool queued  = audio_service_queue_contains(t->filepath);

        lv_obj_t *item = lv_obj_create(s_ctx->list_container);
        lv_obj_set_width(item, LV_PCT(100));
        lv_obj_set_height(item, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(item, 8, 0);
        lv_obj_set_style_pad_column(item, 8, 0);
        lv_obj_set_style_radius(item, 2, 0);
        lv_obj_set_style_border_width(item, playing ? 2 : 1, 0);
        lv_obj_set_style_border_color(item, lv_color_black(), 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        /* 多选 */
        lv_obj_t *cb = lv_checkbox_create(item);
        lv_checkbox_set_text(cb, "");
        if (t->selected) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_obj_add_event_cb(cb, checkbox_cb, LV_EVENT_VALUE_CHANGED,
                            (void *)(uintptr_t)i);

        /* 标题 + 元信息，点击播放 */
        lv_obj_t *info = lv_obj_create(item);
        lv_obj_remove_style_all(info);
        lv_obj_set_flex_grow(info, 1);
        lv_obj_set_height(info, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(info, 4, 0);
        lv_obj_add_flag(info, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(info, track_click_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);

        lv_obj_t *title = lv_label_create(info);
        lv_label_set_text_fmt(title, "%s%s%s",
                              playing ? LV_SYMBOL_PLAY " " : "",
                              (!playing && queued) ? LV_SYMBOL_LIST " " : "",
                              t->title);
        lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(title, LV_PCT(100));

        char size_buf[24];
        if (t->duration_ms > 0) {
            uint32_t sec = t->duration_ms / 1000;
            snprintf(size_buf, sizeof(size_buf), "%lu:%02lu",
                     (unsigned long)(sec / 60), (unsigned long)(sec % 60));
        } else {
            fmt_size(t->size, size_buf, sizeof(size_buf));
        }
        lv_obj_t *meta = lv_label_create(info);
        if (t->supported) {
            lv_label_set_text(meta, size_buf);
        } else {
            /* 不隐藏，明确标出来 —— 比"文件明明在却看不到"好排查 */
            lv_label_set_text_fmt(meta, LV_SYMBOL_WARNING " Unsupported - %s", size_buf);
        }
        lv_obj_set_style_text_font(meta, LV_FONT_SMALL, 0);

        /* 删除 */
        lv_obj_t *del = lv_btn_create(item);
        ui_style_set_btn_secondary(del);
        lv_obj_set_size(del, 56, 48);
        lv_obj_set_style_radius(del, 2, 0);
        lv_obj_t *di = lv_label_create(del);
        lv_label_set_text(di, LV_SYMBOL_TRASH);
        lv_obj_center(di);
        lv_obj_add_event_cb(del, delete_btn_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
    }

    update_summary(app);
}

/* ── 页面构建 ─────────────────────────────────────────────────────── */

/* 后台切歌 / 队列变化时，列表页的"正在播放"标记要跟着变 */
static void on_audio_event(audio_event_t ev, audio_err_t err, void *user_data)
{
    (void)err;
    if (ev == AUDIO_EV_TRACK_CHANGED || ev == AUDIO_EV_QUEUE_CHANGED) {
        if (s_ctx) rebuild(user_data);
    }
}

static void page_delete_cb(lv_event_t *e)
{
    audio_service_unsubscribe(on_audio_event);
    if (s_ctx == lv_event_get_user_data(e)) s_ctx = NULL;
}

static lv_obj_t* build_player_list_page(PlayerApp* app, void* user_data)
{
    (void)user_data;
    Page page = lv_page_create("Music", false, NULL, &app->view->page_nav);

    ViewListCtx *ctx = malloc(sizeof(ViewListCtx));
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to allocate list page context");
        return NULL;
    }
    memset(ctx, 0, sizeof(ViewListCtx));
    app->view->page_nav.nav_ctx = ctx;
    s_ctx = ctx;

    /* header 右侧：重扫目录 */
    if (page.header_right) {
        lv_obj_t *rescan = lv_label_create(page.header_right);
        lv_label_set_text(rescan, LV_SYMBOL_REFRESH);
        lv_obj_center(rescan);
        lv_obj_add_flag(rescan, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(rescan, 15);
        lv_obj_add_event_cb(rescan, rescan_cb, LV_EVENT_CLICKED, app);
    }

    lv_obj_clear_flag(page.container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(page.container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(page.container, 0, 0);

    /* 概要行 */
    ctx->lbl_summary = lv_label_create(page.container);
    lv_obj_set_style_text_font(ctx->lbl_summary, LV_FONT_SMALL, 0);
    lv_obj_set_style_pad_hor(ctx->lbl_summary, 10, 0);
    lv_obj_set_style_pad_ver(ctx->lbl_summary, 6, 0);

    /* 列表 */
    ctx->list_container = lv_obj_create(page.container);
    lv_obj_set_width(ctx->list_container, LV_PCT(100));
    lv_obj_set_flex_grow(ctx->list_container, 1);
    lv_obj_set_flex_flow(ctx->list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(ctx->list_container, 8, 0);
    lv_obj_set_style_pad_row(ctx->list_container, 8, 0);
    lv_obj_set_style_border_width(ctx->list_container, 0, 0);
    lv_obj_set_scroll_dir(ctx->list_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ctx->list_container, LV_SCROLLBAR_MODE_OFF);
    app->view->list_container = ctx->list_container;

    /* 底部动作条 */
    lv_obj_t *footer = lv_obj_create(page.container);
    lv_obj_set_width(footer, LV_PCT(100));
    lv_obj_set_height(footer, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_pad_all(footer, 10, 0);
    lv_obj_set_style_pad_column(footer, 8, 0);

    ctx->btn_enqueue = lv_btn_create(footer);
    ui_style_set_btn_primary(ctx->btn_enqueue);
    lv_obj_set_flex_grow(ctx->btn_enqueue, 1);
    lv_obj_set_height(ctx->btn_enqueue, 50);
    lv_obj_set_style_radius(ctx->btn_enqueue, 2, 0);
    lv_obj_t *el = lv_label_create(ctx->btn_enqueue);
    lv_label_set_text(el, LV_SYMBOL_PLUS "  Add to Queue");
    lv_obj_center(el);
    lv_obj_add_event_cb(ctx->btn_enqueue, enqueue_cb, LV_EVENT_CLICKED, app);

    lv_obj_t *btn_now = lv_btn_create(footer);
    ui_style_set_btn_secondary(btn_now);
    lv_obj_set_size(btn_now, 90, 50);
    lv_obj_set_style_radius(btn_now, 2, 0);
    lv_obj_t *nl = lv_label_create(btn_now);
    lv_label_set_text(nl, LV_SYMBOL_AUDIO);
    lv_obj_center(nl);
    lv_obj_add_event_cb(btn_now, open_now_playing_cb, LV_EVENT_CLICKED, app);

    lv_obj_add_event_cb(page.screen, page_delete_cb, LV_EVENT_DELETE, ctx);
    audio_service_subscribe(on_audio_event, app);

    rebuild(app);
    return page.screen;
}

void player_view_list_init_registry(struct PlayerApp* app) {
    PAGE_REGISTE(app, PAGE_PLAYER_LIST, build_player_list_page);
}
