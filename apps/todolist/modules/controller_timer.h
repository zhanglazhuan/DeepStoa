#ifndef TODOLIST_CONTROLLER_TIMER_H
#define TODOLIST_CONTROLLER_TIMER_H

#include "lvgl.h"

/* 封装所有 Timer 相关的状态控制变量。
 *
 * 计时本身不在这里 —— 已用时由 model 按时间戳计算（见 todolist_model_timer_*），
 * 这里只负责「什么时候去看一眼、要不要真的刷面板」。
 * 原来的快刷/慢刷状态机被删掉了：现在改成比对「显示值有没有变」，
 * 值没变就一帧都不推，比按固定周期刷省得多。 */
typedef struct {
    lv_timer_t * timer;              // 轮询定时器（只唤醒 CPU，不一定刷面板）
    uint32_t     last_display_key;   // 上一次真正推到面板的显示值
    bool         has_display_key;
} TodoListTimerController;

// 前向声明主 Controller
struct TodoListController;

// Timer 页控制器函数声明
void controller_timer_init(struct TodoListController* controller);
void controller_timer_deinit(struct TodoListController* controller);
void controller_on_timer_start_pause(lv_event_t * e);
void controller_on_timer_skip(lv_event_t * e);
void controller_on_timer_finish(lv_event_t * e);

void controller_on_timer_adjust_estimate(lv_event_t * e);

/* 进入/离开 Timer 页时调用，控制轮询定时器的开关 */
void controller_timer_resume_polling(struct TodoListController* controller);
void controller_timer_pause_polling(struct TodoListController* controller);

#endif // TODOLIST_CONTROLLER_TIMER_H
