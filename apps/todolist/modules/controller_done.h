#ifndef TODOLIST_CONTROLLER_DONE_H
#define TODOLIST_CONTROLLER_DONE_H

#include "lvgl.h"

// Done 页控制器函数声明
void controller_on_done_checkbox_changed(lv_event_t * e);
void controller_on_done_delete(lv_event_t * e);

#endif // TODOLIST_CONTROLLER_DONE_H