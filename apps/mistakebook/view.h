#ifndef MISTAKEBOOK_VIEW_H
#define MISTAKEBOOK_VIEW_H

#include <stdbool.h>
#include "framework/page_navigator.h"

struct MistakebookApp;

enum {
    MISTAKEBOOK_PAGE_NONE = 0,
    MISTAKEBOOK_PAGE_LIST,
    MISTAKEBOOK_PAGE_DETAIL,
};

typedef struct MistakebookView {
    page_navigator_t page_nav;
    lv_obj_t *list;
    lv_obj_t *detail_status;
} MistakebookView;

bool mistakebook_view_init(struct MistakebookApp *app);
void mistakebook_view_deinit(struct MistakebookApp *app);
void mistakebook_view_refresh_list(struct MistakebookApp *app);
void mistakebook_view_refresh_detail(struct MistakebookApp *app);

#endif
