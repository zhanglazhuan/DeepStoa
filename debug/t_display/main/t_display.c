// debug/t_display/main/t_display.c
// E-ink display test: GDEM0397T81P via ESP-IDF hardware SPI + direct GPIO
//
// Board: ESP32-S3 DevKit (wiring matches Arduino reference)
// Pin definitions: boards/esp32s3/esp32s3_devkit.h
//
// This file uses the display driver logic (init sequences, update functions)
// preserved from drivers/gdem0397t81p, with hardware SPI transport.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "esp32s3_devkit.h"
#include "epd_display.h"

static const char *TAG = "t_display";

// ─── Pattern generators ────────────────────────────────────────────────

// Fill buffer with 8x8 pixel checkerboard pattern
static void fill_checkerboard(uint8_t *buf)
{
    const int width_bytes = EPD_WIDTH / 8;  // 60 bytes per row
    for (int y = 0; y < EPD_HEIGHT; y++) {
        int y_block = y / 8;
        for (int x_byte = 0; x_byte < width_bytes; x_byte++) {
            int x_block = x_byte;
            uint8_t pixel = ((y_block + x_block) % 2 == 0) ? 0xFF : 0x00;
            buf[y * width_bytes + x_byte] = pixel;
        }
    }
}

// Fill buffer with 20px border rectangle (white background, black border)
static void fill_border(uint8_t *buf)
{
    const int width_bytes = EPD_WIDTH / 8;
    memset(buf, 0xFF, EPD_ARRAY);  // all white

    const int border = 20;
    // Top and bottom borders
    for (int y = 0; y < border; y++) {
        memset(&buf[y * width_bytes], 0x00, width_bytes);                  // top
        memset(&buf[(EPD_HEIGHT - 1 - y) * width_bytes], 0x00, width_bytes); // bottom
    }
    // Left and right borders
    for (int y = border; y < EPD_HEIGHT - border; y++) {
        for (int x = 0; x < border; x++) {
            int byte_idx = y * width_bytes + (x / 8);
            int bit_idx = 7 - (x % 8);
            buf[byte_idx] &= ~(1 << bit_idx);   // clear bit = black
        }
        for (int x = EPD_WIDTH - border; x < EPD_WIDTH; x++) {
            int byte_idx = y * width_bytes + (x / 8);
            int bit_idx = 7 - (x % 8);
            buf[byte_idx] &= ~(1 << bit_idx);
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
