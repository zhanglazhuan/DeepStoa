#ifndef LV_STATUS_BAR_H
#define LV_STATUS_BAR_H

#include <lvgl.h>

/** 状态栏高度 */
#define LV_STATUS_BAR_HEIGHT 40

// 初始化全局状态栏
void lv_status_bar_init(void);

// 外部事件触发状态更新 (例如 WIFI 连上、电量变化时调用)
void lv_status_bar_update(void);

void lv_status_bar_set_visible(bool visible);
lv_coord_t lv_status_bar_get_height(void);

#endif // LV_STATUS_BAR_H
