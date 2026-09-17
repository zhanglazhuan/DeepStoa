#ifndef TODOLIST_VIEW_ABOUT_H
#define TODOLIST_VIEW_ABOUT_H

#include <lvgl.h>

struct TodoListApp;

void view_open_about(void);
void todolist_view_about_init_registry(struct TodoListApp* app);

#endif // TODOLIST_VIEW_ABOUT_H
