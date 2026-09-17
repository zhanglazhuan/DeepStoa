#include "esp_log.h"
#include "app.h"

static const char *TAG = "wallpaper_app";

// 全局单例
static WallpaperApp g_wallpaper_app;

// 系统启动时的初始化入口
void wallpaper_app_init(void) {
    ESP_LOGI(TAG, "Initializing Wallpaper Service...");

    wallpaper_model_init(&g_wallpaper_app.model);
    wallpaper_view_init(&g_wallpaper_app.view);
    wallpaper_controller_init(&g_wallpaper_app.controller, &g_wallpaper_app.model, &g_wallpaper_app.view);
}

// 暴露给其它应用的接口：更新壁纸配置
void wallpaper_app_set_config(wallpaper_mode_t mode, uint32_t timeout_s, uint32_t slide_interval_s, const char* img_folder_path, const char* text_file_path) {
    wallpaper_model_update_config(&g_wallpaper_app.model, mode, timeout_s, slide_interval_s, img_folder_path, text_file_path);
    ESP_LOGI(TAG, "Wallpaper config updated: Mode %d, %ds timeout, %ds interval", mode, timeout_s, slide_interval_s);
}