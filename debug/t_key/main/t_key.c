/*
 * DeepStoa V1 key diagnostic, based on NomadCast debug/t_key.
 * Schematic pages 3 and 7: K3 Vol+, K4 Vol-, K5 Record, K6 OnOff.
 * Each key has an external 100k pull-down; pressed = high.
 * Only log events: the power and record keys do not execute actions.
 */
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "deepstoa_v1.h"

#define POLL_MS 10
#define DEBOUNCE_US (50 * 1000LL)

static const char *TAG = "t_key";

typedef struct {
    const char *name;
    gpio_num_t pin;
    int candidate;
    int stable;
    int64_t changed_at;
    int64_t pressed_at;
} key_t;

static key_t s_keys[] = {
    {.name = "Vol+",   .pin = DEEPV1_PIN_BUTTON_VOL_UP},
    {.name = "Vol-",   .pin = DEEPV1_PIN_BUTTON_VOL_DOWN},
    {.name = "OnOff",  .pin = DEEPV1_PIN_BUTTON_POWER},
    {.name = "Record", .pin = DEEPV1_PIN_BUTTON_RECORD},
};

void app_main(void)
{
    uint64_t mask = 0;
    for (size_t i = 0; i < sizeof(s_keys) / sizeof(s_keys[0]); ++i) {
        mask |= UINT64_C(1) << s_keys[i].pin;
    }
    const gpio_config_t config = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));

    ESP_LOGI(TAG, "DeepStoa V1 key test: active=%u, debounce=50ms",
             (unsigned)DEEPV1_BUTTON_ACTIVE_LEVEL);
    for (size_t i = 0; i < sizeof(s_keys) / sizeof(s_keys[0]); ++i) {
        key_t *key = &s_keys[i];
        key->stable = !DEEPV1_BUTTON_ACTIVE_LEVEL;
        key->candidate = gpio_get_level(key->pin);
        key->changed_at = esp_timer_get_time();
        ESP_LOGI(TAG, "%s: GPIO%d initial raw=%d", key->name,
                 (int)key->pin, key->candidate);
    }
    ESP_LOGI(TAG, "Press/release keys; held keys emit one press and one release.");

    /* Independent debounce avoids blocking other keys while one bounces.
     * A key held at boot is reported after its first stable 50ms. */
    while (1) {
        for (size_t i = 0; i < sizeof(s_keys) / sizeof(s_keys[0]); ++i) {
            key_t *key = &s_keys[i];
            const int level = gpio_get_level(key->pin);
            const int64_t now = esp_timer_get_time();
            if (level != key->candidate) {
                key->candidate = level;
                key->changed_at = now;
            }
            if (key->candidate == key->stable ||
                now - key->changed_at < DEBOUNCE_US) {
                continue;
            }

            key->stable = key->candidate;
            if (key->stable == DEEPV1_BUTTON_ACTIVE_LEVEL) {
                key->pressed_at = key->changed_at;
                ESP_LOGI(TAG, "[BTN] %s pressed (GPIO%d=%d)",
                         key->name, (int)key->pin, key->stable);
            } else {
                ESP_LOGI(TAG, "[BTN] %s released (GPIO%d=%d, held=%" PRId64 "ms)",
                         key->name, (int)key->pin, key->stable,
                         (key->changed_at - key->pressed_at) / 1000);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS) > 0 ? pdMS_TO_TICKS(POLL_MS) : 1);
    }
}
