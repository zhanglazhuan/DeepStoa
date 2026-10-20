#include "page_settings.h"
#include "qmi8658_bsp.h"
#include "i2c_bsp.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"
#include "button_bsp.h"
#include "ff.h"        // FatFs API

#include "pcf85063_bsp.h"
#include "axp_prot.h"
#include "sdcard_bsp.h"
#include "epaper_port.h"
#include "GUI_BMPfile.h"
#include "GUI_Paint.h"
#include "usb_msc_manager.h"

#include "page_clock.h"
#include "page_network.h"
#include "status_bar.h"

extern bool wifi_enable;        // Is the wifi turned on?
extern sdmmc_card_t *card_host; // The global pointer when the SD card is initialized
extern SemaphoreHandle_t qmi8658_mutex;

// E-ink screen sleep time (S)
#define EPD_Sleep_Time   5
// Equipment shutdown time (minutes)
#define Unattended_Time  10

#define BMP_BAT_PATH                        "/sdcard/GUI/BAT.bmp"
#define BMP_WIFI_PATH                       "/sdcard/GUI/WIFI.bmp"

extern uint8_t *Image_Mono;
extern SemaphoreHandle_t rtc_mutex;  

static TaskHandle_t imu_task_handle = NULL;
static const char *TAG = "qmi8658";

int qmi8658_status;
bool auto_rotate = true;

#define SETTINGS_ITEM_COUNT 4

static void display_settings_init();
static void display_settings_option(int option);
static void display_settings_time_img(Time_data rtc_time);
static void Forced_refresh_settings(void);
static void Refresh_page_settings(void);
static int Sleep_wake_settings();
static void page_imu_monitor_show(void);
static void page_sdcard_mode_show(void);
static void draw_sdcard_mode_page(int option, const char *hint, bool full_refresh);
static esp_err_t enter_usb_share_mode(void);
static esp_err_t exit_usb_share_mode(void);

// A subfunction for judging the state
int check_imu_status(const float acc[3], const float gyro[3]) {
    if(acc[0] > 800){
        qmi8658_status = 1;
    } else if (acc[1] > 900) {
        qmi8658_status = 2;
    } else if(acc[0] < -800){
        qmi8658_status = 3;
    } else if (acc[1] < -900) {
        qmi8658_status = 4;
    }
    return qmi8658_status;
}

void show_sdmmc_capacity()
{
    if (card_host) {
        uint64_t card_size = (uint64_t)card_host->csd.capacity * 512;
        ESP_LOGI("settings", "Physical capacity of SD card: %llu MB", card_size / (1024 * 1024));
    } else {
        ESP_LOGI("settings", "The SD card is not initialized");
    }
}

