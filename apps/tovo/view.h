#ifndef TOVO_VIEW_H
#define TOVO_VIEW_H

#include <stdbool.h>
#include "framework/page_navigator.h"

struct TovoApp;
typedef struct TovoView {
    page_navigator_t page_nav;
    lv_obj_t *root;
    lv_obj_t *status;
    lv_obj_t *list;
} TovoView;

bool tovo_view_init(struct TovoApp *app);
void tovo_view_deinit(struct TovoApp *app);
void tovo_view_render(struct TovoApp *app);

#endif
