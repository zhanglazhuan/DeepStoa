// main/DeepStoa.c
// DeepStoa entry point — LVGL + launcher + apps

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "lvgl.h"

#include "esp32s3_devkit.h"
#include "monitor.h"
#include "flash_control.h"
#include "display_control.h"
#include "touch_control.h"
#include "app_manager.h"
#include "fs_control.h"
#include "wifi_manager.h"
#include "audio_service.h"
#include "ota.h"
#include "settings_model.h"
#include "time_service.h"
#include "alarm_service.h"
#include "launcher.h"
#include "settings_app.h"
#include "clock_app.h"
#include "app.h"        // chatbot
#include "todolist_app.h"
#include "anki_app.h"
#include "calendar_app.h"
#include "news_app.h"
#include "player_app.h"
#include "reader_app.h"
#include "ft6336.h"
#include "lv_status_bar.h"
#include "lv_alarm_alert.h"
#include "ui_fonts.h"
#include "lv_theme_hardcore.h"

static const char *TAG = "deepstoa";

void app_main(void)
{
    ESP_LOGI(TAG, "=== DeepStoa Starting ===");
    /* Force the SoC reset-reason implementation into the image.  Without a
     * reference, the weak flash-resident fallback can satisfy the brownout
     * ISR and cause a cache error while flash is disabled. */
    ESP_LOGI(TAG, "Reset reason: %d", (int)esp_reset_reason());

    /* ---- 0. 资源监控 ----
     * 放在最前面：它启动时会立刻打一份基线，那是"什么都还没起来时内部 RAM
     * 有多少"，后面每 60 秒一次的数字都要和它比。同时装上堆分配失败钩子 ——
     * 报告跑在 esp_timer 任务上，main 就算在 LVGL 里活锁了它照样出报告。 */
    monitor_init();

    // ---- 1. Init LVGL ----
    ESP_LOGI(TAG, "Init LVGL...");
    lv_init();

    // ---- 2. Init display (LVGL + e-ink) ----
    ESP_LOGI(TAG, "Init display...");
    display_control_init();

    // ---- 3. Init touch ----
    ESP_LOGI(TAG, "Init touch...");
    esp_err_t ret = ft6336_init(DEVKIT_TOUCH_I2C_PORT,
                                DEVKIT_PIN_TOUCH_SDA,
                                DEVKIT_PIN_TOUCH_SCL,
                                DEVKIT_PIN_TOUCH_RST,
                                DEVKIT_TOUCH_I2C_ADDR);
    if (ret == ESP_OK) {
        touch_control_init();
        ESP_LOGI(TAG, "Touch ready");
    } else {
        ESP_LOGW(TAG, "Touch init failed (0x%X)", ret);
    }

    launcher_gesture_init();

    // ---- 3b. Init status bar (global, on lv_layer_top) ----
    ESP_LOGI(TAG, "Init status bar...");
    lv_status_bar_init();

    /* 闹钟/计时提醒画在 lv_layer_top 上 —— 到点时用户可能在任何 app 里 */
    lv_alarm_alert_init();

    // ---- 3c. Init custom fonts & theme (must be before any UI that uses custom_font_normal) ----
    ESP_LOGI(TAG, "Init fonts & theme...");
    ui_fonts_init();
    lv_theme_hardcore_init(disp, false, LV_FONT_NORMAL);

    // ---- 4. Init flash storage ----
    ESP_LOGI(TAG, "Init FlashDB...");
    flash_control_init();

    /* 走到这里说明这版固件能起来 —— ota_init() 里会取消 bootloader 回滚，
     * 并把上一次升级的结果排队回报给服务端。必须在 FlashDB 之后。 */
    ESP_LOGI(TAG, "Init OTA...");
    ota_init();

    /* ---- 4b. 系统级服务：必须比 app 活得久 ----
     * 时间服务要在任何 app 之前起来 —— 时区和 12/24h 是它持久化的，
     * 状态栏和闹钟服务都依赖它每分钟广播的 APP_EVENT_CLOCK_TICK。 */
    ESP_LOGI(TAG, "Init time service...");
    time_service_init();

    /* 闹钟和计时器同理：退出 Clock app 不该让番茄钟停摆。 */
    ESP_LOGI(TAG, "Init alarm service...");
    alarm_service_init();

    /* 内部 flash 上的 FAT，挂到 /sdcard。必须在 audio_service_init() 之前 ——
     * 续播恢复出来的队列路径要能解析得到。 */
    ESP_LOGI(TAG, "Mount internal filesystem...");
    fs_control_init();

    /* 只初始化 Wi-Fi 子系统（NVS / TCP-IP / netif / driver），不开电台。
     * 开不开由 Settings 里的开关决定，上次开着的话
     * settings_controller_wifi_init() 会自动恢复。 */
    ESP_LOGI(TAG, "Init Wi-Fi manager...");
    wifi_manager_init();

    /* 播放会话必须比 player app 活得久：退出播放器不该断音。 */
    audio_service_init();

    // ---- 5. Init app manager ----
    ESP_LOGI(TAG, "Init App Manager...");
    app_manager_init();

    // ---- 6. Register apps ----
    ESP_LOGI(TAG, "Register apps...");
    settings_init();
    clock_app_init();
    chatbot_init();
    todolist_init();
    anki_init();
    calendar_init();
    news_app_init();
    player_init();
    reader_init();

    // ---- 6b. Background update check (silent; result shows up in Settings) ----
    settings_model_update_t upd;
    settings_model_load_update(&upd);
    if (upd.auto_check_enabled) {
        /* 延后 30 秒，给 WiFi 留出连上的时间；没连上就静默放弃 */
        ota_boot_check(upd.manifest_url[0] ? upd.manifest_url : NULL, 30000);
    }

    // ---- 7. Show launcher home ----
    ESP_LOGI(TAG, "Show launcher home...");
    launcher_home_ui();

    ESP_LOGI(TAG, "=== DeepStoa Ready ===");

    // ---- 8. LVGL main loop ----
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
