// debug/t_display/main/t_display.c
// E-ink display test: verify GDEM0397T81P via AW9523BTQR bit-bang SPI

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "deepstoa_v1.h"
#include "aw9523.h"
#include "Display_EPD_W21.h"

static const char *TAG = "t_display";

// Fill buffer with checkerboard pattern (8x8 pixel blocks)
static void fill_checkerboard(uint8_t *buf)
{
    // Each byte = 8 horizontal pixels. One row = 480/8 = 60 bytes
    const int width_bytes = EPD_WIDTH / 8;
    for (int y = 0; y < EPD_HEIGHT; y++) {
        int y_block = y / 8;  // which 8-pixel-tall block
        for (int x_byte = 0; x_byte < width_bytes; x_byte++) {
            int x_block = x_byte;  // each byte = 8 pixels = 1 block wide
            uint8_t pixel = ((y_block + x_block) % 2 == 0) ? 0xFF : 0x00;
            buf[y * width_bytes + x_byte] = pixel;
        }
    }
}

// Fill buffer with border rectangle (20px from edges)
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

void app_main(void)
{
    ESP_LOGI(TAG, "=== t_display: E-ink Display Test ===");

    // Step 1: Initialize I2C and AW9523
    ESP_LOGI(TAG, "Step 1: Init AW9523 via I2C...");
    esp_err_t ret = aw9523_init(DEEPV1_I2C_PORT,
                                DEEPV1_PIN_I2C_SDA,
                                DEEPV1_PIN_I2C_SCL,
                                DEEPV1_PIN_IO_RESET,
                                DEEPV1_AW9523_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: aw9523_init returned %d", ret);
        return;
    }

    // Verify chip ID
    uint8_t chip_id = aw9523_get_chip_id();
    ESP_LOGI(TAG, "AW9523 chip ID: 0x%02X (expected 0x23)", chip_id);
    if (chip_id != 0x23) {
        ESP_LOGE(TAG, "FAIL: Unexpected chip ID 0x%02X", chip_id);
        return;
    }
    ESP_LOGI(TAG, "PASS: AW9523 initialized, chip ID OK");

    // Step 2: Init display
    ESP_LOGI(TAG, "Step 2: Init display...");
    EPD_GPIO_Config();
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

    // Step 5: Checkerboard pattern
    ESP_LOGI(TAG, "Step 5: Checkerboard pattern...");
    uint8_t *buf = (uint8_t *)malloc(EPD_ARRAY);
    if (buf == NULL) {
        ESP_LOGE(TAG, "FAIL: malloc(%d) failed", EPD_ARRAY);
        return;
    }
    EPD_HW_Init();
    fill_checkerboard(buf);
    EPD_WhiteScreen_ALL(buf);
    ESP_LOGI(TAG, "PASS: Checkerboard done");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Step 6: Border rectangle pattern
    ESP_LOGI(TAG, "Step 6: Border rectangle pattern...");
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
