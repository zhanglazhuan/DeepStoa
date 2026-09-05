#ifndef NEWS_CONTROLLER_LIST_H
#define NEWS_CONTROLLER_LIST_H

#include <lvgl.h>
#include "../app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 直接挂在 LVGL 控件上的回调，user_data 一律是 NewsApp* */
void news_controller_on_article_clicked(lv_event_t *e);
void news_controller_on_refresh_clicked(lv_event_t *e);
void news_controller_on_load_more_clicked(lv_event_t *e);

/** 长按刷新键：给 mock 数据源注入一次网络故障，用来在真机上验证错误分支。 */
void news_controller_on_refresh_long_pressed(lv_event_t *e);

#ifdef __cplusplus
}
#endif

#endif /* NEWS_CONTROLLER_LIST_H */
