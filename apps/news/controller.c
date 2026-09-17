/**
 * @file controller.c — News App Controller
 *
 * 三条互不干扰的加载轨道，各自有独立状态和错误位：
 *   list   首屏 / 刷新 —— 成功后整表替换
 *   more   加载下一页 —— 成功后追加，失败只影响页脚
 *   detail 正文       —— 进详情页时按需拉，命中缓存则不拉
 *
 * 不变量：
 *   1. 每条轨道同时最多一个在途请求；重复触发先 abort 旧的
 *   2. 回调先比对 req_id，过期结果直接丢（刷新期间点了加载更多之类）
 *   3. 数据源的时序细节全在 service 层，这里只处理状态迁移和视图同步
 */

#include <string.h>
#include <stdlib.h>
#include <lvgl.h>
#include "esp_log.h"

#include "app.h"
#include "controller.h"
#include "model.h"
#include "view.h"
#include "news_svc.h"

static const char *TAG = "news_ctrl";

#define SPLASH_MS 2000

/* ── 小工具 ──────────────────────────────────────────────────── */

/* app 在 news_app_stop() 里被 memset，异步回调可能晚于它到达 —— 统一在这里挡掉 */
static bool alive(NewsApp *app)
{
    return app && app->controller && app->model && app->view;
}

const char *news_err_text(news_svc_err_t err)
{
    switch (err) {
    case NEWS_SVC_ERR_OFFLINE:   return "No network. Check Wi-Fi.";
    case NEWS_SVC_ERR_TIMEOUT:   return "Request timed out.";
    case NEWS_SVC_ERR_NETWORK:   return "Cannot reach the news server.";
    case NEWS_SVC_ERR_SERVER:    return "News server error.";
    case NEWS_SVC_ERR_BAD_REPLY: return "Unexpected reply from server.";
    case NEWS_SVC_ERR_BUSY:      return "Busy. Try again in a moment.";
    default:                     return "Something went wrong.";
    }
}

/* ── service 回调 ────────────────────────────────────────────── */

static void on_list_done(uint32_t req_id, const NewsArticle *items, uint16_t count,
                         bool has_more, news_svc_err_t err, void *user_data)
{
    NewsApp *app = user_data;
    if (!alive(app)) return;

    NewsController *c = app->controller;
    if (req_id != c->list_req) {            /* 不变量 2 */
        ESP_LOGI(TAG, "stale list result %lu (want %lu), dropping",
                 (unsigned long)req_id, (unsigned long)c->list_req);
        return;
    }

    NewsModel *m = app->model;
    bool was_more = c->list_req_more;
    c->list_req = 0;

    if (err != NEWS_SVC_OK) {
        ESP_LOGW(TAG, "list page failed: err=%d", (int)err);
        if (was_more) {
            news_model_set_more_state(m, NEWS_LOAD_ERROR, news_err_text(err));
        } else {
            news_model_set_list_state(m, NEWS_LOAD_ERROR, news_err_text(err));
        }
        news_view_sync_list(app);
        return;
    }

    if (!was_more) {
        news_model_reset_list(m);           /* 刷新：整表替换 */
    }

    uint8_t added = news_model_append(m, items, count);
    m->next_page++;
    /* 本地攒满了也算"没有更多"，否则页脚会一直挂着一个点不动的按钮 */
    m->has_more = has_more && !news_model_is_full(m) && added == count;

    news_model_set_list_state(m, NEWS_LOAD_DONE, NULL);
    news_model_set_more_state(m, NEWS_LOAD_IDLE, NULL);

    ESP_LOGI(TAG, "list +%u -> %u items, has_more=%d",
             (unsigned)added, (unsigned)m->count, (int)m->has_more);
    news_view_sync_list(app);
}

static void on_detail_done(uint32_t req_id, const char *content,
                           news_svc_err_t err, void *user_data)
{
    NewsApp *app = user_data;
    if (!alive(app)) return;

    NewsController *c = app->controller;
    if (req_id != c->detail_req) {
        ESP_LOGI(TAG, "stale detail result %lu, dropping", (unsigned long)req_id);
        return;
    }
    c->detail_req = 0;

    NewsModel   *m   = app->model;
    NewsArticle *art = news_model_active(m);

    if (err != NEWS_SVC_OK) {
        news_model_set_detail_state(m, NEWS_LOAD_ERROR, news_err_text(err));
    } else if (!art || !news_model_set_content(m, art->id, content)) {
        news_model_set_detail_state(m, NEWS_LOAD_ERROR, "Out of memory.");
    } else {
        news_model_set_detail_state(m, NEWS_LOAD_DONE, NULL);
    }

    news_view_sync_detail(app);
}

