#ifndef NEWS_VIEW_DETAIL_H
#define NEWS_VIEW_DETAIL_H

#include "../app.h"

#ifdef __cplusplus
extern "C" {
#endif

void news_view_detail_init_registry(NewsApp *app);

/** 按 model 当前状态重建正文区。只应由 news_view_sync_detail() 调用。 */
void news_view_detail_render(NewsApp *app);

#ifdef __cplusplus
}
#endif

#endif /* NEWS_VIEW_DETAIL_H */
