#ifndef ANKI_VIEW_H
#define ANKI_VIEW_H

#include "page_navigator.h"
#include "lv_theme_hardcore.h"
#include "lv_page.h"
#include "lv_bottom_sheet.h"
#include "lv_num_input.h"
#include "model.h"

#define ANKI_PAGE_ID_MAX 10

// 页面枚举
enum anki_page_id_t{
    PAGE_NONE = 0,
    PAGE_LAUNCH,
    PAGE_DECKS,
    PAGE_DECK_FORM,
    PAGE_STUDY,
    PAGE_SETTINGS,
    PAGE_STATS,
    PAGE_IMPORT,
    PAGE_FILE_SELECTOR
};

// 用于存放页面级别上下文，方便局部刷新
typedef struct {
    lv_obj_t * deck_list_cont;
} AnkiViewDecksCtx;

typedef struct AnkiViewStudyCtx {
    lv_obj_t * lbl_front;
    lv_obj_t * separator;
    lv_obj_t * lbl_answer;
    lv_obj_t * btn_answer;
    lv_obj_t * btn_rating;
} AnkiViewStudyCtx;

typedef struct AnkiView {
    page_navigator_t page_nav;
} AnkiView;

struct AnkiApp;

void anki_view_init(struct AnkiApp *app);
void anki_view_deinit(struct AnkiApp *app);

// 注册具体页面的构建函数
void anki_view_pages_init_registry(struct AnkiApp *app);

#endif // ANKI_VIEW_H