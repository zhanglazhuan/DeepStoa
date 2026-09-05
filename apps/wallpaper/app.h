#ifndef WALLPAPER_APP_H
#define WALLPAPER_APP_H

#include "model.h"
#include "view.h"
#include "controller.h"

typedef struct {
    WallpaperModel model;
    WallpaperView view;
    WallpaperController controller;
} WallpaperApp;

// 暴露初始化接口
void wallpaper_app_init(void);

// 暴露给 Settings 页面，允许用户修改配置
void wallpaper_app_set_config(wallpaper_mode_t mode, uint32_t timeout_s, uint32_t slide_interval_s, const char* img_folder_path, const char* text_file_path);

#endif // WALLPAPER_APP_H