#include "page_pomodoro_timer.h"

#include <cmath>
#include <cstdio>

#include "esp_log.h"
#include "esp_timer.h"

#include "button_bsp.h"
#include "epaper_port.h"
#include "GUI_Paint.h"
#include "qmi8658_bsp.h"

extern uint8_t *Image_Mono;
extern SemaphoreHandle_t qmi8658_mutex;

#define POMODORO_TOTAL_SECONDS (25 * 60)

static bool pomodoro_is_portrait() {
    float acc[3] = {0};
    float gyro[3] = {0};
    static bool last_portrait = true;
    static bool last_candidate = true;
    static int stable_samples = 0;
    const float kSwitchThreshold = 300.0f;
    const int kStableSamples = 3;

    if (qmi8658_mutex) {
        xSemaphoreTake(qmi8658_mutex, portMAX_DELAY);
        QMI8658_read_xyz(acc, gyro, NULL);
        xSemaphoreGive(qmi8658_mutex);
    } else {
        QMI8658_read_xyz(acc, gyro, NULL);
    }

    float ax = fabsf(acc[0]);
    float ay = fabsf(acc[1]);
    bool candidate = last_portrait;

    if (ax > ay + kSwitchThreshold) {
        candidate = true;
    } else if (ay > ax + kSwitchThreshold) {
        candidate = false;
    } else {
        stable_samples = 0;
        last_candidate = candidate;
        return last_portrait;
    }

    if (candidate == last_candidate) {
        stable_samples++;
    } else {
        last_candidate = candidate;
        stable_samples = 1;
    }

    if (stable_samples >= kStableSamples) {
        last_portrait = candidate;
        stable_samples = 0;
    }

    return last_portrait;
}

static void pomodoro_draw(int remaining_seconds, bool paused, bool portrait, int refresh_mode) {
    UWORD rotate = portrait ? ROTATE_270 : ROTATE_0;
    const UWORD draw_width = (rotate == ROTATE_0 || rotate == ROTATE_180) ? EPD_WIDTH : EPD_HEIGHT;
    const UWORD draw_height = (rotate == ROTATE_0 || rotate == ROTATE_180) ? EPD_HEIGHT : EPD_WIDTH;

    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, rotate, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);

    int minutes = remaining_seconds / 60;
    int seconds = remaining_seconds % 60;
    if (minutes < 0) minutes = 0;
    if (minutes > 99) minutes = 99;
    if (seconds < 0) seconds = 0;
    if (seconds > 59) seconds = 59;
    char min_str[4] = {0};
    char sec_str[4] = {0};
    snprintf(min_str, sizeof(min_str), "%02d", minutes);
    snprintf(sec_str, sizeof(sec_str), "%02d", seconds);

    const sFONT *big_font = &Font182;
    int total_width = big_font->Width * 5;
    if (total_width + 40 > draw_width) {
        big_font = &Font80;
    }

    int group_width = big_font->Width * 2;
    int colon_width = big_font->Width;
    int total = group_width * 2 + colon_width;
    int start_x = (draw_width - total) / 2;
    int start_y = (draw_height - big_font->Height) / 2;

    Paint_DrawString_EN(start_x, start_y, min_str, (sFONT*)big_font, WHITE, BLACK);
    Paint_DrawString_EN(start_x + group_width, start_y, ":", (sFONT*)big_font, WHITE, BLACK);
    Paint_DrawString_EN(start_x + group_width + colon_width, start_y, sec_str, (sFONT*)big_font, WHITE, BLACK);

    if (paused) {
        Paint_DrawString_CN(10, 10, "暂停", &Font24_UTF8, WHITE, BLACK);
    }

    Paint_DrawString_CN(10, draw_height - 30, "单击:暂停/继续  长按:重置  双击:返回", &Font16_UTF8, WHITE, BLACK);

    if (refresh_mode == Global_refresh) {
        EPD_Display_Base(Image_Mono);
    } else {
        EPD_Display_Partial(Image_Mono, 0, 0, EPD_WIDTH, EPD_HEIGHT);
    }
}

void page_pomodoro_timer_show(void) {
    int remaining_seconds = POMODORO_TOTAL_SECONDS;
    bool paused = false;
    bool portrait = pomodoro_is_portrait();
    bool force_refresh = true;

    int64_t last_tick_us = esp_timer_get_time();
    int last_remaining = remaining_seconds;

    pomodoro_draw(remaining_seconds, paused, portrait, Global_refresh);

    while (1) {
        int button = wait_key_event_and_return_code(pdMS_TO_TICKS(200));

        if (button == 7) {
            paused = !paused;
            force_refresh = true;
        } else if (button == 12) {
            remaining_seconds = POMODORO_TOTAL_SECONDS;
            paused = false;
            last_tick_us = esp_timer_get_time();
            force_refresh = true;
        } else if (button == 8 || button == 22) {
            return;
        }

        bool current_portrait = pomodoro_is_portrait();
        if (current_portrait != portrait) {
            portrait = current_portrait;
            force_refresh = true;
        }

        int64_t now_us = esp_timer_get_time();
        if (!paused && remaining_seconds > 0) {
            while (now_us - last_tick_us >= 1000000) {
                remaining_seconds--;
                last_tick_us += 1000000;
                if (remaining_seconds <= 0) {
                    remaining_seconds = 0;
                    paused = true;
                    break;
                }
            }
        } else {
            last_tick_us = now_us;
        }

        if (force_refresh || remaining_seconds != last_remaining) {
            pomodoro_draw(remaining_seconds, paused, portrait, Partial_refresh);
            last_remaining = remaining_seconds;
            force_refresh = false;
        }
    }
}
