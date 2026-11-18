// debug/t_aw9523/main/t_aw9523.c
// AW9523 IO Expander: non-destructive board validation test

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "deepstoa_v1.h"
#include "aw9523.h"

static const char *TAG = "t_aw9523";

static uint8_t get_port_bit(uint8_t port_value, uint8_t pin)
{
    return (port_value >> AW9523_GET_BIT(pin)) & 0x01U;
}

static void run_aw9523_test(void)
{
    ESP_LOGI(TAG, "=== t_aw9523: AW9523 IO Expander Validation ===");

    ESP_LOGI(TAG, "Board V1.1: SDA=%d SCL=%d RESET=%d address=0x%02X",
             DEEPV1_PIN_I2C_SDA, DEEPV1_PIN_I2C_SCL,
             DEEPV1_PIN_IO_RESET, DEEPV1_AW9523_ADDR);

    // ========================================================================
    // Phase 1: I2C & Identity
    // ========================================================================
    ESP_LOGI(TAG, "--- Phase 1: Init & Chip ID ---");
    // V1.1 audio codecs share SDA/SCL with U8. Power their I/O supply
    // before probing the bus; leave the speaker amplifier disabled.
    gpio_config_t power_conf = {
        .pin_bit_mask = 1ULL << DEEPV1_PIN_EN_POWER,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&power_conf);
    if (ret == ESP_OK) ret = gpio_set_level(DEEPV1_PIN_EN_POWER, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: Audio power enable: %s", esp_err_to_name(ret));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "EN_Power GPIO%d HIGH; audio supply enabled before I2C probe",
             DEEPV1_PIN_EN_POWER);

    ret = aw9523_init(DEEPV1_I2C_PORT,
                                DEEPV1_PIN_I2C_SDA,
                                DEEPV1_PIN_I2C_SCL,
                                DEEPV1_PIN_IO_RESET,
                                DEEPV1_AW9523_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: aw9523_init: %s (0x%x)", esp_err_to_name(ret), ret);
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
    // Phase 2: Verify outputs remain in their non-destructive startup state
    // ========================================================================
    ESP_LOGI(TAG, "--- Phase 2: Verify Safe Output State ---");
    uint8_t p0_read, p1_read;
    ret = aw9523_read_port_checked(0, &p0_read);
    if (ret == ESP_OK) ret = aw9523_read_port_checked(1, &p1_read);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: Input read: %s; cannot verify safe outputs",
                 esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "  Raw inputs: P0=0x%02X P1=0x%02X", p0_read, p1_read);

    bool outputs_safe = ((p0_read & DEEPV1_AW_P0_OUTPUT_MASK) == 0U) &&
                        ((p1_read & DEEPV1_AW_P1_OUTPUT_MASK) == 0U);
    if (outputs_safe) {
        ESP_LOGI(TAG, "PASS: Controlled outputs are LOW (safe state)");
    } else {
        ESP_LOGE(TAG, "FAIL: One or more controlled outputs read HIGH");
    }

    // ========================================================================
    // Phase 3: Report the two real AW9523 input signals
    // ========================================================================
    ESP_LOGI(TAG, "--- Phase 3: Input Snapshot ---");
    uint8_t usb_detect = get_port_bit(p1_read, DEEPV1_AW_PIN_USB_DETECT);
    uint8_t charge = get_port_bit(p1_read, DEEPV1_AW_PIN_CHARGE);
    ESP_LOGI(TAG, "  USB_DETE  P1.0=%u (%s)", usb_detect,
             usb_detect == 0U ? "USB present" : "USB absent");
    ESP_LOGI(TAG, "  CHAG      P1.1=%u (%s)", charge,
             charge == 0U ? "charging" : "not charging");

    // ========================================================================
    // Phase 4: Summary
    // ========================================================================
    ESP_LOGI(TAG, "--- Phase 4: Summary ---");
    bool all_pass = outputs_safe && (chip_id == 0x23U);

    if (all_pass) {
        ESP_LOGI(TAG, "=== TEST COMPLETE: SAFE CHECK PASSED ===");
    } else {
        ESP_LOGE(TAG, "=== TEST COMPLETE: FAILURES DETECTED ===");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    run_aw9523_test();
}
