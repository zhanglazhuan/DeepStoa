#ifndef TODOLIST_CONTROLLER_H
#define TODOLIST_CONTROLLER_H

#include "lvgl.h"
#include "lv_tab.h"
#include "model.h"
#include "view.h"
#include "./modules/controller_timer.h" // 引入刚才定义的 Timer Context

// 前向声明
struct TodoListView;

// 控制器结构
typedef struct TodoListController {
    // 将所有定时器状态封装并挂载在这里
    TodoListTimerController *timer_ctx;
    
    // 指向 model 和 view 的指针
    TodoListModel* model;
    TodoListView* view;
} TodoListController;

void todolist_controller_init(TodoListApp* app);
void todolist_controller_deinit(TodoListApp* app);

// 视图事件回调
void controller_on_open_settings(lv_event_t * e);
void controller_on_about_clicked(lv_event_t * e);
void todolist_controller_open_settings(void);

// Tab 操作
void todolist_controller_on_tab_changed(lv_tab_t *tab, uint32_t idx, void *user_data);
void todolist_controller_active_tab(uint16_t tab_idx, bool with_anim);

void settings_clear_done_cb(lv_event_t * e);

#endif // TODOLIST_CONTROLLER_H