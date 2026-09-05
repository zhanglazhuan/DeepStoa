// system/controller/vibration_control.c
// Vibration motor control — LEDC PWM on ESP32
// Ported from D:\Codes\EPOS\epos\drivers\epos_vibration_control.c

#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "vibration_control.h"

#define VIBRATION_GPIO  GPIO_NUM_48

static bool s_enabled = true;
static bool s_inited  = false;

static void init_ledc(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 200,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .gpio_num = VIBRATION_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&ch);
    s_inited = true;
}

int vibration_run_pattern(vibration_pattern_t pattern)
{
    if (!s_enabled) return -1;
    if (!s_inited) init_ledc();

    uint32_t duty = 0;
    int duration_ms = 100;

    switch (pattern) {
    case VIBRATION_PATTERN_CLICK:
        duty = 512; duration_ms = 50; break;
    case VIBRATION_PATTERN_NOTIFICATION:
        duty = 768; duration_ms = 200; break;
    case VIBRATION_PATTERN_ALARM:
        duty = 1023; duration_ms = 500; break;
    }

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    return 0;
}

int vibration_set_enabled(bool enable)
{
    s_enabled = enable;
    if (!enable && s_inited) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    return 0;
}
