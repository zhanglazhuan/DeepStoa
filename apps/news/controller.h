#ifndef NEWS_CONTROLLER_H
#define NEWS_CONTROLLER_H

#include "app.h"
#include "model.h"
#include "view.h"
#include "news_svc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NewsController {
    NewsModel  *model;
    NewsView   *view;
    lv_timer_t *splash_timer;

    uint32_t    req_seq;        /* 请求号发号器，从 1 开始，0 表示"无在途" */
    uint32_t    list_req;       /* 在途的列表请求 */
    bool        list_req_more;  /* 该请求是"加载更多"而不是首屏/刷新 */
    uint32_t    detail_req;     /* 在途的正文请求 */
} NewsController;

/* lifecycle */
void news_controller_init(NewsApp *app);
/** model/view/controller 都接好之后调用：起启动动画计时并拉首屏。 */
void news_controller_start(NewsApp *app);
void news_controller_deinit(NewsApp *app);

/* 动作 —— 由子页面的事件回调调用 */
void news_controller_load_first(NewsApp *app);   /* 首屏 / 下拉刷新 */
void news_controller_load_more(NewsApp *app);    /* 加载下一页 */
void news_controller_open_article(NewsApp *app, uint8_t idx);
void news_controller_reload_detail(NewsApp *app);

/* 错误码 → 屏上文案（列表页和详情页共用） */
const char *news_err_text(news_svc_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* NEWS_CONTROLLER_H */
