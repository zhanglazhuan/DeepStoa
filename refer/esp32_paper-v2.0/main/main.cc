#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_log.h"
// #include "esp_flash.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include <nvs.h>
#include <nvs_flash.h>
#include "esp_wifi.h"
#include "sdmmc_cmd.h"

// Module header file
// #include "esp_wifi_bsp.h"
#include "button_bsp.h"
#include "i2c_bsp.h"
#include "shtc3_bsp.h"
#include "pcf85063_bsp.h"
#include "sdcard_bsp.h"
#include "epaper_bsp.h"
#include "epaper_port.h"
#include "es8311_bsp.h"
#include "qmi8658_bsp.h"
#include "axp_prot.h"
// #include "wifi_configuration_ap.h"
// #include "wifi_station.h"
#include "ssid_manager.h"

// Page header file
#include "file_browser.h"
#include "page_network.h"
#include "page_weather.h"
#include "page_clock.h"
#include "page_alarm.h"
#include "page_audio.h"
#include "page_picture.h"
#include "page_settings.h"
#include "page_fiction.h"
#include "page_todolist.h"
#include "page_pomodoro_timer.h"
#include "page_chat.h"
#include "status_bar.h"
#include "usb_msc_manager.h"
#include "board.h"
#if CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
#include "boards/waveshare-s3-photopaint-v2/config.h"
#include "aw9523.h"
#endif

#include "freertos/semphr.h"

#include "application.h"

#if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint || CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
#include "boards/waveshare-s3-PhotoPainter/photo_painter_home_lvgl.h"
#endif

// Create a mutex lock to protect the device from interference when reading
SemaphoreHandle_t alarm_mutex = NULL; // protect alarms
SemaphoreHandle_t rtc_mutex = NULL;   // protect RTC
SemaphoreHandle_t nvs_mutex = NULL;   // protect NVS
SemaphoreHandle_t qmi8658_mutex = NULL;   // protect qmi8658

// The wifi is on indicator
bool wifi_enable;      


// The priority of creating thread tasks
#define ALARM_TASK_PRIO   5
#define USER_TASK_PRIO    3
 
// Quantity of individual main courses
#define HOME_PAGE_SIZE  12
#define HOME_PAGE_NUM   12
#define HOME_PAGE_COUNT ((HOME_PAGE_NUM + HOME_PAGE_SIZE - 1) / HOME_PAGE_SIZE)

// E-ink screen sleep time (S)
#define EPD_Sleep_Time   5
// Equipment shutdown time (minutes)
#define Unattended_Time  10

// Icon location
#define Icon_X_1   32
#define Icon_X_2   192
#define Icon_X_3   352
#define Icon_Y_1   78
#define Icon_Y_2   264
#define Icon_Y_3   450
#define Icon_Y_4   636

#define Text_X_1   (Icon_X_1 + 48)
#define Text_X_2   (Icon_X_2 + 48)
#define Text_X_3   (Icon_X_3 + 48)
#define Text_Y_1   (Icon_Y_1 + 100)
#define Text_Y_2   (Icon_Y_2 + 100)
#define Text_Y_3   (Icon_Y_3 + 100)
#define Text_Y_4   (Icon_Y_4 + 100)


// Define the data cache area of the e-ink screen
uint8_t *Image_Mono;

// Main menu content
const char *home_page[HOME_PAGE_NUM] = {"阅读","对话","待办","番茄钟","音频","日历","时钟","图片","闹钟","天气","文件","设置"};

static const int kHomeBoxX[HOME_PAGE_SIZE] = {10, 170, 330, 10, 170, 330, 10, 170, 330, 10, 170, 330};
static const int kHomeBoxY[HOME_PAGE_SIZE] = {57, 57, 57, 243, 243, 243, 429, 429, 429, 615, 615, 615};
static const int kHomeIconX[HOME_PAGE_SIZE] = {Icon_X_1, Icon_X_2, Icon_X_3, Icon_X_1, Icon_X_2, Icon_X_3, Icon_X_1, Icon_X_2, Icon_X_3, Icon_X_1, Icon_X_2, Icon_X_3};
static const int kHomeIconY[HOME_PAGE_SIZE] = {Icon_Y_1, Icon_Y_1, Icon_Y_1, Icon_Y_2, Icon_Y_2, Icon_Y_2, Icon_Y_3, Icon_Y_3, Icon_Y_3, Icon_Y_4, Icon_Y_4, Icon_Y_4};
static const int kHomeTextX[HOME_PAGE_SIZE] = {Text_X_1, Text_X_2, Text_X_3, Text_X_1, Text_X_2, Text_X_3, Text_X_1, Text_X_2, Text_X_3, Text_X_1, Text_X_2, Text_X_3};
static const int kHomeTextY[HOME_PAGE_SIZE] = {Text_Y_1, Text_Y_1, Text_Y_1, Text_Y_2, Text_Y_2, Text_Y_2, Text_Y_3, Text_Y_3, Text_Y_3, Text_Y_4, Text_Y_4, Text_Y_4};

#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
static const unsigned char* kHomeIcons[HOME_PAGE_NUM] = {
    gImage_read,
    gImage_chat,
    gImage_calendar,
    gImage_pomodoro,
    gImage_audio_file,
    gImage_calendar,
    gImage_clock,
    gImage_picture_home,
    gImage_alarm,
    gImage_weather,
    gImage_file,
    gImage_network
};
static const int kHomeIconWidth[HOME_PAGE_NUM] = {96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96};
static const int kHomeIconHeight[HOME_PAGE_NUM] = {96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96};
#elif defined(CONFIG_IMG_SOURCE_TFCARD)
static const char* kHomeIconPaths[HOME_PAGE_NUM] = {
    BMP_READ_PATH,
    BMP_READ_PATH,
    BMP_CALENDAR_PATH,
    BMP_CLOCK_PATH,
    BMP_AUDIO_FILE_PATH,
    BMP_CALENDAR_PATH,
    BMP_CLOCK_PATH,
    BMP_READ_PATH,
    BMP_ALARM_PATH,
    BMP_WEATHER_PATH,
    BMP_FILE_PATH,
    BMP_NETWORK_PATH
};
static const int kHomeIconWidth[HOME_PAGE_NUM] = {96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96};
static const int kHomeIconHeight[HOME_PAGE_NUM] = {96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96};
#endif

