// system/controller/touch_control.h
// Touch controller — LVGL input device wrapper
// Ported from D:\Codes\EPOS\epos\drivers\epd_touch_control.h

#ifndef TOUCH_CONTROL_H
#define TOUCH_CONTROL_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern lv_indev_t *touch_indev;
extern lv_group_t *input_group;

void touch_control_init(void);
void touch_control_set_power(bool on);

#ifdef __cplusplus
}
#endif

#endif
