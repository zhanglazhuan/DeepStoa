#include "pomodoro_service.h"

#include <stdbool.h>
#include "app_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "vibration_control.h"

static const char *TAG = "pomodoro_service";
static struct {
    pomodoro_state_t state;
    uint32_t remaining_s;
    int64_t deadline_us;
    lv_timer_t *tick;
    bool initialized;
} s_pomodoro_ctx = {
    .state = POMODORO_READY,
    .remaining_s = POMODORO_DURATION_S,
};

static void stop_tick(void)
{
    if (s_pomodoro_ctx.tick) {
        lv_timer_delete(s_pomodoro_ctx.tick);
        s_pomodoro_ctx.tick = NULL;
    }
}

static void refresh(void)
{
    if (s_pomodoro_ctx.state != POMODORO_RUNNING) return;
    int64_t left_us = s_pomodoro_ctx.deadline_us - esp_timer_get_time();
    if (left_us <= 0) {
        s_pomodoro_ctx.remaining_s = 0;
        s_pomodoro_ctx.state = POMODORO_FINISHED;
        stop_tick();
        ESP_LOGI(TAG, "session finished");
        vibration_run_pattern(VIBRATION_PATTERN_ALARM);
        app_event_fire(APP_EVENT_POMODORO_FINISHED, NULL);
        return;
    }
    s_pomodoro_ctx.remaining_s = (uint32_t)((left_us + 999999) / 1000000);
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    refresh();
}

void pomodoro_service_init(void)
{
    if (s_pomodoro_ctx.initialized) return;
    s_pomodoro_ctx.initialized = true;
    s_pomodoro_ctx.state = POMODORO_READY;
    s_pomodoro_ctx.remaining_s = POMODORO_DURATION_S;
}

void pomodoro_service_toggle(void)
{
    pomodoro_service_init();
    refresh();
    if (s_pomodoro_ctx.state == POMODORO_RUNNING) {
        s_pomodoro_ctx.state = POMODORO_PAUSED;
        stop_tick();
        return;
    }
    if (s_pomodoro_ctx.state == POMODORO_FINISHED || s_pomodoro_ctx.remaining_s == 0)
        s_pomodoro_ctx.remaining_s = POMODORO_DURATION_S;
    s_pomodoro_ctx.deadline_us = esp_timer_get_time() + (int64_t)s_pomodoro_ctx.remaining_s * 1000000;
    s_pomodoro_ctx.state = POMODORO_RUNNING;
    if (!s_pomodoro_ctx.tick) s_pomodoro_ctx.tick = lv_timer_create(tick, 250, NULL);
    if (!s_pomodoro_ctx.tick) {
        s_pomodoro_ctx.state = POMODORO_PAUSED;
        ESP_LOGE(TAG, "could not create timer");
    }
}

void pomodoro_service_reset(void)
{
    pomodoro_service_init();
    stop_tick();
    s_pomodoro_ctx.state = POMODORO_READY;
    s_pomodoro_ctx.remaining_s = POMODORO_DURATION_S;
    s_pomodoro_ctx.deadline_us = 0;
}

pomodoro_state_t pomodoro_service_state(void)
{
    refresh();
    return s_pomodoro_ctx.state;
}

uint32_t pomodoro_service_remaining_s(void)
{
    refresh();
    return s_pomodoro_ctx.remaining_s;
}
