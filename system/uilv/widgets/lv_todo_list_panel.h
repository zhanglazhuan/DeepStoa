#ifndef LV_TODO_LIST_PANEL_H
#define LV_TODO_LIST_PANEL_H

#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"

typedef struct {
    const char *content;
    bool completed;
} lv_todo_row_t;

typedef void (*lv_todo_select_cb_t)(size_t index, void *user_data);

typedef struct {
    lv_obj_t *root;
    lv_obj_t *status;
    lv_obj_t *list;
    lv_todo_select_cb_t on_select;
    void *user_data;
} lv_todo_list_panel_t;

void lv_todo_list_panel_create(lv_obj_t *parent, lv_todo_list_panel_t *panel,
                               lv_event_cb_t refresh_cb, void *refresh_data);
void lv_todo_list_panel_render(lv_todo_list_panel_t *panel,
                               const lv_todo_row_t *rows, size_t count,
                               const char *status, lv_todo_select_cb_t on_select,
                               void *user_data);

#endif
