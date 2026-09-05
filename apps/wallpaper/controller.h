#ifndef WALLPAPER_CONTROLLER_H
#define WALLPAPER_CONTROLLER_H

#include <lvgl.h>
#include "model.h"
#include "view.h"

typedef struct {
    WallpaperModel* model;
    WallpaperView* view;
    lv_timer_t* monitor_timer;  // 监听闲置时间的定时器
    lv_timer_t* slide_timer;    // PPT 轮播定时器
} WallpaperController;

void wallpaper_controller_init(WallpaperController* ctrl, WallpaperModel* model, WallpaperView* view);

#endif // WALLPAPER_CONTROLLER_H