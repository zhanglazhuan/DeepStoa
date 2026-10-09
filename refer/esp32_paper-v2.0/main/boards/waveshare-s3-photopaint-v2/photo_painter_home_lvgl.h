#ifndef PHOTO_PAINTER_HOME_LVGL_H
#define PHOTO_PAINTER_HOME_LVGL_H

#include <stdbool.h>
#include <stdint.h>

#include "pcf85063_bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

bool photo_painter_lvgl_home_show(int selection, int refresh_mode);
bool photo_painter_lvgl_home_move_selection(int selection, int refresh_mode);
bool photo_painter_lvgl_home_update_status(Time_data rtc_time);

#ifdef __cplusplus
}
#endif

#endif
