#ifndef CLOCK_CONTROLLER_H
#define CLOCK_CONTROLLER_H

#include "esp_system.h"
#include <lvgl.h>
#include "lv_tab.h"
#include "clock_app.h"
#include "model.h"
#include "view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ClockController {
    ClockModel *model;
    ClockView  *view;

    /* 倒计时页面的刷新定时器。只在 Timer 页可见时存在 ——
     * 计时本身由 system/alarm 负责，这个定时器只管把数字画上去。 */
    lv_timer_t *ui_tick;
} ClockController;

/* lifecycle */
void clock_controller_init(ClockApp *app);
void clock_controller_deinit(ClockApp *app);

void clock_controller_on_tab_changed(lv_tab_t *tab, uint32_t idx, void *user_data);

/* Alarm page callbacks */
void clock_controller_on_add_alarm(lv_event_t *e);
void clock_controller_on_alarm_toggle(lv_event_t *e);
void clock_controller_on_alarm_row_clicked(lv_event_t *e);
void clock_controller_on_alarm_save(lv_event_t *e);
void clock_controller_on_alarm_delete(lv_event_t *e);

/* Timer page callbacks */
void clock_controller_on_timer_start_pause(lv_event_t *e);
void clock_controller_on_timer_reset(lv_event_t *e);

/* Timer preset callbacks */
void clock_controller_on_timer_preset_apply(lv_event_t *e);
void clock_controller_on_timer_preset_edit_open(lv_event_t *e);
void clock_controller_on_timer_preset_save(lv_event_t *e);
void clock_controller_on_timer_custom_use(lv_event_t *e);

/* 由 Timer 页在建立 / 销毁时调用，控制刷新定时器的存活 */
void clock_controller_timer_ui_attach(ClockApp *app);
void clock_controller_timer_ui_detach(ClockApp *app);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_CONTROLLER_H */
