/**
 * @file lv_alarm_alert.h
 * @brief 全局闹钟 / 计时提醒弹层。
 *
 * 闹钟和倒计时是系统服务（system/alarm），到点时用户可能在任何一个 app 里 ——
 * 甚至在桌面。所以提醒必须画在 lv_layer_top 上，而不是 Clock app 的某个页面里。
 *
 * 本模块只负责显示，触发逻辑和震动都在 alarm_service：
 * 它订阅 APP_EVENT_ALARM_FIRED / APP_EVENT_TIMER_FINISHED，收到就弹。
 *
 * 开机调一次 lv_alarm_alert_init()（在状态栏之后），此后不用管。
 */

#ifndef LV_ALARM_ALERT_H
#define LV_ALARM_ALERT_H

#ifdef __cplusplus
extern "C" {
#endif

/** 注册事件监听。必须在 app_event 和 LVGL 就绪之后调用。 */
void lv_alarm_alert_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_ALARM_ALERT_H */
