/*
 * view_detail.c — 新闻详情页（标题 + 正文）
 *
 *  ┌───────────────────────────────┐
 *  │ [<]     The Verge             │  header 标题放来源，返回会 abort 在途请求
 *  ├───────────────────────────────┤
 *  │ TITLE                  (32px) │  标题和 meta 建好就不再变
 *  │ source  -  date        (18px) │
 *  │ ───────────────────────────── │
 *  │ body / Loading... / 错误+重试  │  ← body_slot，随状态重建
 *  │ ───────────────────────────── │
 *  │        -  End  -              │  正文到底的收尾标记
 *  └───────────────────────────────┘
 *
 * 标题和概要在列表里已经有了，所以进页面立刻能画出来；只有正文要等网络。
 */

#include <stdio.h>
#include <string.h>
#include <lvgl.h>
#include "esp_log.h"
#include "lv_page.h"
#include "lv_ui_style_guide.h"

#include "view_detail.h"
#include "controller_detail.h"
#include "../view.h"
#include "../model.h"
#include "../controller.h"

static const char *TAG = "news_view_detail";

/* ── helpers ─────────────────────────────────────────────────── */

static lv_obj_t *make_text(lv_obj_t *parent, const char *text, const lv_font_t *font)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    lv_label_set_text(lbl, text);
    return lbl;
}

static void make_hairline(lv_obj_t *parent)
{
    lv_obj_t *hr = lv_obj_create(parent);
    lv_obj_remove_style_all(hr);
    lv_obj_set_size(hr, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(hr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(hr, LV_OPA_COVER, 0);
}

/* ── 正文区：加载中 / 错误 + 重试 / 正文 + 收尾 ───────────────── */

void news_view_detail_render(NewsApp *app)
{
    NewsDetailCtx *ctx = &app->view->detail_ctx;
    if (!ctx->body_slot) {
        ESP_LOGW(TAG, "detail body slot not built yet");
        return;
    }

    NewsModel  *m    = app->model;
    const char *body = news_model_active_content(m);

    lv_obj_clean(ctx->body_slot);

    if (m->detail_state == NEWS_LOAD_BUSY && !body) {
        lv_obj_t *l = make_text(ctx->body_slot, LV_SYMBOL_REFRESH " Loading article...",
                                NEWS_FONT_BODY);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    if (m->detail_state == NEWS_LOAD_ERROR && !body) {
        lv_obj_t *h = make_text(ctx->body_slot,
                                LV_SYMBOL_WARNING " Could not load this article",
                                NEWS_FONT_BODY);
        lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t *d = make_text(ctx->body_slot,
                                m->detail_err[0] ? m->detail_err : "Please try again.",
                                NEWS_FONT_META);
        lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t *btn = lv_button_create(ctx->body_slot);
        ui_style_set_btn_secondary(btn);
        lv_obj_set_size(btn, LV_SIZE_CONTENT, UI_BTN_H_MEDIUM);
        lv_obj_set_style_pad_hor(btn, 20, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, news_controller_on_detail_retry, LV_EVENT_CLICKED, app);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, NEWS_FONT_BODY, 0);
        lv_label_set_text(lbl, LV_SYMBOL_REFRESH " Retry");
        lv_obj_center(lbl);
        return;
    }

    if (!body) {
        /* 拉回来是空正文：退回列表里的概要，总比空白页强 */
        const NewsArticle *art = news_model_active(m);
        body = (art && art->summary[0]) ? art->summary : "No content available.";
    }

    make_text(ctx->body_slot, body, NEWS_FONT_BODY);

    make_hairline(ctx->body_slot);
    lv_obj_t *end = make_text(ctx->body_slot, "-  End  -", NEWS_FONT_META);
    lv_obj_set_style_text_align(end, LV_TEXT_ALIGN_CENTER, 0);
}

/* ── page builder ────────────────────────────────────────────── */

static lv_obj_t *build_detail_page(NewsApp *app, void *user_data)
{
    (void)user_data;

    const NewsArticle *art = news_model_active(app->model);
    const char *hdr = (art && art->source[0]) ? art->source : "Article";

    /* 返回走自己的回调：先取消在途的正文请求，再 pop */
    Page page = lv_page_create(hdr, true, news_controller_on_detail_back, app);

    lv_obj_t *scroll = lv_obj_create(page.container);
    lv_obj_remove_style_all(scroll);
    lv_obj_set_width(scroll, LV_PCT(100));
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_set_style_pad_ver(scroll, 8, 0);
    lv_obj_set_style_pad_row(scroll, 10, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_OFF);

    /* 标题 */
    make_text(scroll, (art && art->title[0]) ? art->title : "(untitled)",
              NEWS_FONT_TITLE);

    /* 来源 · 日期 */
    if (art && (art->source[0] || art->date[0])) {
        char meta[NEWS_SOURCE_LEN + NEWS_DATE_LEN + 8];
        if (art->source[0] && art->date[0]) {
            snprintf(meta, sizeof(meta), "%s  -  %s", art->source, art->date);
        } else {
            snprintf(meta, sizeof(meta), "%s%s", art->source, art->date);
        }
        make_text(scroll, meta, NEWS_FONT_META);
    }

    make_hairline(scroll);

    /* 正文区：唯一会随状态重建的部分 */
    lv_obj_t *slot = lv_obj_create(scroll);
    lv_obj_remove_style_all(slot);
    lv_obj_set_width(slot, LV_PCT(100));
    lv_obj_set_height(slot, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(slot, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(slot, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(slot, 12, 0);

    app->view->detail_ctx.scroll    = scroll;
    app->view->detail_ctx.body_slot = slot;
    /* 列表页的控件此刻已作废，清掉指针免得异步回调摸到 */
    memset(&app->view->list_ctx, 0, sizeof(app->view->list_ctx));

    news_view_detail_render(app);
    return page.screen;
}

void news_view_detail_init_registry(NewsApp *app)
{
    PAGE_REGISTE(app, PAGE_NEWS_DETAIL, build_detail_page);
}
