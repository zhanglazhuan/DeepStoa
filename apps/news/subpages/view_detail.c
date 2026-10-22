/*
 * view_detail.c — 新闻详情页（标题 + 正文）
 *
 *  ┌───────────────────────────────┐
 *  │ [<]     The Verge             │  header 标题放来源，返回会 abort 在途请求
 *  ├───────────────────────────────┤
 *  │ TITLE                  (32px) │  标题和 meta 建好就不再变
 *  │ source  -  date        (18px) │
 *  │ ───────────────────────────── │
 *  │ body / Loading... / 错误+重试  │  ← 正文按可见高度分页，左右滑动
 *  │             1/3               │  页码固定在底部
 *  └───────────────────────────────┘
 *
 * 标题和概要在列表里已经有了，所以进页面立刻能画出来；只有正文要等网络。
 */

#include <stdio.h>
#include <stdlib.h>
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

/* ── 按实际文字宽高切页，边界保持在 UTF-8 字符之间 ───────────── */

static size_t next_char(const char *text, size_t offset, size_t len)
{
    if (offset >= len) return len;
    ++offset;
    while (offset < len && ((unsigned char)text[offset] & 0xc0) == 0x80) ++offset;
    return offset;
}

static size_t page_end(const char *text, size_t start, size_t len,
                       int32_t width, int32_t height, char *scratch)
{
    size_t lo = start;
    size_t hi = len + 1;
    while (hi - lo > 1) {
        size_t mid = lo + (hi - lo) / 2;
        while (mid > start && mid < len &&
               ((unsigned char)text[mid] & 0xc0) == 0x80) --mid;
        if (mid <= lo) mid = next_char(text, lo, len);
        if (mid >= hi) break;

        memcpy(scratch, text + start, mid - start);
        scratch[mid - start] = '\0';
        lv_point_t size;
        lv_text_get_size(&size, scratch, NEWS_FONT_BODY, 0, 0,
                         width, LV_TEXT_FLAG_NONE);
        if (size.y <= height) lo = mid;
        else hi = mid;
    }

    size_t end = lo > start ? lo : next_char(text, start, len);
    if (end < len && end > start + 32) {
        for (size_t i = end; i > end - 32; --i) {
            if (text[i - 1] == ' ' || text[i - 1] == '\n') {
                end = i;
                break;
            }
        }
    }
    return end;
}

static const char *active_body(NewsModel *model)
{
    const char *body = news_model_active_content(model);
    if (body && body[0]) return body;
    const NewsArticle *art = news_model_active(model);
    return (art && art->summary[0]) ? art->summary : "No content available.";
}

static void show_page(NewsApp *app)
{
    NewsDetailCtx *ctx = &app->view->detail_ctx;
    const char *body = active_body(app->model);
    uint16_t page = ctx->page_index;
    uint16_t start = ctx->page_offsets[page];
    uint16_t end = ctx->page_offsets[page + 1];
    lv_label_set_text_fmt(ctx->body_label, "%.*s", (int)(end - start), body + start);
    lv_label_set_text_fmt(ctx->page_label, "%u/%u",
                          (unsigned)(page + 1), (unsigned)ctx->page_count);
}

static void paginate_body(NewsApp *app, const char *body)
{
    NewsDetailCtx *ctx = &app->view->detail_ctx;
    size_t len = strlen(body);
    ctx->page_index = 0;
    ctx->page_count = 0;
    ctx->page_offsets[0] = 0;

    lv_obj_update_layout(lv_obj_get_screen(ctx->body_slot));
    int32_t width = lv_obj_get_content_width(ctx->body_slot);
    int32_t height = lv_obj_get_content_height(ctx->body_slot);
    if (width < 1) width = 400;
    if (height < lv_font_get_line_height(NEWS_FONT_BODY)) height = 500;

    char *scratch = malloc(len + 1);
    if (!scratch) {
        ctx->page_offsets[1] = (uint16_t)len;
        ctx->page_count = 1;
        return;
    }

    size_t start = 0;
    while (start < len && ctx->page_count < NEWS_DETAIL_MAX_PAGES) {
        size_t end = page_end(body, start, len, width, height, scratch);
        ctx->page_offsets[++ctx->page_count] = (uint16_t)end;
        start = end;
    }
    free(scratch);
    if (ctx->page_count == 0) {
        ctx->page_offsets[1] = 0;
        ctx->page_count = 1;
    }
}

