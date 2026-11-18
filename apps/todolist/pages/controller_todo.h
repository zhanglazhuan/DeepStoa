#ifndef TODOLIST_CONTROLLER_TODO_H
#define TODOLIST_CONTROLLER_TODO_H

#include <lvgl.h>

// Todo 页控制器函数声明
void controller_on_todo_label_swipe(lv_event_t * e);
void controller_on_add_btn_clicked(lv_event_t * e);
void controller_on_todo_checkbox_changed(lv_event_t * e);

void controller_on_todo_label_edit(lv_event_t * e);

void controller_on_todo_drag_pressed(lv_event_t * e);
void controller_on_todo_drag_pressing(lv_event_t * e);
void controller_on_todo_drag_released(lv_event_t * e);

#endif // TODOLIST_CONTROLLER_TODO_H