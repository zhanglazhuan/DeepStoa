#include <string.h>
#include "model.h"

void wallpaper_model_init(WallpaperModel* model) {
    if (!model) return;
    
    // 默认配置
    model->config.mode = WALLPAPER_MODE_TEXT;
    model->config.idle_timeout_s = 120;   // 默认 120 秒无操作锁屏
    model->config.slide_interval_s = 60; // 默认 60 秒切换一次
    strncpy(model->config.img_folder_path, "A:/wallpapers", WALLPAPER_PATH_MAX - 1);
    model->config.img_folder_path[WALLPAPER_PATH_MAX - 1] = '\0';
    strncpy(model->config.text_file_path, "A:/quotes.txt", WALLPAPER_PATH_MAX - 1);
    model->config.text_file_path[WALLPAPER_PATH_MAX - 1] = '\0';
    
    model->is_showing = false;
    model->current_file_index = 0;
    model->current_text_offset = 0; // 从文件头开始
}

void wallpaper_model_update_config(WallpaperModel* model, wallpaper_mode_t mode, uint32_t timeout, uint32_t interval, const char* img_folder, const char* text_file) {
    if (!model) return;
    
    // 如果模式切换或文本路径改变，重置文本读取进度
    if (model->config.mode != mode || (text_file && strcmp(model->config.text_file_path, text_file) != 0)) {
        model->current_text_offset = 0;
    }

    model->config.mode = mode;
    model->config.idle_timeout_s = timeout;
    model->config.slide_interval_s = interval;
    
    if (img_folder) {
        strncpy(model->config.img_folder_path, img_folder, WALLPAPER_PATH_MAX - 1);
        model->config.img_folder_path[WALLPAPER_PATH_MAX - 1] = '\0';
    }
    if (text_file) {
        strncpy(model->config.text_file_path, text_file, WALLPAPER_PATH_MAX - 1);
        model->config.text_file_path[WALLPAPER_PATH_MAX - 1] = '\0';
    }
}