void show_sdcard_all_info()
{
    char sdcard_str[50] = {0};
    if (card_host) {
        uint64_t sd_physical_size = (uint64_t)card_host->csd.capacity * 512;
        ESP_LOGI("settings", "Physical capacity of SD card: %llu MB", sd_physical_size / (1024 * 1024));

        snprintf(sdcard_str, sizeof(sdcard_str), " SD卡总容量: %llu MB", sd_physical_size / (1024 * 1024) );
        Paint_DrawString_CN(25, 259, sdcard_str, &Font18_UTF8, WHITE, BLACK);
    } else {
        ESP_LOGI("settings", "The SD card is not initialized");
        Paint_DrawString_CN(25, 259, " SD卡未初始化", &Font18_UTF8, WHITE, BLACK);
    }

    FATFS* fs;
    DWORD fre_clust;
    FRESULT fres = f_getfree("0:", &fre_clust, &fs);
    if (fres != FR_OK) {
        fres = f_getfree("/sdcard", &fre_clust, &fs);
    }

    if (fres == FR_OK && fs != NULL) {
        uint64_t clusters = (uint64_t)(fs->n_fatent - 2);
        uint64_t csize = (uint64_t)fs->csize;
        uint64_t tot_sect = clusters * csize; 
        uint64_t fre_sect = (uint64_t)fre_clust * csize;

        uint64_t sd_partition_total = tot_sect * 512ULL;
        uint64_t sd_partition_free  = fre_sect * 512ULL;
        uint64_t sd_partition_used  = (sd_partition_total > sd_partition_free) ? (sd_partition_total - sd_partition_free) : 0ULL;

        ESP_LOGI("PART", "clusters=%llu, csize=%llu", (unsigned long long)clusters, (unsigned long long)csize);
        ESP_LOGI("settings", "The total capacity of the SD card partition: %llu MB", (unsigned long long)(sd_partition_total / (1024ULL * 1024ULL)));
        ESP_LOGI("settings", "Remaining capacity of the SD card partition: %llu MB", (unsigned long long)(sd_partition_free / (1024ULL * 1024ULL)));
        ESP_LOGI("settings", "The used capacity of the SD card partition: %llu MB", (unsigned long long)(sd_partition_used / (1024ULL * 1024ULL)));

        snprintf(sdcard_str, sizeof(sdcard_str), " SD卡分区总容量: %llu MB", (unsigned long long)(sd_partition_total / (1024ULL * 1024ULL)) );
        Paint_DrawString_CN(25, 295, sdcard_str, &Font18_UTF8, WHITE, BLACK);

        snprintf(sdcard_str, sizeof(sdcard_str), " SD卡分区已用容量: %llu MB", (unsigned long long)(sd_partition_used / (1024ULL * 1024ULL)) );
        Paint_DrawString_CN(25, 331, sdcard_str, &Font18_UTF8, WHITE, BLACK);

        snprintf(sdcard_str, sizeof(sdcard_str), " SD卡分区剩余容量: %llu MB", (unsigned long long)(sd_partition_free / (1024ULL * 1024ULL)) );
        Paint_DrawString_CN(25, 367, sdcard_str, &Font18_UTF8, WHITE, BLACK);

    } else {
        ESP_LOGI("settings", "SD card partition information acquisition failed (f_getfree returns %d)", fres);
        Paint_DrawString_CN(25, 295, " SD卡分区信息获取失败 ", &Font18_UTF8, WHITE, BLACK);
    }
}

// Obtain the capacity information of the SD card
esp_err_t get_sdcard_info(uint64_t* total, uint64_t* free)
{
    if (!total || !free) return ESP_ERR_INVALID_ARG;

    FATFS* fs;
    DWORD fre_clust;
    FRESULT fres = f_getfree("0:", &fre_clust, &fs);
    if (fres != FR_OK) fres = f_getfree("/sdcard", &fre_clust, &fs);
    if (fres != FR_OK || fs == NULL) return ESP_FAIL;

    uint64_t clusters = (uint64_t)(fs->n_fatent - 2);
    uint64_t csize = (uint64_t)fs->csize;
    uint64_t tot_sect = clusters * csize;
    uint64_t fre_sect = (uint64_t)fre_clust * csize;

    *total = tot_sect * 512ULL;
    *free  = fre_sect * 512ULL;
    return ESP_OK;
}

