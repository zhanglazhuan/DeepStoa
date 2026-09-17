#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"

#include "model.h"
#include "app.h"

static const char *TAG = "news_model";

/* ── lifecycle ───────────────────────────────────────────────── */

void news_model_init(NewsApp *app)
{
    app->model = malloc(sizeof(NewsModel));
    if (!app->model) {
        ESP_LOGE(TAG, "Failed to alloc NewsModel");
        return;
    }
    memset(app->model, 0, sizeof(NewsModel));
    app->model->active_idx  = -1;
    app->model->next_page   = 1;
    app->model->has_more    = true;   /* 还没拉过，先当作有 */
    app->model->list_state  = NEWS_LOAD_IDLE;
}

void news_model_deinit(NewsApp *app)
{
    if (!app->model) return;
    news_model_clear_content(app->model);
    free(app->model);
    app->model = NULL;
}

/* ── 列表 ────────────────────────────────────────────────────── */

void news_model_reset_list(NewsModel *model)
{
    memset(model->items, 0, sizeof(model->items));
    model->count      = 0;
    model->active_idx = -1;
    model->next_page  = 1;
    model->has_more   = true;
    news_model_set_more_state(model, NEWS_LOAD_IDLE, NULL);
    /* 正文缓存不清：刷新后 id 若还在列表里，进详情就不用重拉 */
}

bool news_model_is_full(const NewsModel *model)
{
    return model->count >= NEWS_MAX_ARTICLES;
}

uint8_t news_model_append(NewsModel *model, const NewsArticle *items, uint16_t count)
{
    uint8_t added = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (news_model_is_full(model)) {
            ESP_LOGW(TAG, "list full (%d), dropping %u remaining",
                     NEWS_MAX_ARTICLES, (unsigned)(count - i));
            break;
        }
        model->items[model->count++] = items[i];
        added++;
    }
    return added;
}

bool news_model_select(NewsModel *model, uint8_t idx)
{
    if (idx >= model->count) return false;
    model->active_idx = (int8_t)idx;
    return true;
}

NewsArticle *news_model_active(NewsModel *model)
{
    if (model->active_idx < 0 || model->active_idx >= model->count) return NULL;
    return &model->items[model->active_idx];
}

/* ── 正文 ────────────────────────────────────────────────────── */

void news_model_clear_content(NewsModel *model)
{
    free(model->content);
    model->content = NULL;
    model->content_id[0] = '\0';
}

bool news_model_set_content(NewsModel *model, const char *id, const char *text)
{
    if (!id || !text) return false;

    if (!model->content) {
        model->content = malloc(NEWS_CONTENT_LEN);
        if (!model->content) {
            ESP_LOGE(TAG, "no memory for article content");
            model->content_id[0] = '\0';
            return false;
        }
    }
    snprintf(model->content, NEWS_CONTENT_LEN, "%s", text);
    snprintf(model->content_id, sizeof(model->content_id), "%s", id);
    return true;
}

const char *news_model_active_content(NewsModel *model)
{
    NewsArticle *art = news_model_active(model);
    if (!art || !model->content) return NULL;
    if (strcmp(art->id, model->content_id) != 0) return NULL;   /* 缓存属于别篇 */
    return model->content;
}

/* ── 状态 ────────────────────────────────────────────────────── */

static void set_state(NewsLoadState *slot, char *err_buf, size_t err_len,
                      NewsLoadState st, const char *err)
{
    *slot = st;
    if (err) {
        snprintf(err_buf, err_len, "%s", err);
    } else {
        err_buf[0] = '\0';
    }
}

void news_model_set_list_state(NewsModel *model, NewsLoadState st, const char *err)
{
    set_state(&model->list_state, model->list_err, sizeof(model->list_err), st, err);
}

void news_model_set_more_state(NewsModel *model, NewsLoadState st, const char *err)
{
    set_state(&model->more_state, model->more_err, sizeof(model->more_err), st, err);
}

void news_model_set_detail_state(NewsModel *model, NewsLoadState st, const char *err)
{
    set_state(&model->detail_state, model->detail_err, sizeof(model->detail_err), st, err);
}
