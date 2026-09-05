#ifndef NEWS_VIEW_LIST_H
#define NEWS_VIEW_LIST_H

#include "../app.h"

#ifdef __cplusplus
extern "C" {
#endif

void news_view_list_init_registry(NewsApp *app);

/** 按 model 当前状态整块重建列表内容。只应由 news_view_sync_list() 调用。 */
void news_view_list_render(NewsApp *app);

#ifdef __cplusplus
}
#endif

#endif /* NEWS_VIEW_LIST_H */
