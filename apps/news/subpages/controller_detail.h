#ifndef NEWS_CONTROLLER_DETAIL_H
#define NEWS_CONTROLLER_DETAIL_H

#include <lvgl.h>
#include "../app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* user_data 一律是 NewsApp* */
void news_controller_on_detail_back(lv_event_t *e);
void news_controller_on_detail_retry(lv_event_t *e);

#ifdef __cplusplus
}
#endif

#endif /* NEWS_CONTROLLER_DETAIL_H */
