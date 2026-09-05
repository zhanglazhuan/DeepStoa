/**
 * @file view_play.c
 * @brief 播放页 —— 重点是进度条和播放图标的局部刷新
 *
 * 墨水屏整屏刷新会明显闪一下，所以这一页把面板更新拆成两个独立的小窗口：
 *
 *   progress_zone   [已播时间] [====进度条====] [总时长]
 *   btn_play        播放 / 暂停 图标
 *   opts_zone       播放模式 + 音量
 *
 * 页面建好后调一次 epd_region_begin()，用一整帧把静态内容（标题、上一首/
 * 下一首按钮、边框）打到面板并同步差分基准；此后每次更新只把对应窗口推给
 * 面板，其余像素驱动层根本不发送 —— 屏幕上不会有整屏黑白翻转。
 *
 * 两个窗口交替切换即可，不需要取并集：epd_region_flush_obj() 每次会把面板
 * 窗口切到该对象的矩形。同一帧只刷一个区域。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "lv_theme_hardcore.h"
#include "lv_bottom_sheet.h"
#include "lv_epd_region.h"
#include "lv_toast.h"
#include "esp_log.h"

#include "../app.h"
#include "../view.h"
#include "../controller.h"
#include "audio_service.h"

static const char *TAG = "player_view_play";

/* 进度 tick 1Hz，但面板最多每 PUSH_S 秒推一次。
 * 墨水屏一次局部刷新要几百毫秒，1Hz 推面板等于持续刷新，既晃眼又费电。
 * 用户操作（快进 / 播放暂停 / 切歌）会 force 立刻推，所以手感不受影响。 */
#define PROGRESS_TICK_MS    1000
#define PROGRESS_PUSH_S     5
#define SEEK_STEP_MS        15000

typedef struct {
    lv_obj_t *lbl_title;
    lv_obj_t *lbl_index;

    lv_obj_t *progress_zone;   /* 局部刷新窗口 1 */
    lv_obj_t *bar;
    lv_obj_t *lbl_elapsed;
    lv_obj_t *lbl_total;

    lv_obj_t *btn_play;        /* 局部刷新窗口 2 */
    lv_obj_t *lbl_btn_play;

    lv_obj_t *opts_zone;       /* 局部刷新窗口 3：模式 + 音量 */
    lv_obj_t *lbl_mode;
    lv_obj_t *lbl_volume;

    lv_timer_t *tick;
    uint32_t    last_push_s;   /* 上次推面板时的秒数 */
} ViewPlayCtx;

static ViewPlayCtx *s_ctx;   /* 播放页在前台时非 NULL */

