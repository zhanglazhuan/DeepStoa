#ifndef TODOLIST_VIEW_SETTINGS_H
#define TODOLIST_VIEW_SETTINGS_H

#include "lvgl.h"
#include "../view.h"
#include "../model.h"

struct TodoListApp;

void view_open_settings(void);
void todolist_view_settings_init_registry(struct TodoListApp* app);

#endif // TODOLIST_VIEW_SETTINGS_H
