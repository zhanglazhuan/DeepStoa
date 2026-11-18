#ifndef TODOLIST_APP_H
#define TODOLIST_APP_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// 前向声明
struct TodoListView;
struct TodoListModel;
struct TodoListController;

typedef struct TodoListApp {
    struct TodoListModel* model;
    struct TodoListView* view;
    struct TodoListController* controller;
} TodoListApp;

extern TodoListApp g_todolist_app;

/* Register TodoList with the application manager. */
void todolist_init(void);

#ifdef __cplusplus
}
#endif

#endif // TODOLIST_APP_H
