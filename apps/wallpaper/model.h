#ifndef WALLPAPER_MODEL_H
#define WALLPAPER_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <dirent.h>

#define WALLPAPER_PATH_MAX 128
#define MAX_TEXT_LINE_LEN  256

typedef enum {
    WALLPAPER_MODE_BLANK,
    WALLPAPER_MODE_IMAGE,
    WALLPAPER_MODE_TEXT,
    WALLPAPER_MODE_DATETIME
} wallpaper_mode_t;

// 用户配置
typedef struct {
    wallpaper_mode_t mode;             // 当前壁纸模式
    uint32_t idle_timeout_s;       // 无操作多少秒后进入锁屏
    uint32_t slide_interval_s;     // PPT 轮播切换频率（秒）
    char img_folder_path[WALLPAPER_PATH_MAX]; // 壁纸文件夹路径 (例如 "A:/wallpapers")
    char text_file_path[WALLPAPER_PATH_MAX];
} wallpaper_config_t;

// 运行时模型
typedef struct {
    wallpaper_config_t config;
    bool is_showing;               // 当前是否正在显示壁纸
    uint32_t current_file_index;   // 当前显示的图片索引
    off_t current_text_offset;     // 当前读取文本的文件字节偏移量
} WallpaperModel;

void wallpaper_model_init(WallpaperModel* model);
void wallpaper_model_update_config(WallpaperModel* model, wallpaper_mode_t mode, uint32_t timeout, uint32_t interval, const char* img_folder, const char* text_file);

#endif // WALLPAPER_MODEL_H