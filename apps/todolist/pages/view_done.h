#ifndef TODOLIST_VIEW_DONE_H
#define TODOLIST_VIEW_DONE_H

#include "lvgl.h"
#include "../view.h"
#include "../model.h"

// Done 页视图函数声明
lv_obj_t * view_create_done_row(lv_obj_t * parent, const todolist_task_t * t);
void view_refresh_done_list(void);
void view_build_done_tab(TodoListView* v);

#endif // TODOLIST_VIEW_DONE_H