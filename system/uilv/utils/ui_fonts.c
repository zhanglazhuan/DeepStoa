#include "esp_log.h"
#include "ui_fonts.h"
#include "lv_theme_hardcore.h"

static const char *TAG = "ui_fonts";

// 静态分配在 RAM 中的字体结构体
lv_font_t custom_font_normal; 
static lv_font_t ram_font_grid;
static lv_font_t ram_font_clock;
static lv_font_t ram_font_landscape;
static lv_font_t ram_font_info;
static lv_font_t ram_font_doing;
static lv_font_t ram_font_edit;
static lv_font_t ram_font_draggable;
static lv_font_t ram_font_wifi;
static lv_font_t ram_font_battery;
static lv_font_t ram_font_alarm;

void ui_fonts_init(void) {
    ESP_LOGI(TAG,"Initializing RAM fallback fonts...");

    // 1. 将 ROM (Flash) 中的只读字体结构体，完整克隆一份到 RAM 中
    custom_font_normal = *LV_FONT_NORMAL;
    ram_font_grid = icon_grid;
    ram_font_clock = icon_clock;
    ram_font_landscape = icon_landscape;
    ram_font_info = icon_info;
    ram_font_doing = icon_doing;
    ram_font_edit = icon_edit;
    ram_font_draggable = icon_draggable;
    ram_font_wifi = icon_wifi;
    ram_font_battery = icon_battery;
    ram_font_alarm = icom_alarm;
    
    // 2. 在 RAM 中极其安全地进行 fallback 链表拼接，绝不会被 Cache 吞掉
    custom_font_normal.fallback = &ram_font_grid;
    ram_font_grid.fallback = &ram_font_clock;
    ram_font_clock.fallback = &ram_font_landscape;
    ram_font_landscape.fallback = &ram_font_info;
    ram_font_info.fallback = &ram_font_doing;
    ram_font_doing.fallback = &ram_font_edit;
    ram_font_edit.fallback = &ram_font_draggable;
    ram_font_draggable.fallback = &ram_font_wifi;
    ram_font_wifi.fallback = &ram_font_battery;
    ram_font_battery.fallback = &ram_font_alarm;
    ram_font_alarm.fallback = NULL;

    ESP_LOGI(TAG,"RAM fallback fonts mounted securely.");
}