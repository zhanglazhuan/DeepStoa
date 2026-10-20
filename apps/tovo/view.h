#ifndef TOVO_VIEW_H
#define TOVO_VIEW_H

#include <stdbool.h>
#include "framework/page_navigator.h"
#include "widgets/lv_todo_list_panel.h"

struct TovoApp;
typedef struct TovoView {
    page_navigator_t page_nav;
    lv_todo_list_panel_t panel;
} TovoView;

bool tovo_view_init(struct TovoApp *app);
void tovo_view_deinit(struct TovoApp *app);
void tovo_view_render(struct TovoApp *app);

#endif
