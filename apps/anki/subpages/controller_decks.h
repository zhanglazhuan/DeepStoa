#ifndef ANKI_CONTROLLER_DECKS_H
#define ANKI_CONTROLLER_DECKS_H

#include "../app.h"
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void anki_controller_on_deck_clicked(lv_event_t *e);

// 事件处理器
void anki_controller_on_nav_btn_clicked(lv_event_t *e); // 底部导航栏切换

// 处理表单提交的动作接口
void anki_controller_add_root_deck(AnkiApp *app, const char *name);
void anki_controller_add_sub_deck(AnkiApp *app, int parent_idx, const char *name);
void anki_controller_edit_deck(AnkiApp *app, int deck_idx, const char *name);

#ifdef __cplusplus
}
#endif

#endif // ANKI_CONTROLLER_DECKS_H