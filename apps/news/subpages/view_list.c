/*
 * view_list.c — 新闻列表页（卡片流）
 *
 * 屏幕 480x800，可用内容区约 448x710。整页布局：
 *
 *  ┌───────────────────────────────┐
 *  │ [<]      News            [↻]  │  header（右上角刷新，长按注入故障）
 *  ├───────────────────────────────┤
 *  │ Refreshing...                 │  仅"已有内容 + 正在刷新"时出现
 *  │ ┌───────────────────────────┐ │
 *  │ │ TITLE          (32px 换行) │ │
 *  │ │ source  ·  date  (18px)   │ │  ← 一张卡片 ≈ 220px，一屏约 3 张
 *  │ │ summary        (24px 3 行) │ │
 *  │ └───────────────────────────┘ │
 *  │            ...                │
 *  │ ─────────────────────────────  │
 *  │        [ Load more ]          │  页脚：加载更多 / 加载中 / 重试 /
 *  │      - No more news -         │        没有更多
 *  └───────────────────────────────┘
 *
 * 空列表时整页换成占位块：加载中 / 网络异常+重试 / 暂无内容。
 *
 * 渲染策略：状态一变就 lv_obj_clean() 整块重建。墨水屏本来就是整屏刷，
 * 增量更新省不下什么，反而容易和 model 状态对不上；只把滚动位置存下来还原。
 */

#include <stdio.h>
#include <string.h>
#include <lvgl.h>
#include "esp_log.h"
#include "lv_page.h"
#include "lv_ui_style_guide.h"

#include "view_list.h"
#include "controller_list.h"
#include "../view.h"
#include "../model.h"
#include "../controller.h"

static const char *TAG = "news_view_list";

#define CARD_PAD          12
#define CARD_GAP          14
#define SUMMARY_MAX_CHARS 100   /* 24px 字体下约 3 行 */

/* ── styles ──────────────────────────────────────────────────── */

static lv_style_t s_card;
static lv_style_t s_card_pressed;
static bool s_styles_init = false;

static void init_styles(void)
{
    if (s_styles_init) return;
    s_styles_init = true;

    lv_style_init(&s_card);
    lv_style_set_radius(&s_card, 4);
    lv_style_set_border_width(&s_card, 1);
    lv_style_set_border_color(&s_card, lv_color_black());
    lv_style_set_bg_color(&s_card, lv_color_white());
    lv_style_set_bg_opa(&s_card, LV_OPA_COVER);
    lv_style_set_text_color(&s_card, lv_color_black());
    lv_style_set_shadow_width(&s_card, 0);
    lv_style_set_pad_all(&s_card, CARD_PAD);
    lv_style_set_pad_row(&s_card, 6);

    /* 墨水屏上按下反色要整屏重刷一次，得不偿失 —— 按下时只加粗边框 */
    lv_style_init(&s_card_pressed);
    lv_style_set_bg_color(&s_card_pressed, lv_color_white());
    lv_style_set_border_width(&s_card_pressed, 3);
}

/* ── helpers ─────────────────────────────────────────────────── */

/** 按字节裁剪并补省略号；不在 UTF-8 续字节中间切断。 */
static void copy_clamped(char *dst, size_t dst_len, const char *src, size_t max_chars)
{
    size_t len = strlen(src);
    if (max_chars > dst_len - 4) max_chars = dst_len - 4;

    if (len <= max_chars) {
        snprintf(dst, dst_len, "%s", src);
        return;
    }
    size_t n = max_chars;
    while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    while (n > 0 && src[n - 1] == ' ') n--;
    memcpy(dst, src, n);
    dst[n] = '\0';
    strncat(dst, "...", dst_len - n - 1);
}

static void format_meta(char *dst, size_t dst_len, const NewsArticle *art)
{
    if (art->source[0] && art->date[0]) {
        snprintf(dst, dst_len, "%s  -  %s", art->source, art->date);
    } else {
        snprintf(dst, dst_len, "%s%s", art->source, art->date);
    }
}

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

