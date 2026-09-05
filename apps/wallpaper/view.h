#ifndef WALLPAPER_VIEW_H
#define WALLPAPER_VIEW_H

#include <lvgl.h>
#include "lv_theme_hardcore.h"

typedef struct {
    lv_obj_t* overlay; // 盖在系统顶层的容器
    lv_obj_t* img_obj; // 图片组件
    lv_obj_t* label_obj; // 文本组件
} WallpaperView;

void wallpaper_view_init(WallpaperView* view);
void wallpaper_view_show(WallpaperView* view, lv_event_cb_t wake_up_cb, void* user_data);
void wallpaper_view_hide(WallpaperView* view);
void wallpaper_view_set_image(WallpaperView* view, const char* img_path);
void wallpaper_view_set_text(WallpaperView* view, const char* text);

#endif // WALLPAPER_VIEW_H