#ifndef NEWS_VIEW_H
#define NEWS_VIEW_H

#include <lvgl.h>
#include "app.h"
#include "page_navigator.h"
#include "lv_theme_hardcore.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── page IDs ────────────────────────────────────────────────── */
#define NEWS_PAGE_ID_MAX 4

typedef enum {
    PAGE_NEWS_NONE = 0,
    PAGE_NEWS_LAUNCH,
    PAGE_NEWS_LIST,     /* 新闻列表：卡片流 */
    PAGE_NEWS_DETAIL,   /* 新闻详情：标题 + 正文 */
} NewsPageId;

/* ── 字号分级（都取自主题，避免各页面各写各的） ────────────────
 * 屏幕 480x800，~235 dpi，正文用 24 才是舒服的阅读尺寸。 */
#define NEWS_FONT_TITLE   LV_FONT_NORMAL   /* montserrat 32 */
#define NEWS_FONT_BODY    LV_FONT_SMALL    /* montserrat 24 */
#define NEWS_FONT_META    LV_FONT_TINY     /* montserrat 18 */

/* ── 列表页 ──────────────────────────────────────────────────
 * 列表内容整块重建（卡片 / 占位 / 页脚都挂在 scroll 下），
 * 所以这里只留容器指针，不缓存单个控件。 */
typedef struct NewsListCtx {
    lv_obj_t *scroll;
} NewsListCtx;

/* ── 详情页 ──────────────────────────────────────────────────
 * 标题和 meta 建好就不变，只有正文区随状态重建。 */
typedef struct NewsDetailCtx {
    lv_obj_t *scroll;
    lv_obj_t *body_slot;
} NewsDetailCtx;

/* ── view ────────────────────────────────────────────────────── */
typedef struct NewsView {
    page_navigator_t page_nav;
    NewsListCtx      list_ctx;
    NewsDetailCtx    detail_ctx;
} NewsView;

/* lifecycle */
void news_view_init(NewsApp *app);
void news_view_deinit(NewsApp *app);

/* controller → view：只在对应页面处于前台时才真正重绘 */
void news_view_sync_list(NewsApp *app);
void news_view_sync_detail(NewsApp *app);

#ifdef __cplusplus
}
#endif

#endif /* NEWS_VIEW_H */
