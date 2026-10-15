#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"

static const char *TAG = "t_psram";

#define TEST_BYTES (64 * 1024)

static bool check_pattern(uint8_t *buffer, size_t size, uint8_t pattern)
{
    for (size_t i = 0; i < size; ++i) {
        if (buffer[i] != pattern) {
            ESP_LOGE(TAG, "Pattern mismatch at offset %u: got 0x%02x expected 0x%02x",
                     (unsigned)i, buffer[i], pattern);
            return false;
        }
    }
    return true;
}

static bool run_psram_rw_test(void)
{
    uint8_t *buffer = heap_caps_malloc(TEST_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "heap_caps_malloc(%u, SPIRAM) failed", TEST_BYTES);
        return false;
    }

    ESP_LOGI(TAG, "Allocated %u bytes at %p from PSRAM", TEST_BYTES, (void *)buffer);

    memset(buffer, 0xA5, TEST_BYTES);
    if (!check_pattern(buffer, TEST_BYTES, 0xA5)) {
        free(buffer);
        return false;
    }

    for (size_t i = 0; i < TEST_BYTES; ++i) {
        buffer[i] = (uint8_t)(i ^ (i >> 8) ^ 0x5A);
    }
    for (size_t i = 0; i < TEST_BYTES; ++i) {
        uint8_t expected = (uint8_t)(i ^ (i >> 8) ^ 0x5A);
        if (buffer[i] != expected) {
            ESP_LOGE(TAG, "Walking pattern mismatch at offset %u: got 0x%02x expected 0x%02x",
                     (unsigned)i, buffer[i], expected);
            free(buffer);
            return false;
        }
    }

    free(buffer);
    ESP_LOGI(TAG, "PSRAM read/write test passed");
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 PSRAM test ===");
    ESP_LOGI(TAG, "PSRAM initialized: %s", esp_psram_is_initialized() ? "yes" : "no");

    size_t total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "PSRAM heap: total=%u free=%u", (unsigned)total, (unsigned)free_bytes);

    if (!esp_psram_is_initialized() || total == 0) {
        ESP_LOGE(TAG, "PSRAM is not available through the heap");
        return;
    }

    bool passed = run_psram_rw_test();
    ESP_LOGI(TAG, "Result: %s", passed ? "PASS" : "FAIL");
}
