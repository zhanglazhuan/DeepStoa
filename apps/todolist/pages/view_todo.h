#ifndef TODOLIST_VIEW_TODO_H
#define TODOLIST_VIEW_TODO_H

#include "lvgl.h"
#include "../view.h"
#include "../model.h"

// Todo 页视图函数声明
lv_obj_t * view_create_todo_row(lv_obj_t *parent, const todolist_task_t *t);
void view_refresh_todo_list(void * user_data);
void view_build_todo_tab(TodoListView* v);
/* 传 id 而不是指针：弹窗要跨若干帧存活，期间数组可能已经被压缩过 */
void view_todo_show_delete_dialog(uint32_t task_id);

#endif // TODOLIST_VIEW_TODO_H
