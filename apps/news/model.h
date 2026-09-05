#ifndef NEWS_MODEL_H
#define NEWS_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── tunables ─────────────────────────────────────────────────── */
#define NEWS_PAGE_SIZE      6      /* 一页拉多少条 */
#define NEWS_MAX_ARTICLES   24     /* 列表最多攒多少条（= 4 页），到顶后 has_more 强制关掉 */
#define NEWS_ID_LEN         24
#define NEWS_TITLE_LEN      128
#define NEWS_SOURCE_LEN     48
#define NEWS_DATE_LEN       24
#define NEWS_SUMMARY_LEN    192
#define NEWS_CONTENT_LEN    4096   /* 正文上限，超出由 service 层截断 */
#define NEWS_ERRMSG_LEN     96

/* ── 三条独立的加载轨道共用一个状态机 ────────────────────────────
 * list   : 首屏 / 刷新
 * more   : 加载下一页
 * detail : 正文
 */
typedef enum {
    NEWS_LOAD_IDLE = 0,
    NEWS_LOAD_BUSY,
    NEWS_LOAD_DONE,
    NEWS_LOAD_ERROR,
} NewsLoadState;

/* ── 列表条目：只放列表卡片要用的字段，正文单独按需拉 ──────────── */
typedef struct NewsArticle {
    char id[NEWS_ID_LEN];
    char title[NEWS_TITLE_LEN];
    char source[NEWS_SOURCE_LEN];
    char date[NEWS_DATE_LEN];
    char summary[NEWS_SUMMARY_LEN];
} NewsArticle;

/* ── model ────────────────────────────────────────────────────── */
typedef struct NewsModel {
    NewsArticle   items[NEWS_MAX_ARTICLES];
    uint8_t       count;
    int8_t        active_idx;       /* -1 = 没选中 */

    uint16_t      next_page;        /* 下一次要请求的页号，1 起 */
    bool          has_more;         /* 服务端还有下一页，且本地还装得下 */

    NewsLoadState list_state;
    NewsLoadState more_state;
    NewsLoadState detail_state;

    char          list_err[NEWS_ERRMSG_LEN];
    char          more_err[NEWS_ERRMSG_LEN];
    char          detail_err[NEWS_ERRMSG_LEN];

    /* 正文缓存：一次只留一篇，进详情页按需 malloc */
    char         *content;
    char          content_id[NEWS_ID_LEN];
} NewsModel;

/* lifecycle */
void news_model_init(NewsApp *app);
void news_model_deinit(NewsApp *app);

/* 列表 */
void     news_model_reset_list(NewsModel *model);
uint8_t  news_model_append(NewsModel *model, const NewsArticle *items, uint16_t count);
bool     news_model_is_full(const NewsModel *model);

bool         news_model_select(NewsModel *model, uint8_t idx);
NewsArticle *news_model_active(NewsModel *model);

/* 正文 */
void        news_model_clear_content(NewsModel *model);
bool        news_model_set_content(NewsModel *model, const char *id, const char *text);
const char *news_model_active_content(NewsModel *model);

/* 状态 */
void news_model_set_list_state(NewsModel *model, NewsLoadState st, const char *err);
void news_model_set_more_state(NewsModel *model, NewsLoadState st, const char *err);
void news_model_set_detail_state(NewsModel *model, NewsLoadState st, const char *err);

#ifdef __cplusplus
}
#endif

#endif /* NEWS_MODEL_H */
