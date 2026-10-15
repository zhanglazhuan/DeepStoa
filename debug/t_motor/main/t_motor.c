/* DeepStoa V1 vibration motor test.
 * Schematic pages 2-4: 3V3 -> R50 -> J5 -> Q5 -> GND.
 * AW9523 P1.0 (EN_Motor / TP27) drives Q5 through R52, active high.
 */
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "aw9523.h"
#include "deepstoa_v1.h"

static const char *TAG = "t_motor";

void app_main(void)
{
    ESP_LOGI(TAG, "=== DeepStoa V1 motor test ===");
    esp_err_t err = aw9523_init(DEEPV1_I2C_PORT,
                               DEEPV1_PIN_I2C_SDA,
                               DEEPV1_PIN_I2C_SCL,
                               DEEPV1_PIN_IO_RESET,
                               DEEPV1_AW9523_ADDR);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AW9523 init failed: %s; test aborted", esp_err_to_name(err));
        return;
    }

    /* Driver initialization sets GPIO mode, output direction and low levels.
     * Motor uses always-on 3V3, not the EN_POWER-controlled audio supply. */
    aw9523_set_pin(DEEPV1_AW_PIN_EN_MOTOR, 0);
    ESP_LOGI(TAG, "EN_Motor=P%u.%u (TP27), HIGH=ON; starting in 2 seconds",
             (unsigned)AW9523_GET_PORT(DEEPV1_AW_PIN_EN_MOTOR),
             (unsigned)AW9523_GET_BIT(DEEPV1_AW_PIN_EN_MOTOR));
    vTaskDelay(pdMS_TO_TICKS(2000));

    static const uint32_t pulse_ms[] = {200, 400, 800};
    for (size_t i = 0; i < sizeof(pulse_ms) / sizeof(pulse_ms[0]); ++i) {
        ESP_LOGI(TAG, "Pulse %u: ON for %lu ms", (unsigned)(i + 1),
                 (unsigned long)pulse_ms[i]);
        aw9523_set_pin(DEEPV1_AW_PIN_EN_MOTOR, 1);
        vTaskDelay(pdMS_TO_TICKS(pulse_ms[i]));
        aw9523_set_pin(DEEPV1_AW_PIN_EN_MOTOR, 0);
        ESP_LOGI(TAG, "Pulse %u: OFF for 1000 ms", (unsigned)(i + 1));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Sequence complete, motor commanded OFF. Reset to repeat.");
    ESP_LOGI(TAG, "Confirm vibration physically; logs are commands, not motor feedback.");
}
