#ifndef CLOCK_MODEL_H
#define CLOCK_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include "clock_app.h"
#include "alarm_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 闹钟表、计时器状态、快捷预设和它们的持久化都在 system/alarm 里 ——
 * app 关掉之后闹钟还得继续走，所以那些数据不能挂在 app 的生命周期上。
 * 这里只剩纯 UI 状态：这次打开看的是哪一页、正在编辑哪一条。
 */
typedef struct ClockModel {
    uint8_t active_tab;      /* 0 = Alarm, 1 = Timer */
    int8_t  edit_alarm_idx;  /* -1 = 新建 */
} ClockModel;

void clock_model_init(ClockApp *app);
void clock_model_deinit(ClockApp *app);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_MODEL_H */