// Log tag
static const char *TAG = "main";
static const bool kUsbAutoSharePromptEnabled = false;
static const int kDialogueIconBoxSize = 32;
static const int kDialogueMicIconSize = 32;

static void draw_status_icon_clock(int x, int y)
{
    Paint_DrawLine(x + 6, y + 4, x + 6, y + 15, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 6, y + 4, x + 10, y + 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 10, y + 2, x + 14, y + 3, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 14, y + 3, x + 16, y + 7, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 16, y + 7, x + 16, y + 11, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 16, y + 11, x + 14, y + 15, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 14, y + 15, x + 10, y + 17, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 10, y + 17, x + 7, y + 16, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 11, y + 10, x + 15, y + 12, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 13, y + 15, x + 15, y + 18, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
}

static void draw_status_icon_listening(int x, int y)
{
#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
    Paint_ReadBmp(gImage_MIC, x + (kDialogueIconBoxSize - kDialogueMicIconSize) / 2, y, 32, 32);
#else
    draw_status_icon_speaking(x, y);
#endif
}

static void draw_status_icon_speaking(int x, int y)
{
#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
    Paint_ReadBmp(gImage_MIC, x + (kDialogueIconBoxSize - kDialogueMicIconSize) / 2, y, 32, 32);
#else
    Paint_DrawLine(x + 8, y + 3, x + 12, y + 3, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 7, y + 4, x + 7, y + 11, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 13, y + 4, x + 13, y + 11, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 8, y + 12, x + 12, y + 12, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawPoint(x + 7, y + 3, BLACK, DOT_PIXEL_1X1, DOT_FILL_AROUND);
    Paint_DrawPoint(x + 13, y + 3, BLACK, DOT_PIXEL_1X1, DOT_FILL_AROUND);
    Paint_DrawPoint(x + 7, y + 12, BLACK, DOT_PIXEL_1X1, DOT_FILL_AROUND);
    Paint_DrawPoint(x + 13, y + 12, BLACK, DOT_PIXEL_1X1, DOT_FILL_AROUND);
    Paint_DrawLine(x + 10, y + 13, x + 10, y + 17, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 7, y + 17, x + 13, y + 17, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 5, y + 8, x + 5, y + 11, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 15, y + 8, x + 15, y + 11, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
#endif
}

static void draw_status_icon_muted(int x, int y)
{
#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
    ESP_LOGI("dialogue_icon", "draw_status_icon_muted: embedded muted icon gImage_LISTENING_MUTED at (%d,%d)", x, y);
    Paint_ReadBmp(gImage_LISTENING_MUTED, x, y, 32, 32);
#else
    ESP_LOGI("dialogue_icon", "draw_status_icon_muted: fallback line icon at (%d,%d)", x, y);
    draw_status_icon_clock(x, y);
    Paint_DrawLine(x + 4, y + 4, x + 17, y + 18, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
#endif
}

static void draw_status_icon_idle(int x, int y)
{
#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
    Paint_ReadBmp(gImage_LISTENING, x, y, 32, 32);
#else
    draw_status_icon_clock(x, y);
#endif
}

static void draw_dialogue_status_icon(int x, int y)
{
    auto& app = Application::GetInstance();
    auto codec = Board::GetInstance().GetAudioCodec();
    const bool muted = app.IsUiMuted();
    const DeviceState state = app.GetDeviceState();

    Paint_ClearWindows(x, y, x + kDialogueIconBoxSize, y + kDialogueIconBoxSize, WHITE);

    ESP_LOGI("dialogue_icon",
             "draw_dialogue_status_icon: ui_muted=%d device_state=%d volume=%d pos=(%d,%d)",
             muted,
             static_cast<int>(state),
             codec != nullptr ? codec->output_volume() : -1,
             x,
             y);
    
    if (muted) {
        draw_status_icon_muted(x, y);
        ESP_LOGI("dialogue_icon", "draw_dialogue_status_icon: JINGYIN");
        return;
    }

    switch (state) {
        case kDeviceStateListening:
        case kDeviceStateSpeaking:
            ESP_LOGI("dialogue_icon", "draw_dialogue_status_icon: drawing speaking/mic icon");
            draw_status_icon_speaking(x, y);
            break;
        case kDeviceStateIdle:
        default:
            ESP_LOGI("dialogue_icon", "draw_dialogue_status_icon: drawing idle/ear icon");
            draw_status_icon_idle(x, y);
            break;
    }
}


void draw_selection_box_old(int selection)
{
    // Define the position and size of each menu item
    const int box_width = 140;
    const int box_height = 160;

    if (selection >= 0 && selection < HOME_PAGE_SIZE) {
        int x = kHomeBoxX[selection];  // The frame is slightly larger
        int y = kHomeBoxY[selection];
        
        Paint_DrawRectangle(x, y, x + box_width, y + box_height, WHITE, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    }
}

void draw_selection_box(int selection)
{
    // Define the position and size of each menu item
    const int box_width = 140;
    const int box_height = 160;

    if (selection >= 0 && selection < HOME_PAGE_SIZE) {
        int x = kHomeBoxX[selection];
        int y = kHomeBoxY[selection];
        
        Paint_DrawRectangle(x, y, x + box_width, y + box_height, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    }
}

// The main page displays
void esp_home(int selection, int Refresh_mode)
{
#if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
    photo_painter_lvgl_home_show(selection, Refresh_mode);
    return;
#endif

    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);
    uint16_t x_or = 0;
    char Time_str[16]={0};
    int BAT_Power;
    char BAT_Power_str[16]={0};

    // Draw the top state
    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    Time_data rtc_time = PCF85063_GetTime();
    xSemaphoreGive(rtc_mutex);
    
    snprintf(Time_str, sizeof(Time_str), "%02d:%02d", rtc_time.hours, rtc_time.minutes);
    Paint_DrawString_EN(20, 11, Time_str, &Font16, WHITE, BLACK);
    draw_dialogue_status_icon(292, 10);
#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
    DrawWifiStatusIcon(326, 8);
    Paint_ReadBmp(gImage_BAT, 370, 17, 32, 16);
#elif defined(CONFIG_IMG_SOURCE_TFCARD)
    DrawWifiStatusIcon(326, 8);
    GUI_ReadBmp(BMP_BAT_PATH, 370, 17);
#endif
    BAT_Power = get_battery_power();
    ESP_LOGI("BAT_Power", "BAT_Power = %d%%",BAT_Power);
    snprintf(BAT_Power_str, sizeof(BAT_Power_str), "%d%%", BAT_Power);
    if(BAT_Power == -1) BAT_Power = 20;
    else BAT_Power = BAT_Power * 20 / 100;
    Paint_DrawString_EN(411, 11, BAT_Power_str, &Font16, WHITE, BLACK);
    Paint_DrawRectangle(375, 22, 395, 30, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawRectangle(375, 22, 375+BAT_Power, 30, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawLine(2, 54, EPD_HEIGHT-2, 54, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);

    int page_index = selection / HOME_PAGE_SIZE;
    int selection_in_page = selection % HOME_PAGE_SIZE;

    // Use the built-in image or the TF card image
    for (int slot = 0; slot < HOME_PAGE_SIZE; ++slot) {
        int item_index = page_index * HOME_PAGE_SIZE + slot;
        if (item_index >= HOME_PAGE_NUM) {
            continue;
        }
        int icon_width = kHomeIconWidth[item_index];
        int icon_height = kHomeIconHeight[item_index];
        int icon_x = kHomeIconX[slot] + (96 - icon_width) / 2;
        int icon_y = kHomeIconY[slot] + (96 - icon_height) / 2;
#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
        Paint_ReadBmp(kHomeIcons[item_index], icon_x, icon_y, icon_width, icon_height);
#elif defined(CONFIG_IMG_SOURCE_TFCARD)
        GUI_ReadBmp(kHomeIconPaths[item_index], icon_x, icon_y);
#endif
        x_or = reassignCoordinates_CH(kHomeTextX[slot], home_page[item_index], &Font16_UTF8);
        Paint_DrawString_CN(x_or, kHomeTextY[slot], home_page[item_index], &Font16_UTF8, WHITE, BLACK);
    }

    // Draw a selection box based on the selected items (within the current page)
    draw_selection_box(selection_in_page);

    if (Refresh_mode == Global_refresh) {
        EPD_Display_Base(Image_Mono);
    } else if(Refresh_mode == Partial_refresh){
        EPD_Display_Partial(Image_Mono,0,0,EPD_WIDTH,EPD_HEIGHT);
    }
}

// On the main page, select "Process" at the top or bottom
void Page_Down_home(int selection, int Refresh_mode)
{
#if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
    photo_painter_lvgl_home_move_selection(selection, Refresh_mode);
    return;
#endif

    int selection_old = selection - 1 ;
    if((selection-1) < 0 ) selection_old = HOME_PAGE_SIZE - 1;
    draw_selection_box_old(selection_old);
    draw_selection_box(selection);

    if (Refresh_mode == Global_refresh) {
        EPD_Display_Base(Image_Mono);
    } else if(Refresh_mode == Partial_refresh){
        EPD_Display_Partial(Image_Mono,0,0,EPD_WIDTH,EPD_HEIGHT);
    }
}
void Page_Up_home(int selection, int Refresh_mode)
{
#if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
    photo_painter_lvgl_home_move_selection(selection, Refresh_mode);
    return;
#endif

    int selection_old = selection + 1 ;
    if((selection + 1) >= HOME_PAGE_SIZE ) selection_old = 0;
    draw_selection_box_old(selection_old);
    draw_selection_box(selection);

    if (Refresh_mode == Global_refresh) {
        EPD_Display_Base(Image_Mono);
    } else if(Refresh_mode == Partial_refresh){
        EPD_Display_Partial(Image_Mono,0,0,EPD_WIDTH,EPD_HEIGHT);
    }
}

// Display time and battery level
void display_home_time_img(Time_data rtc_time)
{
#if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
    photo_painter_lvgl_home_update_status(rtc_time);
    return;
#endif

    char Time_str[16]={0};
    int BAT_Power;
    char BAT_Power_str[16]={0};

    snprintf(Time_str, sizeof(Time_str), "%02d:%02d", rtc_time.hours, rtc_time.minutes);
    Paint_DrawString_EN(20, 11, Time_str, &Font16, WHITE, BLACK);
    draw_dialogue_status_icon(292, 10);

// Use the built-in image or the TF card image
#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
    DrawWifiStatusIcon(326, 8);
    Paint_ReadBmp(gImage_BAT, 370, 17, 32, 16);
#elif defined(CONFIG_IMG_SOURCE_TFCARD)
    DrawWifiStatusIcon(326, 8);
    GUI_ReadBmp(BMP_BAT_PATH, 370, 17);
#endif
    BAT_Power = get_battery_power();
    ESP_LOGI("BAT_Power", "BAT_Power = %d%%",BAT_Power);
    snprintf(BAT_Power_str, sizeof(BAT_Power_str), "%d%%", BAT_Power);
    if(BAT_Power == -1) BAT_Power = 20;
    else BAT_Power = BAT_Power * 20 / 100;
    Paint_DrawString_EN(411, 11, BAT_Power_str, &Font16, WHITE, BLACK);
    Paint_DrawRectangle(375, 22, 395, 30, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawRectangle(375, 22, 375+BAT_Power, 30, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    EPD_Display_Partial(Image_Mono,0,0,EPD_WIDTH,EPD_HEIGHT);
}
static void display_home_time_img_last(Time_data rtc_time)
{
    char Time_str[20]={0};
    int hours = rtc_time.hours;
    int minutes = rtc_time.minutes-1;

    if(minutes < 0){
        minutes = 59;
        hours = rtc_time.hours - 1;
        if(hours < 0 )
            hours = 23;
    }

    snprintf(Time_str, sizeof(Time_str), "%02d:%02d", hours, minutes);
    Paint_DrawString_EN(20, 11, Time_str, &Font16, WHITE, BLACK);

    EPD_Display_Partial(Image_Mono,0,0,EPD_WIDTH,EPD_HEIGHT);
}

static void draw_usb_dialog(int share_selected)
{
    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);

    Paint_DrawString_CN(88, 220, " 检测到 Type-C 连接 ", &Font16_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(82, 260, " 是否共享 SD 卡给电脑 ", &Font16_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(82, 300, " 上下切换，功能键确认 ", &Font16_UTF8, WHITE, BLACK);

    const int y1 = 380;
    const int y2 = 440;
    const int share_x1 = 70;
    const int share_x2 = 190;
    const int cancel_x1 = 230;
    const int cancel_x2 = 350;

    Paint_DrawRectangle(share_x1, y1, share_x2, y2,
                        share_selected ? BLACK : BLACK, DOT_PIXEL_2X2,
                        share_selected ? DRAW_FILL_FULL : DRAW_FILL_EMPTY);
    Paint_DrawRectangle(cancel_x1, y1, cancel_x2, y2,
                        share_selected ? BLACK : BLACK, DOT_PIXEL_2X2,
                        share_selected ? DRAW_FILL_EMPTY : DRAW_FILL_FULL);

    Paint_DrawString_CN(100, 396, "共享", &Font16_UTF8,
                        share_selected ? BLACK : WHITE,
                        share_selected ? WHITE : BLACK);
    Paint_DrawString_CN(260, 396, "取消", &Font16_UTF8,
                        share_selected ? WHITE : BLACK,
                        share_selected ? BLACK : WHITE);

    EPD_Display_Base(Image_Mono);
}

static void draw_usb_status_screen(const char* line1, const char* line2, const char* line3)
{
    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);

    Paint_DrawString_CN(100, 230, line1, &Font16_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(58, 280, line2, &Font16_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(58, 330, line3, &Font16_UTF8, WHITE, BLACK);

    EPD_Display_Base(Image_Mono);
}

static int wait_usb_share_decision(void)
{
    int share_selected = 1;

    EPD_Init();
    draw_usb_dialog(share_selected);

    while (get_usb_connected()) {
        int button = wait_key_event_and_return_code(pdMS_TO_TICKS(500));
        if (button == 0 || button == 14) {
            share_selected = !share_selected;
            draw_usb_dialog(share_selected);
        } else if (button == 7) {
            return share_selected;
        } else if (button == 22) {
            return 0;
        }
    }

    return 0;
}

static void run_usb_sdcard_share_session(void)
{
    esp_err_t ret = sdcard_unmount();
    if (ret != ESP_OK) {
        draw_usb_status_screen("SD卡切换失败", "无法进入 USB 共享模式", "请稍后重试");
        vTaskDelay(pdMS_TO_TICKS(1200));
        return;
    }

    ret = usb_msc_start_for_sdcard();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB MSC start failed: %s", esp_err_to_name(ret));
        sdcard_remount();
        draw_usb_status_screen("USB共享启动失败", "SD卡已经恢复本地挂载", "请检查配置后重试");
        vTaskDelay(pdMS_TO_TICKS(1200));
        return;
    }

    bool host_using = usb_msc_is_host_using_storage();
    bool host_ejected = false;
    draw_usb_status_screen("USB共享已开启",
                           host_using ? "电脑正在访问 SD 卡" : "等待电脑访问 SD 卡",
                           "弹出或拔出后自动恢复本地模式");

    while (get_usb_connected()) {
        bool new_host_using = usb_msc_is_host_using_storage();
        if (!new_host_using) {
            host_ejected = true;
            draw_usb_status_screen("电脑已弹出设备", "正在恢复本地 SD 卡读写", "请稍候...");
            break;
        }

        if (new_host_using != host_using) {
            host_using = new_host_using;
            draw_usb_status_screen("USB共享已开启",
                                   host_using ? "电脑正在访问 SD 卡" : "等待电脑访问 SD 卡",
                                   "弹出或拔出后自动恢复本地模式");
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    ret = usb_msc_stop_for_sdcard();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB MSC stop failed: %s", esp_err_to_name(ret));
    }

    ret = sdcard_remount();
    if (ret != ESP_OK) {
        draw_usb_status_screen("SD卡恢复失败", "请重启设备后重试", " ");
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }

    draw_usb_status_screen(host_ejected ? "已恢复本地模式" : "USB已断开",
                           "SD卡已恢复本地读写",
                           host_ejected ? "Type-C 仍连接时可继续充电" : " ");
    vTaskDelay(pdMS_TO_TICKS(900));
}


// Main menu task
int home_selection = 0; // Current main menu options (global for MCP control)
static TaskHandle_t g_user_task_handle = NULL;

extern "C" TaskHandle_t get_user_task_handle(void)
{
    return g_user_task_handle;
}

void user_Task(void *arg)
{
    int button = -1;        // Key status

    int time_count = 0;
    Time_data rtc_time = {0};
    int last_minutes = -1;
    bool usb_prompt_done = false;
    DeviceState last_device_state = kDeviceStateUnknown;
    bool last_muted = false;
    
    ESP_LOGI("home", "user_Task started");
    esp_home(home_selection, Global_refresh);
    ESP_LOGI("home", "Initial home refresh completed; starting key processing");
    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    rtc_time = PCF85063_GetTime();
    xSemaphoreGive(rtc_mutex);
    last_minutes = rtc_time.minutes;
    last_device_state = Application::GetInstance().GetDeviceState();
    if (auto codec = Board::GetInstance().GetAudioCodec(); codec != nullptr) {
        last_muted = Application::GetInstance().IsUiMuted();
    }

    while (1)
    {
        if (kUsbAutoSharePromptEnabled) {
            bool usb_connected = get_usb_connected();
            if (!usb_connected) {
                usb_prompt_done = false;
            } else if (!usb_prompt_done) {
                int allow_share = wait_usb_share_decision();
                usb_prompt_done = true;
                time_count = 0;

                if (allow_share == 1 && get_usb_connected()) {
                    run_usb_sdcard_share_session();
                }

                EPD_Init();
                esp_home(home_selection, Global_refresh);
                continue;
            }
        }

        // // The log loop prints the main menu
        // for (int i = 0; i < HOME_PAGE_NUM; ++i) {
        //     ESP_LOGI("home", "%s%s", home_page[i], (i == home_selection) ? " <" : "");
        // }

        button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        if (button >= 0) ESP_LOGI("home", "Received key=%d selection=%d", button, home_selection);
        time_count++;

        bool usb_wakeup_requested = false;
        if((time_count > EPD_Sleep_Time)) {
            ESP_LOGI("home", "EPD_Sleep");
            EPD_Sleep();
            while(1)
            {
                button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
                if (get_usb_connected()) {
                    ESP_LOGI("home", "EPD_Init");
                    EPD_Init();
                    time_count = 0;
                    usb_wakeup_requested = true;
                    break;
                }
                if (button == 12){
                    ESP_LOGI("home", "EPD_Init");
                    EPD_Init();
                    time_count = 0;
                    break;
                } else if (button == 8 || button == 22 || button == 14 || button == 0 || button == 7 || button == 23){
                    ESP_LOGI("home", "EPD_Init");
                    EPD_Init();
#if !(CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint || CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2)
                    EPD_Display_Partial(Image_Mono,0,0,EPD_WIDTH,EPD_HEIGHT);
#endif
                    time_count = 0;
                    break;
                }
                xSemaphoreTake(rtc_mutex, portMAX_DELAY);
                rtc_time = PCF85063_GetTime();
                xSemaphoreGive(rtc_mutex);
                DeviceState current_state = Application::GetInstance().GetDeviceState();
                bool current_muted = false;
                if (auto codec = Board::GetInstance().GetAudioCodec(); codec != nullptr) {
                    current_muted = Application::GetInstance().IsUiMuted();
                }
                // Refresh the time and battery level once every minute
                if(rtc_time.minutes != last_minutes) {
                    last_minutes = rtc_time.minutes;
                    ESP_LOGI("home", "EPD_Init");
                    EPD_Init();
                    // display_home_time_img_last(rtc_time);
#if !(CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint || CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2)
                    EPD_Display_Partial(Image_Mono,0,0,EPD_WIDTH,EPD_HEIGHT);
#endif
                    display_home_time_img(rtc_time);
                    ESP_LOGI("home", "EPD_Sleep");
                    EPD_Sleep();
                    // sleep_js++;
                    // if(sleep_js > Unattended_Time){
                    //     ESP_LOGI("home", "power off");
                    //     axp_pwr_off();
                    // } 
                } else if (current_state != last_device_state || current_muted != last_muted) {
                    last_device_state = current_state;
                    last_muted = current_muted;
                    ESP_LOGI("home", "EPD_Init due to dialogue state change");
                    EPD_Init();
#if !(CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint || CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2)
                    EPD_Display_Partial(Image_Mono,0,0,EPD_WIDTH,EPD_HEIGHT);
#endif
                    display_home_time_img(rtc_time);
                    ESP_LOGI("home", "EPD_Sleep");
                    EPD_Sleep();
                }
            }
        }
        if (usb_wakeup_requested) {
            continue;
        }

        if (button == 14) {
            // The next main course item
            int prev_page = home_selection / HOME_PAGE_SIZE;
            home_selection++;
            if (home_selection >= HOME_PAGE_NUM) home_selection = 0;
            int new_page = home_selection / HOME_PAGE_SIZE;
            ESP_LOGI("home", "Selection moved down to %d", home_selection);
            if (new_page != prev_page) {
                esp_home(home_selection, Global_refresh);
            } else {
                Page_Down_home(home_selection % HOME_PAGE_SIZE, Partial_refresh);
            }
            time_count = 0;
        } else if (button == 0) {
            // The previous main course item
            int prev_page = home_selection / HOME_PAGE_SIZE;
            home_selection--;
            if (home_selection < 0) home_selection = HOME_PAGE_NUM - 1;
            int new_page = home_selection / HOME_PAGE_SIZE;
            ESP_LOGI("home", "Selection moved up to %d", home_selection);
            if (new_page != prev_page) {
                esp_home(home_selection, Global_refresh);
            } else {
                Page_Up_home(home_selection % HOME_PAGE_SIZE, Partial_refresh);
            }
            time_count = 0;
        } else if (button == 7) {
            // Enter the sub-menu
            ESP_LOGI("home", "Entering page selection=%d", home_selection);
            if (home_selection == 0) {
                page_fiction_file();
            } else if (home_selection == 1) {
                page_chat_show();
            } else if (home_selection == 2) {
                page_todolist_show();
            } else if (home_selection == 3) {
                page_pomodoro_timer_show();
            } else if (home_selection == 4) {
                page_audio_main();
            } else if (home_selection == 5) {
                page_calendar_show();
            } else if (home_selection == 6) {
                page_clock_show();
            } else if (home_selection == 7) {
                page_picture_show();
            } else if (home_selection == 8) {
                page_alarm_menu();
            } else if (home_selection == 9) {
                page_weather_city_select();
            } else if (home_selection == 10) {
                file_browser_task();
            } else if (home_selection == 11) {
                page_settings_show();
            } else {
                
                // ESP_LOGI("home", "entry page: %s", (home_selection < HOME_PAGE_NUM && home_selection >= 0) ? home_page[home_selection] : "未知");
                // ESP_LOGI("home", "entry page: %s", home_page[home_selection]);
                // Other pages can be expanded here
            }
            vTaskDelay(pdMS_TO_TICKS(50)); 
            esp_home(home_selection, Partial_refresh);
            time_count = 0;
        } else if (button == 12) {
            ESP_LOGI("home", "Global_refresh");
            EPD_Init();
            esp_home(home_selection, Global_refresh);
            time_count = 0;
        }  else if (button == 22) {
            ESP_LOGI("home", "power off");
            Paint_DrawString_CN(180, 11, " 已关机 ", &Font16_UTF8, WHITE, BLACK);
            EPD_Display_Partial(Image_Mono,0,0,EPD_WIDTH,EPD_HEIGHT);
            axp_pwr_off();
        } else if (button == 23) {
            ESP_LOGI("home", "settings");
            page_settings_show();
            esp_home(home_selection, Partial_refresh);
        }

        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        rtc_time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
        DeviceState current_state = Application::GetInstance().GetDeviceState();
        bool current_muted = false;
        if (auto codec = Board::GetInstance().GetAudioCodec(); codec != nullptr) {
            current_muted = Application::GetInstance().IsUiMuted();
        }
        // Refresh the time and battery level once every minute
        if(rtc_time.minutes != last_minutes) {
            last_minutes = rtc_time.minutes;
            display_home_time_img(rtc_time);
#if !(CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint || CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2)
            EPD_Display_Partial(Image_Mono,0,0,EPD_WIDTH,EPD_HEIGHT);
#endif
        } else if (current_state != last_device_state || current_muted != last_muted) {
            last_device_state = current_state;
            last_muted = current_muted;
            display_home_time_img(rtc_time);
#if !(CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint || CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2)
            EPD_Display_Partial(Image_Mono,0,0,EPD_WIDTH,EPD_HEIGHT);
#endif
        }
    }
}


// Add threads that work in modes such as clock, weather, and calendar
static void clock_mode_task(void *arg)
{
    page_clock_show_mode();
    vTaskDelete(NULL);
}
static void calendar_mode_task(void *arg)
{
    page_calendar_show_mode();
    vTaskDelete(NULL);
}
static void weather_mode_task(void *arg)
{
    weather_city_select_mode();
    vTaskDelete(NULL);
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGE("EVEN","Hello world!\n");

    // Initialize the SD card
#if CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
    sdcard_config_t sdcard_cfg = {
        .clk = SDMMC_CLK_PIN,
        .cmd = SDMMC_CMD_PIN,
        .d0 = SDMMC_D0_PIN,
        .d1 = GPIO_NUM_NC,
        .d2 = GPIO_NUM_NC,
        .d3 = GPIO_NUM_NC,
        .width = 1,
    };
    sdcard_init_with_config(&sdcard_cfg);

    // The schematic has one shared I2C bus only. Do not touch GPIO41/42:
    // those pins are CAMERA_HREF/CAMERA_VSYNC.
#if 0
    // Legacy recovery experiment retained for reference only. Driving a shared
    // bus push-pull is not valid for the V1.0 schematic and can damage slaves.
    {
        // Phase 1: Check initial state with pull-ups enabled
        gpio_config_t io_cfg_input = {
            .pin_bit_mask = (1ULL << 17) | (1ULL << 18),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_cfg_input);
        vTaskDelay(pdMS_TO_TICKS(20));
        int sda = gpio_get_level(GPIO_NUM_17);
        int scl = gpio_get_level(GPIO_NUM_18);
        ESP_LOGI(TAG, "AUDIO_I2C pre-recovery (input+pullup): GPIO17=%d GPIO18=%d", sda, scl);

        if (sda == 0 || scl == 0) {
            // Phase 2: Aggressive recovery with open-drain + pull-ups
            gpio_config_t io_cfg_od = {
                .pin_bit_mask = (1ULL << 17) | (1ULL << 18),
                .mode = GPIO_MODE_INPUT_OUTPUT_OD,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            gpio_config(&io_cfg_od);

            // SCL has a capacitor to GND, so rise times are slow.
            // With 45kΩ pull-up, need ~200μs for SCL to rise (τ=45kΩ×C).
            const int scl_rise_us = 200;

            for (int attempt = 0; attempt < 5; attempt++) {
                if (attempt > 0) {
                    vTaskDelay(pdMS_TO_TICKS(100 * attempt));
                }

                // Release both lines, wait for capacitor to charge
                gpio_set_level(GPIO_NUM_17, 1);
                gpio_set_level(GPIO_NUM_18, 1);
                esp_rom_delay_us(scl_rise_us);

                // Generate START condition (SDA high→low while SCL high)
                gpio_set_level(GPIO_NUM_17, 0);
                esp_rom_delay_us(50);

                // Clock SCL up to 128 times, checking SDA each cycle
                for (int i = 0; i < 128; i++) {
                    gpio_set_level(GPIO_NUM_18, 0);
                    esp_rom_delay_us(50);
                    gpio_set_level(GPIO_NUM_18, 1);
                    esp_rom_delay_us(scl_rise_us);  // wait for SCL to actually rise
                    if (gpio_get_level(GPIO_NUM_17) == 1) {
                        ESP_LOGI(TAG, "SDA released after %d SCL pulses (attempt %d)", i + 1, attempt + 1);
                        break;
                    }
                }

                // Generate STOP condition (SDA low→high while SCL high)
                gpio_set_level(GPIO_NUM_18, 0);
                esp_rom_delay_us(50);
                gpio_set_level(GPIO_NUM_17, 0);
                esp_rom_delay_us(50);
                gpio_set_level(GPIO_NUM_18, 1);
                esp_rom_delay_us(scl_rise_us);  // wait for SCL to rise
                gpio_set_level(GPIO_NUM_17, 1);
                esp_rom_delay_us(scl_rise_us);  // wait for SDA to rise

                sda = gpio_get_level(GPIO_NUM_17);
                scl = gpio_get_level(GPIO_NUM_18);
                ESP_LOGI(TAG, "After recovery attempt %d: GPIO17=%d GPIO18=%d", attempt + 1, sda, scl);

                if (sda == 1 && scl == 1) {
                    ESP_LOGI(TAG, "AUDIO_I2C bus recovered after attempt %d", attempt + 1);
                    break;
                }

                // Push-pull blast on SDA for stubborn slaves
                if (attempt >= 2 && sda == 0) {
                    gpio_config_t io_cfg_pp = {
                        .pin_bit_mask = (1ULL << 17),
                        .mode = GPIO_MODE_OUTPUT,
                        .pull_up_en = GPIO_PULLUP_DISABLE,
                        .pull_down_en = GPIO_PULLDOWN_DISABLE,
                        .intr_type = GPIO_INTR_DISABLE,
                    };
                    gpio_config(&io_cfg_pp);
                    gpio_set_level(GPIO_NUM_17, 1);
                    esp_rom_delay_us(scl_rise_us);
                    gpio_config(&io_cfg_od);  // back to open-drain
                    gpio_set_level(GPIO_NUM_17, 1);
                    esp_rom_delay_us(scl_rise_us);
                    ESP_LOGI(TAG, "After push-pull blast: GPIO17=%d", gpio_get_level(GPIO_NUM_17));
                }
            }
        }
    }
#endif
    i2c_master_init_custom(AUDIO_I2C_SDA_PIN, AUDIO_I2C_SCL_PIN);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "Shared I2C idle levels: SDA(GPIO17)=%d SCL(GPIO18)=%d",
             gpio_get_level(AUDIO_I2C_SDA_PIN), gpio_get_level(AUDIO_I2C_SCL_PIN));

#if 0
    ESP_LOGI(TAG, "AUDIO_I2C scan levels: GPIO17=%d GPIO18=%d",
             gpio_get_level(GPIO_NUM_17), gpio_get_level(GPIO_NUM_18));
    ESP_LOGI(TAG, "=== Scanning AUDIO_I2C (GP17/GP18) full range at %d Hz ===", I2C_MASTER_FREQ_HZ);
    int audio_found_count = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        i2c_master_dev_handle_t scan_dev_handle = NULL;
        esp_err_t r = i2c_bus_add_device(addr, I2C_MASTER_FREQ_HZ, &scan_dev_handle);
        uint8_t scan_byte = 0;
        if (r == ESP_OK) {
            r = i2c_master_receive(scan_dev_handle, &scan_byte, 1, pdMS_TO_TICKS(100));
            esp_err_t rm_ret = i2c_master_bus_rm_device(scan_dev_handle);
            if (rm_ret != ESP_OK) {
                ESP_LOGW(TAG, "  AUDIO_I2C scan remove failed at 0x%02X: %s", addr, esp_err_to_name(rm_ret));
            }
        }
        if (r == ESP_OK) {
            ESP_LOGI(TAG, "  AUDIO_I2C found at 0x%02X", addr);
            audio_found_count++;
        } else if (r == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "  AUDIO_I2C probe timeout at 0x%02X", addr);
        } else if (addr >= 0x40 && addr <= 0x43) {
            ESP_LOGW(TAG, "  ES7210 candidate no ACK at 0x%02X: %s", addr, esp_err_to_name(r));
        }
    }
    ESP_LOGI(TAG, "=== AUDIO_I2C scan complete: %d device(s) found ===", audio_found_count);
#endif

    // AW9523 is supplied by 3V3. On this assembly EN_Power is strapped high;
    // the AW9523 driver keeps P1.7 released instead of driving that strap.
    esp_err_t epaper_ret = epaper_port_init();
    if (epaper_ret != ESP_OK) {
        ESP_LOGE(TAG, "AW9523/e-paper startup failed; checking remaining I2C devices");
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_ERROR_CHECK(i2c_bus_add_device(SHTC3Addr, I2C_MASTER_FREQ_HZ,
                                       &shtc3_dev_handle));
    ESP_ERROR_CHECK(i2c_bus_add_device(QMI8658_SLAVE_ADDR_L, I2C_MASTER_FREQ_HZ,
                                       &qmi8658_dev_handle));

    const uint8_t schematic_i2c_addresses[] = {
        AW9523_I2C_ADDR, SHTC3Addr, QMI8658_SLAVE_ADDR_L, QMI8658_SLAVE_ADDR_H, 0x18, 0x40, 0x2e
    };
    for (uint8_t address : schematic_i2c_addresses) {
        esp_err_t probe = i2c_master_probe(i2c_bus_handle, address, 100);
        ESP_LOGI(TAG, "Schematic I2C device 0x%02X: %s", address,
                 probe == ESP_OK ? "present" : esp_err_to_name(probe));
    }
    if (epaper_ret != ESP_OK) {
        ESP_LOGE(TAG, "Fatal board hardware fault: shared I2C bus is unavailable; startup stopped");
        return;
    }
    // BoxAudioCodec asserts when either codec cannot initialize. Keep the
    // peripheral rail enabled and stop here so missing hardware can be measured
    // without an endless reboot loop. Codec configuration uses 8-bit addresses.
    const uint8_t codec_addresses[] = {
        AUDIO_CODEC_ES8311_ADDR >> 1, AUDIO_CODEC_ES7210_ADDR >> 1
    };
    for (uint8_t address : codec_addresses) {
        esp_err_t ret = i2c_master_probe(i2c_bus_handle, address, 100);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Required audio codec 0x%02X not responding: %s; startup stopped, EN_Power remains on for diagnosis",
                     address, esp_err_to_name(ret));
            return;
        }
    }
#else
    _sdcard_init();
    i2c_master_init();   // Initialize the I2C bus
#endif
    vTaskDelay(pdMS_TO_TICKS(50));
#if !CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
    i2c_devices_init();  // Initialize all I2C devices
#endif
    vTaskDelay(pdMS_TO_TICKS(50));

    spiffs_init();

    // Initialize the RTC
#if !CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
    PCF85063_init();
#endif
    // PCF85063_alarm_Time_Disable();
    // PCF85063_clear_alarm_flag();

    // Only legacy boards contain AXP2101. V2 uses TP4056 and discrete rails.
#if !CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
    axp_init();
#endif
    char mode = load_mode_enable_from_nvs();
#if !CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
    if(RTC_INT && mode!=0)
    {
        // Determine whether the time is up or if the USB power supply is connected
        if(get_usb_connected()){
            vTaskDelay(pdMS_TO_TICKS(100));
            if((!PWR_OUT))
            {
                ESP_LOGE(TAG,"USB connection, but the device is shut down when the mode is not set to 0");
                Paint_DrawString_CN(180, 11, " 已关机 ", &Font16_UTF8, WHITE, BLACK);
                EPD_Display_Partial(Image_Mono,0,0,EPD_WIDTH,EPD_HEIGHT);
                axp_pwr_off();
            } else {
                ESP_LOGE(TAG,"Others, continue the process");
            }
        }
        // Set the mode to 0
        ESP_LOGE(TAG,"Set the mode to 0");
        save_mode_enable_to_nvs(0);
        mode = 0;
    }
#endif

    // The program officially begins to execute
    // ESP_LOGE(TAG,"Hello world!\n");
    // Initialize key
#if !CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
    button_Init();
#endif

    // Initialize the temperature and humidity sensor
    i2c_shtc3_init();

    // Initialize the audio
    // page_audio_int();

    // E-ink screen pin initialization
#if !CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
    epaper_port_init();
#endif
    // EPD_display_BMP(NULL);
    // vTaskDelay(pdMS_TO_TICKS(500));

    // EPD_Init
    EPD_Init();
    // Create a data cache area for the e-paper
    if((Image_Mono = (UBYTE *)heap_caps_malloc(EPD_SIZE_MONO, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) == NULL) {
        ESP_LOGW(TAG, "SPIRAM allocation failed, trying internal RAM");
        Image_Mono = (UBYTE *)heap_caps_malloc(EPD_SIZE_MONO, MALLOC_CAP_8BIT);
    }
    if (Image_Mono == NULL) {
        ESP_LOGE(TAG,"Failed to apply for black memory...");
    }

    // Confirm the current mode
    ESP_LOGE(TAG,"mode = %d",mode);

    // Clear the alarm clock
#if !CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
    PCF85063_clear_alarm_flag();
#endif

    if(mode == 1) {
        // clock
        xTaskCreate(clock_mode_task, "clock_mode", 12 * 1024, NULL, USER_TASK_PRIO, NULL);
    } else if(mode == 2) {
        // calendar
        xTaskCreate(calendar_mode_task, "calendar_mode", 12 * 1024, NULL, USER_TASK_PRIO, NULL);
    } else if(mode == 3) {
        // weather
        xTaskCreate(weather_mode_task, "weather_mode", 12 * 1024, NULL, USER_TASK_PRIO, NULL);
    } else {
        // Clear the alarm clock
#if !CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
        PCF85063_alarm_Time_Disable();
        PCF85063_clear_alarm_flag();
#endif
        // QMI8658A is present on the V1.0 schematic as well as legacy boards.
        QMI8658_init();

        Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
        Paint_SetScale(2);
        Paint_SelectImage(Image_Mono);
        Paint_Clear(WHITE);

        // Initialize and run the application
        auto& app = Application::GetInstance();
        app.Initialize();
        // Read the NVS to determine whether the WiFi is enabled
        wifi_enable = load_wifi_enable_from_nvs();
        if (wifi_enable)
        {
            ESP_LOGI("network", "WiFi is enabled and the connection to WiFi begins");
            EPD_Clear();
            vTaskDelay(pdMS_TO_TICKS(500));
            page_network_init_main();
        }
        else 
        {
            ESP_LOGI("network", "Turn off WiFi");
            // Turn off WiFi
            ESP_ERROR_CHECK(safe_wifi_stop());
            ESP_ERROR_CHECK(safe_wifi_deinit());
        }

        // Initialize the mutex lock
        alarm_mutex = xSemaphoreCreateMutex();
        rtc_mutex = xSemaphoreCreateMutex();
        qmi8658_mutex = xSemaphoreCreateMutex();

        // Check whether the creation was successful
        assert(alarm_mutex != NULL);
        assert(rtc_mutex != NULL);

        // Initialize the todolist module (load CSV + start reminder task)
        page_todolist_init();



        ESP_LOGI("EVEN", "Heap before tasks: free=%u min=%u internal_free=%u internal_largest=%u spiram_free=%u",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)esp_get_minimum_free_heap_size(),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        
        // Run the main loop in a dedicated FreeRTOS task to avoid blocking app_main
        BaseType_t ret = xTaskCreate([](void* arg) {
            auto* application = static_cast<Application*>(arg);
            application->Run();
            vTaskDelete(nullptr);
        }, "main_event_loop", 8192, &app, USER_TASK_PRIO, nullptr);
        if (ret != pdPASS) {
            ESP_LOGE("EVEN", "Failed to create main_event_loop task");
        }

        // Create the main menu task
        ret = xTaskCreate(user_Task, "user_Task", 24 * 1024, NULL, USER_TASK_PRIO, &g_user_task_handle);
        if (ret != pdPASS) {
            ESP_LOGE("EVEN", "Failed to create user_Task");
        }
        // Create an alarm clock background monitoring task
        ret = xTaskCreate(alarm_task, "alarm_task", 4 * 1024, NULL, ALARM_TASK_PRIO, NULL);
        if (ret != pdPASS) {
            ESP_LOGE("EVEN", "Failed to create alarm_task");
        }

        ESP_LOGE("EVEN","-----started.\n");
    }
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }

    heap_caps_free(Image_Mono);
    fflush(stdout);
    // esp_restart();
}