static void detail_gesture_cb(lv_event_t *event)
{
    NewsApp *app = lv_event_get_user_data(event);
    NewsDetailCtx *ctx = &app->view->detail_ctx;
    lv_indev_t *indev = lv_indev_get_act();
    if (!ctx->body_label || !indev) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT && ctx->page_index + 1 < ctx->page_count) {
        ++ctx->page_index;
    } else if (dir == LV_DIR_RIGHT && ctx->page_index > 0) {
        --ctx->page_index;
    } else return;
    show_page(app);
    lv_indev_wait_release(indev);
}

/* ── 正文区：加载中 / 错误 + 重试 / 正文分页 ───────────────────── */

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
    ctx->body_label = NULL;
    ctx->page_count = 0;
    lv_label_set_text(ctx->page_label, "-");

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

    body = active_body(m);
    ctx->body_label = make_text(ctx->body_slot, "", NEWS_FONT_BODY);
    lv_obj_set_style_text_letter_space(ctx->body_label, 0, 0);
    lv_obj_set_style_text_line_space(ctx->body_label, 0, 0);
    paginate_body(app, body);
    show_page(app);
}

/* ── page builder ────────────────────────────────────────────── */

static lv_obj_t *build_detail_page(NewsApp *app, void *user_data)
{
    (void)user_data;

    const NewsArticle *art = news_model_active(app->model);
    const char *hdr = (art && art->source[0]) ? art->source : "Article";

    /* 返回走自己的回调：先取消在途的正文请求，再 pop */
    Page page = lv_page_create(hdr, true, news_controller_on_detail_back, app);

    lv_obj_clear_flag(page.container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(page.container, 10, 0);
    lv_obj_add_event_cb(page.container, detail_gesture_cb, LV_EVENT_GESTURE, app);

    /* 标题 */
    make_text(page.container, (art && art->title[0]) ? art->title : "(untitled)",
              NEWS_FONT_TITLE);

    /* 来源 · 日期 */
    if (art && (art->source[0] || art->date[0])) {
        char meta[NEWS_SOURCE_LEN + NEWS_DATE_LEN + 8];
        if (art->source[0] && art->date[0]) {
            snprintf(meta, sizeof(meta), "%s  -  %s", art->source, art->date);
        } else {
            snprintf(meta, sizeof(meta), "%s%s", art->source, art->date);
        }
        make_text(page.container, meta, NEWS_FONT_META);
    }

    make_hairline(page.container);

    /* 正文占满标题与页码之间的空间，内容固定在当前页。 */
    lv_obj_t *slot = lv_obj_create(page.container);
    lv_obj_remove_style_all(slot);
    lv_obj_set_width(slot, LV_PCT(100));
    lv_obj_set_flex_grow(slot, 1);
    lv_obj_set_flex_flow(slot, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(slot, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(slot, 12, 0);
    lv_obj_clear_flag(slot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *page_label = lv_label_create(page.container);
    lv_obj_set_width(page_label, LV_PCT(100));
    lv_obj_set_height(page_label, 30);
    lv_obj_set_style_text_font(page_label, NEWS_FONT_META, 0);
    lv_obj_set_style_text_align(page_label, LV_TEXT_ALIGN_CENTER, 0);

    memset(&app->view->detail_ctx, 0, sizeof(app->view->detail_ctx));
    app->view->detail_ctx.body_slot = slot;
    app->view->detail_ctx.page_label = page_label;
    /* 列表页的控件此刻已作废，清掉指针免得异步回调摸到 */
    memset(&app->view->list_ctx, 0, sizeof(app->view->list_ctx));

    news_view_detail_render(app);
    return page.screen;
}

void news_view_detail_init_registry(NewsApp *app)
{
    PAGE_REGISTE(app, PAGE_NEWS_DETAIL, build_detail_page);
}
