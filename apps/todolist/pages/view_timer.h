#ifndef TODOLIST_VIEW_TIMER_H
#define TODOLIST_VIEW_TIMER_H

#include "lvgl.h"
#include "../view.h"
#include "../model.h"

// Timer 页视图函数声明
void todolist_view_timer_init(TodoListView* v);
void todolist_view_timer_deinit(TodoListView* v);
void view_build_timer_tab(TodoListView* v);

/* 用 model 里的数据把整个 Timer 页刷成一致状态（任务名 / 预计 / 剩余 / 进度 / 按钮文案）。
 * t 传 NULL 表示当前没有任务。running 决定开始按钮显示 Start 还是 Pause。
 * 整页都变了，所以会重新打一帧全屏基准并重新钉住局部刷新窗口。 */
void todolist_view_timer_sync(const todolist_task_t *t, bool running);
/* 等价于 todolist_view_timer_sync(t, false) */
void todolist_view_timer_update(todolist_task_t *t);

/* 只更新倒计时和进度条，并把面板刷新窗口切到这一块 */
void todolist_view_timer_refresh_progress(const todolist_task_t *t);
/* 更新开始/暂停按钮，与本帧最终的倒计时使用同一个刷新窗口。 */
void todolist_view_timer_set_running(bool running);

/* 剩余秒数 → MM:SS 的显示变化标记。
 * 控制器按秒检测变化，同一秒内不重复刷新。 */
uint32_t todolist_view_timer_display_key(uint32_t rem_s);

#endif // TODOLIST_VIEW_TIMER_H