static lv_obj_t *make_hairline(lv_obj_t *parent)
{
    lv_obj_t *hr = lv_obj_create(parent);
    lv_obj_remove_style_all(hr);
    lv_obj_set_size(hr, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(hr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(hr, LV_OPA_COVER, 0);
    return hr;
}

static lv_obj_t *make_action_btn(lv_obj_t *parent, const char *text,
                                 lv_event_cb_t cb, NewsApp *app)
{
    lv_obj_t *btn = lv_button_create(parent);
    ui_style_set_btn_secondary(btn);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, UI_BTN_H_MEDIUM);
    lv_obj_set_style_pad_hor(btn, 20, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, app);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, NEWS_FONT_BODY, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

/* ── 卡片 ────────────────────────────────────────────────────── */

static void build_card(lv_obj_t *parent, NewsApp *app, uint8_t idx)
{
    const NewsArticle *art = &app->model->items[idx];

    lv_obj_t *card = lv_button_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &s_card, LV_PART_MAIN);
    lv_obj_add_style(card, &s_card_pressed, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    lv_obj_set_user_data(card, (void *)(uintptr_t)idx);
    lv_obj_add_event_cb(card, news_controller_on_article_clicked, LV_EVENT_CLICKED, app);

    /* 标题 */
    make_text(card, art->title[0] ? art->title : "(untitled)", NEWS_FONT_TITLE);

    /* 来源 · 日期 */
    if (art->source[0] || art->date[0]) {
        char meta[NEWS_SOURCE_LEN + NEWS_DATE_LEN + 8];
        format_meta(meta, sizeof(meta), art);
        make_text(card, meta, NEWS_FONT_META);
    }

    /* 概要 */
    if (art->summary[0]) {
        char sum[NEWS_SUMMARY_LEN];
        copy_clamped(sum, sizeof(sum), art->summary, SUMMARY_MAX_CHARS);
        make_text(card, sum, NEWS_FONT_BODY);
    }
}

/* ── 页脚：加载更多 / 加载中 / 重试 / 没有更多 ────────────────── */

static void build_footer(lv_obj_t *parent, NewsApp *app)
{
    const NewsModel *m = app->model;

    lv_obj_t *foot = lv_obj_create(parent);
    lv_obj_remove_style_all(foot);
    lv_obj_set_width(foot, LV_PCT(100));
    lv_obj_set_height(foot, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(foot, 10, 0);
    lv_obj_set_style_pad_row(foot, 8, 0);

    make_hairline(foot);

    if (m->more_state == NEWS_LOAD_BUSY) {
        lv_obj_t *l = make_text(foot, LV_SYMBOL_REFRESH " Loading more...", NEWS_FONT_META);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    if (m->more_state == NEWS_LOAD_ERROR) {
        lv_obj_t *l = make_text(foot, m->more_err[0] ? m->more_err : "Load failed.",
                                NEWS_FONT_META);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        make_action_btn(foot, LV_SYMBOL_REFRESH " Retry",
                        news_controller_on_load_more_clicked, app);
        return;
    }

    if (m->has_more) {
        make_action_btn(foot, "Load more", news_controller_on_load_more_clicked, app);
        return;
    }

    lv_obj_t *l = make_text(foot, "-  No more news  -", NEWS_FONT_META);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}

/* ── 空列表时的整页占位 ──────────────────────────────────────── */

static void build_placeholder(lv_obj_t *parent, NewsApp *app,
                              const char *headline, const char *detail,
                              const char *action)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 16, 0);
    lv_obj_set_style_pad_hor(box, 8, 0);

    lv_obj_t *h = make_text(box, headline, NEWS_FONT_BODY);
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);

    if (detail && detail[0]) {
        lv_obj_t *d = make_text(box, detail, NEWS_FONT_META);
        lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (action) {
        make_action_btn(box, action, news_controller_on_refresh_clicked, app);
    }
}

/* ── 整块渲染 ────────────────────────────────────────────────── */

void news_view_list_render(NewsApp *app)
{
    NewsListCtx *ctx = &app->view->list_ctx;
    if (!ctx->scroll) {
        ESP_LOGW(TAG, "list scroll container not built yet");
        return;
    }
    init_styles();

    const NewsModel *m = app->model;

    /* 重建会把滚动位置清零，"加载更多"之后读者会被弹回顶部 —— 存下来还原 */
    lv_obj_update_layout(ctx->scroll);
    int32_t keep_y = lv_obj_get_scroll_y(ctx->scroll);

    lv_obj_clean(ctx->scroll);

    if (m->count == 0) {
        switch (m->list_state) {
        case NEWS_LOAD_BUSY:
            build_placeholder(ctx->scroll, app,
                              LV_SYMBOL_REFRESH " Loading news...", NULL, NULL);
            break;
        case NEWS_LOAD_ERROR:
            build_placeholder(ctx->scroll, app,
                              LV_SYMBOL_WARNING " Could not load news",
                              m->list_err[0] ? m->list_err : NULL,
                              LV_SYMBOL_REFRESH " Retry");
            break;
        default:
            build_placeholder(ctx->scroll, app, "No news yet.", NULL,
                              LV_SYMBOL_REFRESH " Refresh");
            break;
        }
        return;
    }

    /* 已有内容时刷新失败不清空列表，页首挂一条提示就够了 */
    if (m->list_state == NEWS_LOAD_BUSY) {
        lv_obj_t *l = make_text(ctx->scroll, LV_SYMBOL_REFRESH " Refreshing...",
                                NEWS_FONT_META);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    } else if (m->list_state == NEWS_LOAD_ERROR) {
        lv_obj_t *l = make_text(ctx->scroll, m->list_err[0] ? m->list_err
                                                            : "Refresh failed.",
                                NEWS_FONT_META);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    }

    for (uint8_t i = 0; i < m->count; i++) {
        build_card(ctx->scroll, app, i);
    }
    build_footer(ctx->scroll, app);

    lv_obj_update_layout(ctx->scroll);
    lv_obj_scroll_to_y(ctx->scroll, keep_y, LV_ANIM_OFF);
}

/* ── page builder ────────────────────────────────────────────── */

static lv_obj_t *build_list_page(NewsApp *app, void *user_data)
{
    (void)user_data;
    init_styles();

    Page page = lv_page_create("News", true, page_navigator_navigate_back,
                               &app->view->page_nav);

    /* 右上角刷新：短按刷新，长按给 mock 数据源注入一次网络故障，
     * 用来在真机上走一遍错误分支（http 实现里长按是空操作）。 */
    lv_obj_t *refresh = lv_label_create(page.header_right);
    lv_obj_set_style_text_font(refresh, NEWS_FONT_TITLE, 0);
    lv_label_set_text(refresh, LV_SYMBOL_REFRESH);
    lv_obj_center(refresh);
    lv_obj_add_flag(refresh, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(refresh, 12);
    lv_obj_add_event_cb(refresh, news_controller_on_refresh_clicked,
                        LV_EVENT_CLICKED, app);
    lv_obj_add_event_cb(refresh, news_controller_on_refresh_long_pressed,
                        LV_EVENT_LONG_PRESSED, app);

    /* 滚动容器：卡片、占位、页脚都挂在它下面，整块重建 */
    lv_obj_t *scroll = lv_obj_create(page.container);
    lv_obj_remove_style_all(scroll);
    lv_obj_set_width(scroll, LV_PCT(100));
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_set_style_pad_ver(scroll, 8, 0);
    lv_obj_set_style_pad_row(scroll, CARD_GAP, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_OFF);

    app->view->list_ctx.scroll = scroll;
    /* 离开详情页时那边的控件已经作废，别留着让异步回调摸到 */
    memset(&app->view->detail_ctx, 0, sizeof(app->view->detail_ctx));

    news_view_list_render(app);
    return page.screen;
}

void news_view_list_init_registry(NewsApp *app)
{
    PAGE_REGISTE(app, PAGE_NEWS_LIST, build_list_page);
}
