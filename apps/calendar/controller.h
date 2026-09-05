#ifndef CALENDAR_CONTROLLER_H
#define CALENDAR_CONTROLLER_H

#include <lvgl.h>
#include "app.h"
#include "model.h"
#include "view.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 控制器目前是无状态的：没有在途请求，也没有定时器。
 * 结构体留着是为了和其他 app 的 MVC 形状一致，以及日程功能落地后有地方放。 */
typedef struct CalendarController {
    CalendarModel *model;
    CalendarView  *view;
} CalendarController;

/* Lifecycle */
void calendar_controller_init(CalendarApp *app);
void calendar_controller_deinit(CalendarApp *app);

/* 月份导航（挂在 LVGL 控件上，user_data 是 CalendarApp*） */
void calendar_controller_on_prev_month(lv_event_t *e);
void calendar_controller_on_next_month(lv_event_t *e);
void calendar_controller_on_today(lv_event_t *e);

/* 月份选择器 */
void calendar_controller_on_month_title_clicked(lv_event_t *e);
void calendar_controller_on_picker_year_prev(lv_event_t *e);
void calendar_controller_on_picker_year_next(lv_event_t *e);

/** 选中某个月：由 view 的宫格回调转发过来，点中即生效，没有确认步骤。 */
void calendar_controller_on_month_picked(CalendarApp *app, uint8_t month);

#ifdef __cplusplus
}
#endif

#endif /* CALENDAR_CONTROLLER_H */