static void fmt_ms(uint32_t ms, char *buf, size_t n)
{
    uint32_t s = ms / 1000;
    snprintf(buf, n, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

/* ── 局部刷新：进度区 ─────────────────────────────────────────────── */

static void render_progress(PlayerApp *app)
{
    ViewPlayCtx *ctx = s_ctx;
    if (!ctx) return;
    (void)app;

    uint32_t pos = audio_service_position_ms();
    uint32_t dur = audio_service_duration_ms();
    char buf[16];

    fmt_ms(pos, buf, sizeof(buf));
    lv_label_set_text(ctx->lbl_elapsed, buf);

    fmt_ms(dur, buf, sizeof(buf));
    lv_label_set_text(ctx->lbl_total, buf);

    int32_t pct = 0;
    if (dur > 0) {
        pct = (int32_t)((uint64_t)pos * 1000u / dur);
    }
    /* LV_ANIM_OFF：墨水屏上不要补间动画，否则每一帧都是一次面板刷新 */
    lv_bar_set_value(ctx->bar, pct, LV_ANIM_OFF);
}

static void notify_progress(struct PlayerApp *app, bool force)
{
    ViewPlayCtx *ctx = s_ctx;
    if (!ctx || !app) return;

    uint32_t now_s = audio_service_position_ms() / 1000;
    if (!force && (now_s / PROGRESS_PUSH_S) == (ctx->last_push_s / PROGRESS_PUSH_S)) {
        return;   /* 节流：本周期已经推过了 */
    }
    ctx->last_push_s = now_s;

    render_progress(app);

    /* 被 toast / 弹窗打断过就先重建基准帧，否则只推进度区这一块 */
    if (!epd_region_is_active()) epd_region_begin(lv_screen_active());
    else                         epd_region_flush_obj(ctx->progress_zone);
}

/* ── 局部刷新：播放/暂停图标 ──────────────────────────────────────── */

static void notify_state(struct PlayerApp *app)
{
    ViewPlayCtx *ctx = s_ctx;
    if (!ctx || !app) return;

    bool playing = (audio_service_state() == AUDIO_STATE_PLAYING);
    lv_label_set_text(ctx->lbl_btn_play, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

    if (!epd_region_is_active()) epd_region_begin(lv_screen_active());
    else                         epd_region_flush_obj(ctx->btn_play);  /* ← 只推图标 */
}

/* ── 局部刷新：播放模式 + 音量 ────────────────────────────────────── */

static void notify_options(struct PlayerApp *app)
{
    ViewPlayCtx *ctx = s_ctx;
    if (!ctx || !app || !ctx->opts_zone) return;

    lv_label_set_text(ctx->lbl_mode, audio_mode_text(audio_service_mode()));
    lv_label_set_text_fmt(ctx->lbl_volume, "%u", audio_service_volume());

    if (!epd_region_is_active()) epd_region_begin(lv_screen_active());
    else                         epd_region_flush_obj(ctx->opts_zone);
}

/* ── 换曲：内容整体变了，走一次整屏 ───────────────────────────────── */

static void notify_track(struct PlayerApp *app)
{
    ViewPlayCtx *ctx = s_ctx;
    if (!ctx || !app) return;

    const audio_track_t *t = audio_service_current();
    uint16_t qn = audio_service_queue_count();
    int16_t  qp = audio_service_queue_pos();

    lv_label_set_text(ctx->lbl_title, t ? t->title : "No track");
    if (qn > 0 && qp >= 0) lv_label_set_text_fmt(ctx->lbl_index, "%d / %u", qp + 1, qn);
    else                   lv_label_set_text(ctx->lbl_index, "—");

    bool playing = (audio_service_state() == AUDIO_STATE_PLAYING);
    lv_label_set_text(ctx->lbl_btn_play, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    render_progress(app);
    ctx->last_push_s = audio_service_position_ms() / 1000;

    /* 标题长度变了、进度归零，整页都不一样了 —— 这时候整屏刷一次更干净，
     * 然后重新建立局部刷新的差分基准。 */
    epd_region_begin(lv_screen_active());
}

/* ── 1Hz tick ─────────────────────────────────────────────────────── */

static void tick_cb(lv_timer_t *t)
{
    PlayerApp *app = lv_timer_get_user_data(t);
    if (!app) return;
    if (audio_service_state() != AUDIO_STATE_PLAYING) return;
    notify_progress(app, false);
}

/* audio_service 的事件回调 —— 播放状态不再由本 app 持有，
 * 所以后台切歌 / 播完自动下一首也能正确刷新界面。 */
static void on_audio_event(audio_event_t ev, audio_err_t err, void *user_data)
{
    PlayerApp *app = user_data;
    switch (ev) {
    case AUDIO_EV_TRACK_CHANGED: notify_track(app); break;
    case AUDIO_EV_STATE_CHANGED:  notify_state(app);   break;
    case AUDIO_EV_OPTION_CHANGED: notify_options(app); break;
    case AUDIO_EV_ERROR:
        /* 局部刷新窗口开着时 toast 画在窗口外会看不见，先退回整屏 */
        if (epd_region_is_active()) epd_region_end();
        lv_toast_show(audio_err_text(err), 2500);
        break;
    default: break;
    }
}

/* ── 交互 ─────────────────────────────────────────────────────────── */

static void toggle_cb(lv_event_t *e)
{
    (void)e;
    audio_service_toggle();
}

static void prev_cb(lv_event_t *e) { (void)e; audio_service_prev(); }
static void next_cb(lv_event_t *e) { (void)e; audio_service_next(); }

static void seek_back_cb(lv_event_t *e)
{
    (void)e;
    audio_service_seek_by(-SEEK_STEP_MS);
    notify_progress(&g_player_app, true);
}

static void seek_fwd_cb(lv_event_t *e)
{
    (void)e;
    audio_service_seek_by(+SEEK_STEP_MS);
    notify_progress(&g_player_app, true);
}

static void mode_cb(lv_event_t *e)
{
    (void)e;
    audio_service_cycle_mode();
}

static void vol_down_cb(lv_event_t *e) { (void)e; audio_service_volume_down(); }
static void vol_up_cb(lv_event_t *e)   { (void)e; audio_service_volume_up(); }

/* ── 播放队列（底部弹窗，不新开页面）────────────────────────────────
 * 弹窗是覆盖整屏的，局部刷新窗口会把它挡在外面 —— 打开前先退回整屏。
 * 关掉之后下一次 notify_* 会自己把局部刷新重新建立起来。 */

static void queue_close_cb(lv_event_t *e)
{
    lv_bottom_sheet_close(lv_event_get_user_data(e));
}

static void queue_jump_cb(lv_event_t *e)
{
    audio_service_play_index((uint16_t)(uintptr_t)lv_event_get_user_data(e));
}

static void queue_remove_cb(lv_event_t *e)
{
    audio_service_remove_at((uint16_t)(uintptr_t)lv_event_get_user_data(e));
}

static void queue_clear_cb(lv_event_t *e)
{
    (void)e;
    audio_service_clear_queue();
}

static void show_queue_sheet(lv_event_t *e)
{
    (void)e;
    uint16_t n = audio_service_queue_count();
    if (n == 0) { lv_toast_show("The queue is empty", 1500); return; }

    if (epd_region_is_active()) epd_region_end();

    lv_bottom_sheet_t *sheet = lv_bottom_sheet_create(NULL);
    lv_obj_t *content = lv_bottom_sheet_get_content(sheet);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 6, 0);

    int16_t cur = audio_service_queue_pos();

    for (uint16_t i = 0; i < n; i++) {
        const audio_track_t *t = audio_service_queue_at(i);
        if (!t) continue;

        lv_obj_t *row = lv_obj_create(content);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_set_style_radius(row, 2, 0);
        lv_obj_set_style_border_width(row, (i == (uint16_t)cur) ? 2 : 1, 0);
        lv_obj_set_style_border_color(row, lv_color_black(), 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text_fmt(lbl, "%s%s",
                              (i == (uint16_t)cur) ? LV_SYMBOL_PLAY " " : "", t->title);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_flex_grow(lbl, 1);
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(lbl, queue_jump_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(lbl, queue_close_cb, LV_EVENT_CLICKED, sheet);

        lv_obj_t *del = lv_btn_create(row);
        ui_style_set_btn_secondary(del);
        lv_obj_set_size(del, 52, 44);
        lv_obj_set_style_radius(del, 2, 0);
        lv_obj_center(lv_label_create(del));
        lv_label_set_text(lv_obj_get_child(del, 0), LV_SYMBOL_MINUS);
        lv_obj_add_event_cb(del, queue_remove_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(del, queue_close_cb, LV_EVENT_CLICKED, sheet);
    }

    lv_obj_t *clear = lv_btn_create(content);
    ui_style_set_btn_secondary(clear);
    lv_obj_set_width(clear, LV_PCT(100));
    lv_obj_set_height(clear, 50);
    lv_obj_set_style_radius(clear, 2, 0);
    lv_obj_t *cl = lv_label_create(clear);
    lv_label_set_text(cl, LV_SYMBOL_TRASH "  Clear Queue");
    lv_obj_center(cl);
    lv_obj_add_event_cb(clear, queue_clear_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(clear, queue_close_cb, LV_EVENT_CLICKED, sheet);
}

/* 点进度条任意位置跳转过去 */
static void bar_click_cb(lv_event_t *e)
{
    PlayerApp *app = lv_event_get_user_data(e);
    ViewPlayCtx *ctx = s_ctx;
    uint32_t dur = audio_service_duration_ms();
    if (!ctx || dur == 0) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_area_t a;
    lv_obj_get_coords(ctx->bar, &a);
    int32_t w = lv_area_get_width(&a);
    if (w <= 0) return;

    int32_t x = p.x - a.x1;
    if (x < 0) x = 0;
    if (x > w) x = w;

    uint32_t pos = (uint32_t)((uint64_t)dur * (uint32_t)x / (uint32_t)w);
    audio_service_seek_to(pos);
    notify_progress(app, true);   /* 快进立刻反馈，只刷进度条那一块 */
}

static void page_delete_cb(lv_event_t *e)
{
    ViewPlayCtx *ctx = lv_event_get_user_data(e);
    if (!ctx) return;

    if (ctx->tick) lv_timer_delete(ctx->tick);
    if (s_ctx == ctx) s_ctx = NULL;

    /* 必须取消订阅：服务还活着，但本页面马上就被释放了 */
    audio_service_unsubscribe(on_audio_event);

    /* 退出局部刷新模式，把整屏刷新交还给其他页面 */
    epd_region_end();
    ESP_LOGI(TAG, "play page destroyed, full-refresh restored");
    /* ctx 本身由 page_navigator 在 nav_ctx 释放时回收 */
}

/* ── 页面构建 ─────────────────────────────────────────────────────── */

static lv_obj_t *make_ctrl_btn(lv_obj_t *parent, const char *sym,
                               lv_event_cb_t cb, void *ud, int w)
{
    lv_obj_t *btn = lv_btn_create(parent);
    ui_style_set_btn_secondary(btn);
    lv_obj_set_size(btn, w, 56);
    lv_obj_set_style_radius(btn, 2, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, sym);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
    return btn;
}

static lv_obj_t* build_player_play_page(PlayerApp* app, void* user_data)
{
    (void)user_data;
    Page page = lv_page_create("Now Playing", true,
                               page_navigator_navigate_back, &app->view->page_nav);

    ViewPlayCtx *ctx = malloc(sizeof(ViewPlayCtx));
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to allocate play page context");
        return NULL;
    }
    memset(ctx, 0, sizeof(ViewPlayCtx));
    app->view->page_nav.nav_ctx = ctx;
    s_ctx = ctx;

    lv_obj_clear_flag(page.container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(page.container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page.container, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(page.container, 18, 0);

    const audio_track_t *t = audio_service_current();

    /* 标题 */
    ctx->lbl_title = lv_label_create(page.container);
    lv_label_set_text(ctx->lbl_title, t ? t->title : "No track");
    lv_label_set_long_mode(ctx->lbl_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ctx->lbl_title, LV_PCT(100));
    lv_obj_set_style_text_align(ctx->lbl_title, LV_TEXT_ALIGN_CENTER, 0);

    ctx->lbl_index = lv_label_create(page.container);
    lv_obj_set_style_text_font(ctx->lbl_index, LV_FONT_SMALL, 0);
    /* 点"3 / 8"打开播放队列 —— 不新开页面，用底部弹窗 */
    lv_obj_add_flag(ctx->lbl_index, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(ctx->lbl_index, 16);
    lv_obj_add_event_cb(ctx->lbl_index, show_queue_sheet, LV_EVENT_CLICKED, app);
    uint16_t qn = audio_service_queue_count();
    int16_t  qp = audio_service_queue_pos();
    if (qn > 0 && qp >= 0) lv_label_set_text_fmt(ctx->lbl_index, "%d / %u", qp + 1, qn);
    else                   lv_label_set_text(ctx->lbl_index, "—");

    /* ── 局部刷新窗口 1：进度区 ──────────────────────────────────
     * 时间和进度条放在同一个容器里，这样只需要一个刷新窗口，
     * 不用在三个对象之间来回切窗口。 */
    ctx->progress_zone = lv_obj_create(page.container);
    lv_obj_set_width(ctx->progress_zone, LV_PCT(100));
    lv_obj_set_height(ctx->progress_zone, LV_SIZE_CONTENT);
    lv_obj_clear_flag(ctx->progress_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(ctx->progress_zone, 0, 0);
    lv_obj_set_style_bg_opa(ctx->progress_zone, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ctx->progress_zone, lv_color_white(), 0);
    lv_obj_set_style_pad_all(ctx->progress_zone, 6, 0);
    lv_obj_set_style_pad_row(ctx->progress_zone, 8, 0);
    lv_obj_set_flex_flow(ctx->progress_zone, LV_FLEX_FLOW_COLUMN);

    ctx->bar = lv_bar_create(ctx->progress_zone);
    lv_obj_set_width(ctx->bar, LV_PCT(100));
    lv_obj_set_height(ctx->bar, 16);
    lv_bar_set_range(ctx->bar, 0, 1000);
    lv_obj_set_style_radius(ctx->bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ctx->bar, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(ctx->bar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(ctx->bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->bar, lv_color_black(), LV_PART_INDICATOR);
    /* 进度条可点，点哪跳哪 */
    lv_obj_add_flag(ctx->bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(ctx->bar, 12);
    lv_obj_add_event_cb(ctx->bar, bar_click_cb, LV_EVENT_CLICKED, app);

    lv_obj_t *time_row = lv_obj_create(ctx->progress_zone);
    lv_obj_remove_style_all(time_row);
    lv_obj_set_width(time_row, LV_PCT(100));
    lv_obj_set_height(time_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ctx->lbl_elapsed = lv_label_create(time_row);
    lv_obj_set_style_text_font(ctx->lbl_elapsed, LV_FONT_SMALL, 0);
    ctx->lbl_total = lv_label_create(time_row);
    lv_obj_set_style_text_font(ctx->lbl_total, LV_FONT_SMALL, 0);

    /* ── 快进 / 快退 ───────────────────────────────────────────── */
    lv_obj_t *seek_row = lv_obj_create(page.container);
    lv_obj_remove_style_all(seek_row);
    lv_obj_set_width(seek_row, LV_PCT(100));
    lv_obj_set_height(seek_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(seek_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(seek_row, LV_FLEX_ALIGN_SPACE_AROUND,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_ctrl_btn(seek_row, LV_SYMBOL_LEFT " 15s",  seek_back_cb, app, 150);
    make_ctrl_btn(seek_row, "15s " LV_SYMBOL_RIGHT, seek_fwd_cb,  app, 150);

    /* ── 上一首 / 播放暂停 / 下一首 ─────────────────────────────── */
    lv_obj_t *ctrl_row = lv_obj_create(page.container);
    lv_obj_remove_style_all(ctrl_row);
    lv_obj_set_width(ctrl_row, LV_PCT(100));
    lv_obj_set_height(ctrl_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_SPACE_AROUND,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_ctrl_btn(ctrl_row, LV_SYMBOL_PREV, prev_cb, app, 90);

    /* 局部刷新窗口 2 */
    ctx->btn_play = lv_btn_create(ctrl_row);
    ui_style_set_btn_primary(ctx->btn_play);
    lv_obj_set_size(ctx->btn_play, 110, 68);
    lv_obj_set_style_radius(ctx->btn_play, 2, 0);
    ctx->lbl_btn_play = lv_label_create(ctx->btn_play);
    lv_label_set_text(ctx->lbl_btn_play,
                      audio_service_state() == AUDIO_STATE_PLAYING
                          ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_obj_center(ctx->lbl_btn_play);
    lv_obj_add_event_cb(ctx->btn_play, toggle_cb, LV_EVENT_CLICKED, app);

    make_ctrl_btn(ctrl_row, LV_SYMBOL_NEXT, next_cb, app, 90);

    /* ── 局部刷新窗口 3：播放模式 + 音量 ─────────────────────────
     * 两者放同一个容器，共用一个刷新窗口，不用在两个对象间来回切。 */
    ctx->opts_zone = lv_obj_create(page.container);
    lv_obj_set_width(ctx->opts_zone, LV_PCT(100));
    lv_obj_set_height(ctx->opts_zone, LV_SIZE_CONTENT);
    lv_obj_clear_flag(ctx->opts_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(ctx->opts_zone, 0, 0);
    lv_obj_set_style_bg_opa(ctx->opts_zone, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ctx->opts_zone, lv_color_white(), 0);
    lv_obj_set_style_pad_all(ctx->opts_zone, 6, 0);
    lv_obj_set_style_pad_column(ctx->opts_zone, 8, 0);
    lv_obj_set_flex_flow(ctx->opts_zone, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctx->opts_zone, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *btn_mode = lv_btn_create(ctx->opts_zone);
    ui_style_set_btn_secondary(btn_mode);
    lv_obj_set_size(btn_mode, 150, 50);
    lv_obj_set_style_radius(btn_mode, 2, 0);
    ctx->lbl_mode = lv_label_create(btn_mode);
    lv_label_set_text(ctx->lbl_mode, audio_mode_text(audio_service_mode()));
    lv_obj_center(ctx->lbl_mode);
    lv_obj_add_event_cb(btn_mode, mode_cb, LV_EVENT_CLICKED, app);

    /* 音量走离散档位（每档 AUDIO_VOLUME_STEP），不做滑块：
     * 拖动过程中每个中间值都是一次墨水屏刷新。 */
    make_ctrl_btn(ctx->opts_zone, LV_SYMBOL_VOLUME_MID, vol_down_cb, app, 70);
    ctx->lbl_volume = lv_label_create(ctx->opts_zone);
    lv_label_set_text_fmt(ctx->lbl_volume, "%u", audio_service_volume());
    make_ctrl_btn(ctx->opts_zone, LV_SYMBOL_VOLUME_MAX, vol_up_cb, app, 70);

    /* 扬声器不可用时把控制区禁掉并说明原因 */
    if (!audio_service_is_available()) {
        lv_obj_add_state(ctx->btn_play, LV_STATE_DISABLED);
        lv_obj_t *warn = lv_label_create(page.container);
        lv_label_set_text(warn, LV_SYMBOL_WARNING " Speaker unavailable");
        lv_obj_set_style_text_font(warn, LV_FONT_SMALL, 0);
    }

    render_progress(app);
    ctx->last_push_s = audio_service_position_ms() / 1000;

    audio_service_subscribe(on_audio_event, app);
    ctx->tick = lv_timer_create(tick_cb, PROGRESS_TICK_MS, app);
    lv_obj_add_event_cb(page.screen, page_delete_cb, LV_EVENT_DELETE, ctx);

    /* 静态内容先整屏打一帧并同步差分基准，之后所有更新都只推小窗口 */
    epd_region_begin(page.screen);

    return page.screen;
}

void player_view_play_init_registry(struct PlayerApp* app) {
    PAGE_REGISTE(app, PAGE_PLAYER_PLAY, build_player_play_page);
}
