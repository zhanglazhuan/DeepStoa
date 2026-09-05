#ifndef TODOLIST_APP_H
#define TODOLIST_APP_H

#include <lvgl.h>

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

#endif // TODOLIST_APP_H