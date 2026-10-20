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
#include "driver/i2c_master.h"

#include "deepstoa_v1.h"
#include "aw9523.h"
#include "epd_display.h"
#include "ft6336.h"

static const char *TAG = "t_touch";

#define CHECKER_CELL_PX 32

// E-paper RAM is column-major: one 100-byte column for each of 480 X positions.
// Touch coordinates are intentionally left raw so the grid can be used to
// collect calibration measurements later.
static uint8_t s_checkerboard[EPD_ARRAY];

static void show_checkerboard(void)
{
    const int bytes_per_column = EPD_HEIGHT / 8;
    const int bytes_per_cell = CHECKER_CELL_PX / 8;

    for (int x = 0; x < EPD_WIDTH; x++) {
        int cell_x = x / CHECKER_CELL_PX;
        uint8_t *column = s_checkerboard + x * bytes_per_column;
        for (int byte_y = 0; byte_y < bytes_per_column; byte_y++) {
            int cell_y = byte_y / bytes_per_cell;
            column[byte_y] = ((cell_x + cell_y) & 1) ? 0x00 : 0xFF;
        }
    }

    ESP_LOGI(TAG, "Drawing %dx%d checkerboard (%d px cells)",
             EPD_WIDTH, EPD_HEIGHT, CHECKER_CELL_PX);
    EPD_HW_Init_Fast();
    EPD_WhiteScreen_ALL_Fast(s_checkerboard);
    EPD_DeepSleep();
    ESP_LOGI(TAG, "Checkerboard ready; touch coordinates remain uncalibrated");
}

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

    // Step 1: Initialize the display and the shared I2C bus through AW9523.
    ESP_LOGI(TAG, "Step 1: Init display + shared I2C%d (SDA=%d SCL=%d)...",
             DEEPV1_I2C_PORT, DEEPV1_PIN_I2C_SDA, DEEPV1_PIN_I2C_SCL);
    epd_gpio_config();
    show_checkerboard();

    // J8 touch power comes from LCD_3V3 (schematic pages 2 and 6).
    // epd_gpio_config() has enabled the shared display/touch rail through P1.7.
    // Hold touch reset low before starting the controller.
    aw9523_set_pin(DEEPV1_AW_PIN_TOUCH_RST, 0);

    // FT6336 reset is active-low and driven by AW9523 P1.4.
    vTaskDelay(pdMS_TO_TICKS(10));
    aw9523_set_pin(DEEPV1_AW_PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(350));

    esp_err_t ret = i2c_master_probe(aw9523_get_i2c_bus(), DEEPV1_TOUCH_I2C_ADDR, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch probe at 0x%02X failed: %s. Check J8 power, reset and cable.",
                 DEEPV1_TOUCH_I2C_ADDR, esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Touch device ACK at 0x%02X", DEEPV1_TOUCH_I2C_ADDR);

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
    } else {
        ESP_LOGE(TAG, "Chip ID read failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "=== Touch polling started (touch the screen) ===");

    // Step 4: Poll touch data continuously
    uint32_t tick = 0;
    uint32_t read_errors = 0;
    while (1) {
        ft6336_touch_data_t data;
        ret = ft6336_read(&data);

        if (ret != ESP_OK) {
            if (read_errors++ % 20 == 0) {
                ESP_LOGE(TAG, "Touch read failed: %s (consecutive errors=%lu)",
                         esp_err_to_name(ret), (unsigned long)read_errors);
            }
        } else {
            if (read_errors > 0) {
                ESP_LOGI(TAG, "Touch reads recovered");
            }
            read_errors = 0;
            if (tick % 100 == 0) {
                ESP_LOGI(TAG, "Polling alive: I2C read OK, active touches=%u", data.count);
            }
        }

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
