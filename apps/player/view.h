#ifndef PLAYER_VIEW_H
#define PLAYER_VIEW_H

#include <lvgl.h>
#include "page_navigator.h"
#include "lv_theme_hardcore.h"
#include "lv_page.h"

#include "model.h"

struct PlayerApp;

typedef enum {
    PAGE_PLAYER_NONE = 0,
    PAGE_PLAYER_LIST,
    PAGE_PLAYER_PLAY,
} PlayerPageID;

#define PAGE_PLAYER_ID_MAX 4

typedef struct PlayerView {
    page_navigator_t page_nav;
    lv_obj_t *list_container;
} PlayerView;

void player_view_init(struct PlayerApp *app);
void player_view_deinit(struct PlayerApp *app);

/* 两个页面各自向 audio_service 订阅事件（页面销毁时取消订阅），
 * 播放状态不再由 app 持有，所以 view 层不需要再对外暴露刷新入口。 */

#endif // PLAYER_VIEW_H
