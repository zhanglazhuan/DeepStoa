// debug/t_touch/main/t_touch.c
// FT6336U touch controller test
//
// DeepStoa v1 shared I2C: SCL=GPIO18, SDA=GPIO17
// INT: GPIO9, RST: AW9523 P1.4
// I2C address: 0x38

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "deepstoa_v1.h"
#include "aw9523.h"
#include "ft6336.h"

static const char *TAG = "t_touch";

static const char *event_str(ft6336_event_t e)
{
    switch (e) {
    case FT6336_EVENT_DOWN:    return "DOWN";
    case FT6336_EVENT_UP:      return "UP";
    case FT6336_EVENT_CONTACT: return "CONTACT";
    default:                   return "NONE";
    }
}

static const char *gesture_str(uint8_t g)
{
    switch (g) {
    case FT6336_GESTURE_NONE:          return "NONE";
    case FT6336_GESTURE_SWIPE_UP:      return "SWIPE UP";
    case FT6336_GESTURE_SWIPE_DOWN:    return "SWIPE DOWN";
    case FT6336_GESTURE_SWIPE_LEFT:    return "SWIPE LEFT";
    case FT6336_GESTURE_SWIPE_RIGHT:   return "SWIPE RIGHT";
    case FT6336_GESTURE_ZOOM_IN:       return "ZOOM IN";
    case FT6336_GESTURE_ZOOM_OUT:      return "ZOOM OUT";
    case FT6336_GESTURE_PRESS:         return "PRESS";
    default:                           return "?";
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== t_touch: FT6336 Touch Controller Test ===");

    // Step 1: Initialize the shared I2C bus through the AW9523 driver.
    ESP_LOGI(TAG, "Step 1: Init shared I2C%d (SDA=%d SCL=%d)...",
             DEEPV1_I2C_PORT, DEEPV1_PIN_I2C_SDA, DEEPV1_PIN_I2C_SCL);
    esp_err_t ret = aw9523_init(DEEPV1_I2C_PORT,
                                DEEPV1_PIN_I2C_SDA,
                                DEEPV1_PIN_I2C_SCL,
                                DEEPV1_PIN_IO_RESET,
                                DEEPV1_AW9523_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: aw9523_init returned 0x%X (%s)",
                 ret, esp_err_to_name(ret));
        return;
    }

    // FT6336 reset is active-low and driven by AW9523 P1.4.
    aw9523_set_pin(DEEPV1_AW_PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    aw9523_set_pin(DEEPV1_AW_PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(350));

    // Step 2: Attach FT6336 to the I2C bus already owned by AW9523.
    ESP_LOGI(TAG, "Step 2: Init FT6336 at 0x%02X...", DEEPV1_TOUCH_I2C_ADDR);
    ret = ft6336_init_shared(aw9523_get_i2c_bus(), GPIO_NUM_NC,
                             DEEPV1_TOUCH_I2C_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: ft6336_init_shared returned 0x%X (%s)",
                 ret, esp_err_to_name(ret));
        return;
    }

    // Step 3: Read chip ID
    uint8_t chip_id;
    ret = ft6336_get_chip_id(&chip_id);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Chip ID: 0x%02X (expected 0x02 for FT6336U)", chip_id);
        if (chip_id != 0x02) {
            ESP_LOGW(TAG, "Unexpected chip ID — may not be FT6336U");
        } else {
            ESP_LOGI(TAG, "PASS: FT6336U detected");
        }
    }

    ESP_LOGI(TAG, "=== Touch polling started (touch the screen) ===");

    // Step 4: Poll touch data continuously
    uint32_t tick = 0;
    while (1) {
        ft6336_touch_data_t data;
        ret = ft6336_read(&data);

        if (ret == ESP_OK && data.count > 0) {
            ESP_LOGI(TAG, "[%lu] Touches=%d Gesture=%s",
                     tick, data.count, gesture_str(data.gesture_id));

            for (int i = 0; i < data.count; i++) {
                ft6336_touch_point_t *p = &data.points[i];
                ESP_LOGI(TAG, "  Point %d: ID=%u X=%4u Y=%4u Weight=%3u Area=%2u Event=%s",
                         i, p->id, p->x, p->y, p->weight, p->area, event_str(p->event));
            }
        }

        tick++;
        vTaskDelay(pdMS_TO_TICKS(50));  // 20 Hz polling
    }
}