// Six-axis monitoring mission
void qmi8658_task(void) {
    imu_status_t status;
    int qmi8658_stat;

    QMI8658_read_xyz(status.acc, status.gyro, NULL);
    // Print the corresponding six-axis sensor data
    ESP_LOGI(TAG, "acc_x = %4.3fmg , acc_y  = %4.3fmg , acc_z  = %4.3fmg", status.acc[0], status.acc[1], status.acc[2]);
    ESP_LOGI(TAG, "gyro_x = %4.3fdps, gyro_y = %4.3fdps, gyro_z = %4.3fdps", status.gyro[0], status.gyro[1], status.gyro[2]);

    Paint_DrawRectangle(0, 57, 480, 650, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    char qmi8658_str[50] = {0};

    snprintf(qmi8658_str, sizeof(qmi8658_str), " acc_x = %4.3fmg", status.acc[0] );
    Paint_DrawString_CN(10, 59, qmi8658_str, &Font18_UTF8, WHITE, BLACK);

    snprintf(qmi8658_str, sizeof(qmi8658_str), " acc_y = %4.3fmg", status.acc[1] );
    Paint_DrawString_CN(10, 95, qmi8658_str, &Font18_UTF8, WHITE, BLACK);

    snprintf(qmi8658_str, sizeof(qmi8658_str), " acc_z = %4.3fmg", status.acc[2] );
    Paint_DrawString_CN(10, 131, qmi8658_str, &Font18_UTF8, WHITE, BLACK);

    snprintf(qmi8658_str, sizeof(qmi8658_str), " gyro_x = %4.3fdps", status.gyro[0] );
    Paint_DrawString_CN(10, 167, qmi8658_str, &Font18_UTF8, WHITE, BLACK);

    snprintf(qmi8658_str, sizeof(qmi8658_str), " gyro_y = %4.3fdps", status.gyro[1] );
    Paint_DrawString_CN(10, 201, qmi8658_str, &Font18_UTF8, WHITE, BLACK);

    snprintf(qmi8658_str, sizeof(qmi8658_str), " gyro_z = %4.3fdps", status.gyro[2] );
    Paint_DrawString_CN(10, 237, qmi8658_str, &Font18_UTF8, WHITE, BLACK);

    Refresh_page_settings();

    xSemaphoreTake(qmi8658_mutex, portMAX_DELAY);
    qmi8658_stat = check_imu_status(status.acc, status.gyro);
    xSemaphoreGive(qmi8658_mutex);

    ESP_LOGI(TAG,"status = %d\r\n", qmi8658_stat);
}

static void page_imu_monitor_show(void)
{
    EPD_Init();
    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);

    Paint_DrawString_CN(150, 20, " 六轴加速度 ", &Font24_UTF8, BLACK, WHITE);
    Paint_DrawString_CN(20, 690, " 功能键刷新，双击确认/Boot返回 ", &Font18_UTF8, WHITE, BLACK);
    EPD_Display_Base(Image_Mono);

    while (1) {
        qmi8658_task();
        int button = wait_key_event_and_return_code(pdMS_TO_TICKS(800));
        if (button == 8 || button == 22 || button == 23) {
            return;
        }
        if (button == 12) {
            EPD_Init();
            Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
            Paint_SetScale(2);
            Paint_SelectImage(Image_Mono);
            Paint_Clear(WHITE);
            Paint_DrawString_CN(150, 20, " 六轴加速度 ", &Font24_UTF8, BLACK, WHITE);
            Paint_DrawString_CN(20, 690, " 功能键刷新，双击确认/Boot返回 ", &Font18_UTF8, WHITE, BLACK);
            EPD_Display_Base(Image_Mono);
        }
    }
}

// Memory monitoring task
void SRAM_task(void) {
     // Read memory parameters
    ESP_LOGI("settings", "On-chip SRAM is available: %d", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI("settings", "PSRAM is available: %d", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI("settings", "Total SRAM is available: %d", heap_caps_get_free_size(MALLOC_CAP_8BIT));

    Paint_DrawRectangle(0, 57, 480, 650, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_CN(10, 59, " 内存信息: ", &Font24_UTF8, BLACK, WHITE);
    char SRAM[50] = {0};
    snprintf(SRAM, sizeof(SRAM), " 片上SRAM可用: %d KB", heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024 );
    Paint_DrawString_CN(25, 105, SRAM, &Font18_UTF8, WHITE, BLACK);
    snprintf(SRAM, sizeof(SRAM), " PSRAM可用: %d KB", heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024 );
    Paint_DrawString_CN(25, 141, SRAM, &Font18_UTF8, WHITE, BLACK);
    snprintf(SRAM, sizeof(SRAM), " 总SRAM可用: %d KB", heap_caps_get_free_size(MALLOC_CAP_8BIT)/1024 );
    Paint_DrawString_CN(25, 177, SRAM, &Font18_UTF8, WHITE, BLACK);


    Paint_DrawString_CN(10, 213, " TF卡信息: ", &Font24_UTF8, BLACK, WHITE);
    ESP_LOGI("settings", "Remaining space on the SD card");
    // Remaining space on the SD card
    uint64_t sd_total = 0, sd_free = 0;
    esp_err_t ret = get_sdcard_info(&sd_total, &sd_free);
    if (ret == ESP_OK) {
        ESP_LOGI("settings", "Total capacity of the SD card: %llu KB", sd_total / 1024);
        ESP_LOGI("settings", "Remaining capacity of the SD card: %llu KB", sd_free / 1024);
    } else {
        ESP_LOGI("settings", "The SD card information acquisition failed");
    }
    show_sdmmc_capacity();
    show_sdcard_all_info();

    Refresh_page_settings();
}

static esp_err_t enter_usb_share_mode(void)
{
    if (usb_msc_is_running()) {
        return ESP_OK;
    }

    if (!get_usb_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = sdcard_unmount();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdcard_unmount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = usb_msc_start_for_sdcard();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "usb_msc_start_for_sdcard failed: %s", esp_err_to_name(ret));
        sdcard_remount();
        return ret;
    }

    return ESP_OK;
}

static esp_err_t exit_usb_share_mode(void)
{
    esp_err_t ret = ESP_OK;

    if (usb_msc_is_running()) {
        ret = usb_msc_stop_for_sdcard();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "usb_msc_stop_for_sdcard failed: %s", esp_err_to_name(ret));
        }
    }

    esp_err_t remount_ret = sdcard_remount();
    if (remount_ret != ESP_OK) {
        ESP_LOGE(TAG, "sdcard_remount failed: %s", esp_err_to_name(remount_ret));
        return remount_ret;
    }

    return ret;
}

