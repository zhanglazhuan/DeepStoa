// debug/t_display/main/t_display.c
// E-ink display test: GDEM0397T81P on the DeepStoa v1 board
//
// SPI is connected directly to the ESP32-S3. DC, RESET and POWER are
// controlled through the AW9523 according to boards/esp32s3/deepstoa_v1.h.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "epd_display.h"
#include "deepstoa_v1.h"
#include "aw9523.h"

static const char *TAG = "t_display";

// ─── Pattern generators ────────────────────────────────────────────────

// The GDEM0397T81P RAM is addressed column-major: one column contains
// EPD_HEIGHT / 8 bytes, and each byte contains eight vertical pixels.
// Keep test patterns in the same layout as the panel driver.
static void fill_checkerboard(uint8_t *buf)
{
    const int bytes_per_column = EPD_HEIGHT / 8;
    for (int x = 0; x < EPD_WIDTH; x++) {
        uint8_t *column = &buf[x * bytes_per_column];
        for (int y_byte = 0; y_byte < bytes_per_column; y_byte++) {
            uint8_t pixel = ((x / 8 + y_byte) % 2 == 0) ? 0xFF : 0x00;
            column[y_byte] = pixel;
        }
    }
}

// Fill buffer with 20px border rectangle (white background, black border)
static void fill_border(uint8_t *buf)
{
    memset(buf, 0xFF, EPD_ARRAY);  // all white

    const int border = 20;
    const int bytes_per_column = EPD_HEIGHT / 8;
    for (int x = 0; x < EPD_WIDTH; x++) {
        uint8_t *column = &buf[x * bytes_per_column];
        for (int y_byte = 0; y_byte < bytes_per_column; y_byte++) {
            for (int bit = 0; bit < 8; bit++) {
                int y = y_byte * 8 + bit;
                if (x < border || x >= EPD_WIDTH - border ||
                    y < border || y >= EPD_HEIGHT - border) {
                    column[y_byte] &= (uint8_t)~(1U << (7 - bit));
                }
            }
        }
    }
}

// ─── Main test sequence ────────────────────────────────────────────────

void app_main(void)
{
    ESP_LOGI(TAG, "=== t_display: E-ink Display Test (HW SPI) ===");

    // Step 1: Configure GPIO and SPI
    ESP_LOGI(TAG, "Step 1: Init GPIO and hardware SPI...");
    epd_gpio_config();

    ESP_LOGI(TAG, "PASS: GPIO and SPI initialized");

    // Step 2: Init display (full update mode)
    ESP_LOGI(TAG, "Step 2: Init display...");
    EPD_HW_Init();
    ESP_LOGI(TAG, "PASS: Display initialized");

    // Step 3: White screen
    ESP_LOGI(TAG, "Step 3: White screen...");
    EPD_WhiteScreen_White();
    ESP_LOGI(TAG, "PASS: White screen done");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Step 4: Black screen
    ESP_LOGI(TAG, "Step 4: Black screen...");
    EPD_HW_Init();  // re-init after prior update mode
    EPD_WhiteScreen_Black();
    ESP_LOGI(TAG, "PASS: Black screen done");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Step 5: Fast update with checkerboard
    ESP_LOGI(TAG, "Step 5: Fast update - checkerboard...");
    uint8_t *buf = (uint8_t *)malloc(EPD_ARRAY);
    if (buf == NULL) {
        ESP_LOGE(TAG, "FAIL: malloc(%d) failed", EPD_ARRAY);
        return;
    }
    EPD_HW_Init_Fast();
    fill_checkerboard(buf);
    EPD_WhiteScreen_ALL_Fast(buf);
    ESP_LOGI(TAG, "PASS: Fast checkerboard done");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Step 6: Full update with border rectangle
    ESP_LOGI(TAG, "Step 6: Full update - border rectangle...");
    EPD_HW_Init();
    fill_border(buf);
    EPD_WhiteScreen_ALL(buf);
    ESP_LOGI(TAG, "PASS: Border rectangle done");
    vTaskDelay(pdMS_TO_TICKS(2000));

    free(buf);

    // Step 7: Deep sleep
    ESP_LOGI(TAG, "Step 7: Deep sleep...");
    EPD_DeepSleep();
    ESP_LOGI(TAG, "PASS: Display entered deep sleep");

    ESP_LOGI(TAG, "=== TEST COMPLETE: All steps passed ===");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