/* ── 请求发起 ────────────────────────────────────────────────── */

static void request_page(NewsApp *app, uint16_t page, bool is_more)
{
    NewsController *c = app->controller;

    if (c->list_req) news_svc_abort(c->list_req);   /* 不变量 1 */

    c->list_req      = ++c->req_seq;
    c->list_req_more = is_more;

    if (is_more) {
        news_model_set_more_state(app->model, NEWS_LOAD_BUSY, NULL);
    } else {
        news_model_set_list_state(app->model, NEWS_LOAD_BUSY, NULL);
    }
    news_view_sync_list(app);

    ESP_LOGI(TAG, "fetch list page=%u (%s) req=%lu",
             (unsigned)page, is_more ? "more" : "refresh", (unsigned long)c->list_req);
    news_svc_fetch_list(c->list_req, page, NEWS_PAGE_SIZE, on_list_done, app);
}

void news_controller_load_first(NewsApp *app)
{
    if (!alive(app)) return;
    request_page(app, 1, false);
}

void news_controller_load_more(NewsApp *app)
{
    if (!alive(app)) return;

    NewsModel *m = app->model;
    if (!m->has_more) return;
    if (app->controller->list_req) return;     /* 刷新或上一次加载更多还没回来 */

    request_page(app, m->next_page, true);
}

void news_controller_open_article(NewsApp *app, uint8_t idx)
{
    if (!alive(app)) return;

    NewsModel *m = app->model;
    if (!news_model_select(m, idx)) {
        ESP_LOGW(TAG, "invalid article index %u", (unsigned)idx);
        return;
    }

    if (news_model_active_content(m)) {
        news_model_set_detail_state(m, NEWS_LOAD_DONE, NULL);   /* 缓存命中 */
    } else {
        news_controller_reload_detail(app);
    }

    PAGE_NAVIGATE_TO(app, PAGE_NEWS_DETAIL, NULL);
}

void news_controller_reload_detail(NewsApp *app)
{
    if (!alive(app)) return;

    NewsController *c   = app->controller;
    NewsArticle    *art = news_model_active(app->model);
    if (!art) return;

    if (c->detail_req) news_svc_abort(c->detail_req);
    c->detail_req = ++c->req_seq;

    news_model_set_detail_state(app->model, NEWS_LOAD_BUSY, NULL);
    news_view_sync_detail(app);

    ESP_LOGI(TAG, "fetch detail id=%s req=%lu", art->id, (unsigned long)c->detail_req);
    news_svc_fetch_detail(c->detail_req, art->id, on_detail_done, app);
}

/* ── 启动动画 ────────────────────────────────────────────────── */

static void splash_timer_cb(lv_timer_t *timer)
{
    NewsApp *app = lv_timer_get_user_data(timer);

    lv_timer_delete(timer);
    if (alive(app)) {
        app->controller->splash_timer = NULL;
        ESP_LOGI(TAG, "splash finished, going to list");
        /* 首屏可能已经在启动动画期间到货了，列表页构建时直接从 model 渲染。
         * 这里刻意不走 PAGE_NAVIGATE_TO：启动动画不该进返回栈，
         * 否则在列表页按返回会退回到那张封面而不是退出应用。 */
        page_navigator_navigate_to(&app->view->page_nav, app, PAGE_NEWS_LIST, NULL);
    }
}

/* ── lifecycle ───────────────────────────────────────────────── */

void news_controller_init(NewsApp *app)
{
    app->controller = malloc(sizeof(NewsController));
    if (!app->controller) {
        ESP_LOGE(TAG, "Failed to alloc NewsController");
        return;
    }
    memset(app->controller, 0, sizeof(NewsController));

    news_svc_init();
}

void news_controller_start(NewsApp *app)
{
    if (!alive(app)) return;

    app->controller->splash_timer = lv_timer_create(splash_timer_cb, SPLASH_MS, app);
    lv_timer_set_repeat_count(app->controller->splash_timer, 1);

    news_controller_load_first(app);
}

void news_controller_deinit(NewsApp *app)
{
    if (!app->controller) return;

    NewsController *c = app->controller;
    if (c->list_req)   news_svc_abort(c->list_req);
    if (c->detail_req) news_svc_abort(c->detail_req);
    if (c->splash_timer) {
        lv_timer_delete(c->splash_timer);
        c->splash_timer = NULL;
    }

    news_svc_deinit();

    free(c);
    app->controller = NULL;
}
