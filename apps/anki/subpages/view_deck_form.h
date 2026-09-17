#ifndef ANKI_VIEW_DECK_FORM_H
#define ANKI_VIEW_DECK_FORM_H

#include "../app.h"

// --- 定义表单的模式 ---
typedef enum {
    DECK_FORM_MODE_ADD_ROOT, // 新建根牌组
    DECK_FORM_MODE_ADD_SUB,  // 新建子牌组
    DECK_FORM_MODE_EDIT      // 编辑现有牌组
} DeckFormMode;

// --- 页面传参载荷 ---
typedef struct {
    DeckFormMode mode;
    int target_deck_idx; // 用于 Edit (被编辑的 deck 索引) 或 Add Sub (父 deck 索引)
} DeckFormPayload;

// 注册页面初始化
void anki_view_deck_form_init_registry(struct AnkiApp* app);

#endif // ANKI_VIEW_DECK_FORM_H