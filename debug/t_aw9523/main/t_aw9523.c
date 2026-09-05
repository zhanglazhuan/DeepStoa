// debug/t_aw9523/main/t_aw9523.c
// AW9523 IO Expander: full pin-level validation test

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "deepstoa_v1.h"
#include "aw9523.h"

static const char *TAG = "t_aw9523";

// Output pin list: all pins EXCEPT P0.5 (INT) and P1.6 (BUSY) which are inputs
static const uint8_t g_output_pins[] = {
    AW9523_PIN(0, 0), AW9523_PIN(0, 1), AW9523_PIN(0, 2), AW9523_PIN(0, 3),
    AW9523_PIN(0, 4), AW9523_PIN(0, 6), AW9523_PIN(0, 7),
    AW9523_PIN(1, 0), AW9523_PIN(1, 1), AW9523_PIN(1, 2), AW9523_PIN(1, 3),
    AW9523_PIN(1, 4), AW9523_PIN(1, 5), AW9523_PIN(1, 7),
};
static const int g_num_output_pins = sizeof(g_output_pins) / sizeof(g_output_pins[0]);

static const uint8_t g_input_pins[] = {
    AW9523_PIN(0, 5),  // LCD_INT
    AW9523_PIN(1, 6),  // LCD_BUSY
};
static const int g_num_input_pins = sizeof(g_input_pins) / sizeof(g_input_pins[0]);

void app_main(void)
{
    ESP_LOGI(TAG, "=== t_aw9523: AW9523 IO Expander Validation ===");

    // ========================================================================
    // Phase 1: I2C & Identity
    // ========================================================================
    ESP_LOGI(TAG, "--- Phase 1: Init & Chip ID ---");
    esp_err_t ret = aw9523_init(DEEPV1_I2C_PORT,
                                DEEPV1_PIN_I2C_SDA,
                                DEEPV1_PIN_I2C_SCL,
                                DEEPV1_PIN_IO_RESET,
                                DEEPV1_AW9523_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: aw9523_init returned %d", ret);
        return;
    }

    uint8_t chip_id = aw9523_get_chip_id();
    ESP_LOGI(TAG, "Chip ID: 0x%02X (expected 0x23)", chip_id);
    if (chip_id != 0x23) {
        ESP_LOGE(TAG, "FAIL: Unexpected chip ID 0x%02X", chip_id);
        return;
    }
    ESP_LOGI(TAG, "PASS: Phase 1 — chip ID OK");
    vTaskDelay(pdMS_TO_TICKS(100));

    // ========================================================================
    // Phase 2: Output Pin Walk
    // ========================================================================
    ESP_LOGI(TAG, "--- Phase 2: Output Pin Walk (%d pins) ---", g_num_output_pins);

    int pin_pass = 0, pin_fail = 0;
    for (int i = 0; i < g_num_output_pins; i++) {
        uint8_t pin = g_output_pins[i];
        uint8_t port = AW9523_GET_PORT(pin);
        uint8_t bit = AW9523_GET_BIT(pin);

        // Set pin HIGH, read back
        aw9523_set_pin(pin, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
        uint8_t val_high = aw9523_get_pin(pin);

        // Set pin LOW, read back
        aw9523_set_pin(pin, 0);
        vTaskDelay(pdMS_TO_TICKS(1));
        uint8_t val_low = aw9523_get_pin(pin);

        if (val_high == 1 && val_low == 0) {
            ESP_LOGI(TAG, "  P%d.%d: PASS (H=%d L=%d)", port, bit, val_high, val_low);
            pin_pass++;
        } else {
            ESP_LOGE(TAG, "  P%d.%d: FAIL (H=%d L=%d)", port, bit, val_high, val_low);
            pin_fail++;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // ========================================================================
    // Phase 3: Input Pin Read
    // ========================================================================
    ESP_LOGI(TAG, "--- Phase 3: Input Pin Read ---");
    for (int i = 0; i < g_num_input_pins; i++) {
        uint8_t pin = g_input_pins[i];
        uint8_t port = AW9523_GET_PORT(pin);
        uint8_t bit = AW9523_GET_BIT(pin);
        uint8_t val = aw9523_get_pin(pin);
        ESP_LOGI(TAG, "  P%d.%d: %d", port, bit, val);
    }

    // ========================================================================
    // Phase 4: Port-Level Operations
    // ========================================================================
    ESP_LOGI(TAG, "--- Phase 4: Port-Level Ops ---");

    // Set P0 to all outputs, write 0xAA, read back
    aw9523_set_port_dir(0, 0x00);
    aw9523_write_port(0, 0xAA);
    vTaskDelay(pdMS_TO_TICKS(1));
    uint8_t p0_read = aw9523_read_port(0);
    ESP_LOGI(TAG, "  P0 write 0xAA -> read 0x%02X %s",
             p0_read, (p0_read == 0xAA) ? "PASS" : "FAIL");

    // Set P1 to all outputs, write 0x55, read back
    aw9523_set_port_dir(1, 0x00);
    aw9523_write_port(1, 0x55);
    vTaskDelay(pdMS_TO_TICKS(1));
    uint8_t p1_read = aw9523_read_port(1);
    ESP_LOGI(TAG, "  P1 write 0x55 -> read 0x%02X %s",
             p1_read, (p1_read == 0x55) ? "PASS" : "FAIL");

    int port_pass = ((p0_read == 0xAA) && (p1_read == 0x55)) ? 1 : 0;

    // ========================================================================
    // Phase 5: Restore Safe State
    // ========================================================================
    ESP_LOGI(TAG, "--- Phase 5: Restore Safe State ---");
    // Match aw9523_init defaults:
    // P0: bit5=INT(input), others output low
    aw9523_set_port_dir(0, (1 << 5));
    aw9523_write_port(0, 0x00);
    // P1: bit6=BUSY(input), bit4=CS(high), others output low
    aw9523_set_port_dir(1, (1 << 6));
    aw9523_write_port(1, (1 << 4));
    ESP_LOGI(TAG, "  Safe state restored (CS=high, all others low)");

    // ========================================================================
    // Phase 6: Summary
    // ========================================================================
    ESP_LOGI(TAG, "--- Phase 6: Summary ---");
    ESP_LOGI(TAG, "  Output pins: %d/%d passed", pin_pass, g_num_output_pins);
    bool all_pass = (pin_fail == 0) && port_pass && (chip_id == 0x23);

    if (all_pass) {
        ESP_LOGI(TAG, "=== TEST COMPLETE: ALL PASSED ===");
    } else {
        ESP_LOGE(TAG, "=== TEST COMPLETE: FAILURES DETECTED ===");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