static void draw_sdcard_mode_page(int option, const char *hint, bool full_refresh)
{
    const bool usb_connected = get_usb_connected();
    const bool share_running = usb_msc_is_running();
    const bool host_using = share_running ? usb_msc_is_host_using_storage() : false;

    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);

    Paint_DrawString_CN(130, 90, " SD卡模式控制 ", &Font18_UTF8, BLACK, WHITE);
    Paint_DrawString_CN(40, 150,
                        share_running ? " 当前模式: 电脑共享 " : " 当前模式: 本地读写 ",
                        &Font18_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(40, 190,
                        usb_connected ? " Type-C状态: 已连接 " : " Type-C状态: 未连接 ",
                        &Font18_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(40, 230,
                        share_running
                            ? (host_using ? " 主机状态: 正在访问 " : " 主机状态: 已弹出设备 ")
                            : " 本地系统可直接读写SD卡 ",
                        &Font18_UTF8, WHITE, BLACK);

    if (hint != NULL) {
        Paint_DrawString_CN(40, 280, hint, &Font18_UTF8, WHITE, BLACK);
    }

    const int x1 = 70;
    const int x2 = 410;
    const int y_start = 380;
    const int y_step = 74;
    const int y_height = 50;

    const char *labels[3] = {"共享给电脑", "恢复本地读写", "返回设置页面"};

    for (int i = 0; i < 3; ++i) {
        const bool selected = (i == option);
        const int y1 = y_start + i * y_step;
        const int y2 = y1 + y_height;
        const uint16_t label_x = reassignCoordinates_CH((x1 + x2) / 2, labels[i], &Font18_UTF8);

        Paint_DrawRectangle(x1, y1, x2, y2, BLACK, DOT_PIXEL_2X2,
                            selected ? DRAW_FILL_FULL : DRAW_FILL_EMPTY);
        Paint_DrawString_CN(label_x, y1 + 5, labels[i], &Font18_UTF8,
                            selected ? BLACK : WHITE,
                            selected ? WHITE : BLACK);
    }

    Paint_DrawString_CN(30, 630, " 上下切换，功能键确认，返回键退出 ", &Font18_UTF8, WHITE, BLACK);

    if (full_refresh) {
        EPD_Display_Base(Image_Mono);
    } else {
        EPD_Display_Partial(Image_Mono, 0, 0, EPD_WIDTH, EPD_HEIGHT);
    }
}

static void page_sdcard_mode_show(void)
{
    int option = 0;
    const char *hint = "默认本地读写，按确认可切换模式";

    EPD_Init();
    draw_sdcard_mode_page(option, hint, true);

    while (1) {
        if (usb_msc_is_running()) {
            if (!get_usb_connected() || !usb_msc_is_host_using_storage()) {
                esp_err_t ret = exit_usb_share_mode();
                hint = (ret == ESP_OK) ? "检测到弹出或拔线，已恢复本地模式"
                                       : "自动恢复本地失败，请重试";
                draw_sdcard_mode_page(option, hint, false);
            }
        }

        int button = wait_key_event_and_return_code(pdMS_TO_TICKS(300));
        bool redraw = false;

        if (button == 14) {
            option = (option + 1) % 3;
            redraw = true;
        } else if (button == 0) {
            option = (option + 2) % 3;
            redraw = true;
        } else if (button == 7) {
            if (option == 0) {
                esp_err_t ret = enter_usb_share_mode();
                if (ret == ESP_OK) {
                    hint = "共享已开启，电脑可读写SD卡";
                } else if (ret == ESP_ERR_INVALID_STATE) {
                    hint = "请先连接Type-C到电脑";
                } else {
                    hint = "开启共享失败，请重试";
                }
                redraw = true;
            } else if (option == 1) {
                esp_err_t ret = exit_usb_share_mode();
                hint = (ret == ESP_OK) ? "已恢复本地读写模式"
                                       : "恢复本地失败，请重试";
                redraw = true;
            } else {
                exit_usb_share_mode();
                return;
            }
        } else if (button == 8 || button == 22) {
            exit_usb_share_mode();
            return;
        } else if (button == 12) {
            EPD_Init();
            draw_sdcard_mode_page(option, hint, true);
            continue;
        }

        if (redraw) {
            draw_sdcard_mode_page(option, hint, false);
        }
    }
}

void page_settings_show(void)
{
    int button;
    int idx = 0;

    int time_count = 0;
    Time_data time;
    int last_minutes = -1;

    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    time = PCF85063_GetTime();
    xSemaphoreGive(rtc_mutex);
    last_minutes = time.minutes;

    display_settings_init();
    while (1)
    {
        button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        if(button == -1) time_count++;
        if(time_count >= EPD_Sleep_Time){
            button = Sleep_wake_settings();
            time_count = 0;
        }
        if (button == 14) {
            idx = (idx + 1) % SETTINGS_ITEM_COUNT;
            time_count = 0;
            display_settings_option(idx);
        } else if (button == 0) {
            idx = (idx + SETTINGS_ITEM_COUNT - 1) % SETTINGS_ITEM_COUNT;
            time_count = 0;
            display_settings_option(idx);
        } else if (button == 7) {
            if (idx == 0) {
                page_handle_network_key_event();
                EPD_Init();
                display_settings_init();
                display_settings_option(idx);
                time_count = 0;
            } else if (idx == 1) {
                page_imu_monitor_show();
                EPD_Init();
                display_settings_init();
                display_settings_option(idx);
                time_count = 0;
            } else if (idx == 2) {
                page_sdcard_mode_show();
                EPD_Init();
                display_settings_init();
                display_settings_option(idx);
                time_count = 0;
            } else if (idx == 3) {
                EPD_Init();
                Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
                Paint_SetScale(2);
                Paint_SelectImage(Image_Mono);
                Paint_Clear(WHITE);
                page_timezone_select();
                EPD_Init();
                display_settings_init();
                display_settings_option(idx);
                time_count = 0;
            }
        } else if (button == 8 || button == 22) {
            EPD_Init();
            Refresh_page_settings();
            return;
        }

        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
        if ((time.minutes != last_minutes)) {
            last_minutes = time.minutes;
            display_settings_time_img(time);
        }
    }
}

int qmi8658_status_t(void)
{
    xSemaphoreTake(qmi8658_mutex, portMAX_DELAY);
    int qmi8658_stat = abs(qmi8658_status);
    xSemaphoreGive(qmi8658_mutex);
    return qmi8658_stat;
}


static void Forced_refresh_settings(void)
{
    EPD_Display_Base(Image_Mono);
}
static void Refresh_page_settings(void)
{
    EPD_Display_Partial(Image_Mono,0,0,EPD_WIDTH,EPD_HEIGHT);
}
static int Sleep_wake_settings()
{   
    int button = 0;
    int sleep_js = 0;
    Time_data rtc_time = {0};
    int last_minutes = -1;

    ESP_LOGI("home", "EPD_Sleep");
    Paint_DrawString_CN(10, 557, " 已进入睡眠，确认键唤醒 ", &Font24_UTF8, BLACK, WHITE);
    Refresh_page_settings();
    EPD_Sleep();
    while(1)
    {
        int button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        if (button == 12){
            ESP_LOGI("home", "EPD_Init");
            EPD_Init();
            Forced_refresh_settings();
            break;
        } else if (button == 8 || button == 22 || button == 14 || button == 0 || button == 7){
            ESP_LOGI("home", "EPD_Init");
            EPD_Init();
            Refresh_page_settings();
            break;
        }
        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        rtc_time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
        if(rtc_time.minutes != last_minutes) {
            last_minutes = rtc_time.minutes;
            ESP_LOGI("home", "EPD_Init");
            EPD_Init();
            // display_settings_time_img(rtc_time);
            Refresh_page_settings();
            // sleep_js++;
            // if(sleep_js > Unattended_Time){
            //     ESP_LOGI("home", "pwr_off");
            //     Paint_DrawString_CN(10, 602, " 已关机 ", &Font24_UTF8, BLACK, WHITE);
            //     Refresh_page_settings();
            //     axp_pwr_off();
            // }

            display_settings_time_img(rtc_time);
            Refresh_page_settings();
            ESP_LOGI("home", "EPD_Sleep");
            EPD_Sleep();
        }
    }

    return button;
}


static void display_settings_init()
{
    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);

    char Time_str[16]={0};
    int BAT_Power;
    char BAT_Power_str[16]={0};

    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    Time_data rtc_time = PCF85063_GetTime();
    xSemaphoreGive(rtc_mutex);

    snprintf(Time_str, sizeof(Time_str), "%02d:%02d", rtc_time.hours, rtc_time.minutes);
    Paint_DrawString_EN(20, 11, Time_str, &Font16, WHITE, BLACK);
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

    Paint_DrawString_CN(185, 18, " 设置 ", &Font24_UTF8, WHITE, BLACK);
    Paint_DrawRectangle(0, 57, 480, 640, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_CN(20, 120, " 网络设置 ", &Font24_UTF8, BLACK, WHITE);
    Paint_DrawString_CN(20, 190, " 六轴加速度 ", &Font24_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(20, 260, " USB模式 ", &Font24_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(20, 330, " 时区设置 ", &Font24_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(20, 430, " 上下切换，功能键进入，双击确认返回 ", &Font18_UTF8, WHITE, BLACK);

    Refresh_page_settings();
}


static void display_settings_option(int option)
{
    if(option == 0){
        Paint_DrawString_CN(20, 120, " 网络设置 ", &Font24_UTF8, BLACK, WHITE);
        Paint_DrawString_CN(20, 190, " 六轴加速度 ", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(20, 260, " USB模式 ", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(20, 330, " 时区设置 ", &Font24_UTF8, WHITE, BLACK);
    } else if(option == 1){
        Paint_DrawString_CN(20, 120, " 网络设置 ", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(20, 190, " 六轴加速度 ", &Font24_UTF8, BLACK, WHITE);
        Paint_DrawString_CN(20, 260, " USB模式 ", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(20, 330, " 时区设置 ", &Font24_UTF8, WHITE, BLACK);
    } else if(option == 2){
        Paint_DrawString_CN(20, 120, " 网络设置 ", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(20, 190, " 六轴加速度 ", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(20, 260, " USB模式 ", &Font24_UTF8, BLACK, WHITE);
        Paint_DrawString_CN(20, 330, " 时区设置 ", &Font24_UTF8, WHITE, BLACK);
    } else if(option == 3){
        Paint_DrawString_CN(20, 120, " 网络设置 ", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(20, 190, " 六轴加速度 ", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(20, 260, " USB模式 ", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(20, 330, " 时区设置 ", &Font24_UTF8, BLACK, WHITE);
    }
    Refresh_page_settings();
}

static void display_settings_time_img(Time_data rtc_time)
{
    char Time_str[16]={0};
    int BAT_Power;
    char BAT_Power_str[16]={0};

    snprintf(Time_str, sizeof(Time_str), "%02d:%02d", rtc_time.hours, rtc_time.minutes);
    Paint_DrawString_EN(20, 11, Time_str, &Font16, WHITE, BLACK);
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

    Refresh_page_settings();
}






















