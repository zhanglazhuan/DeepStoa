// debug/t_lvgl/main/t_lvgl.c
// Custom tab widget test — uses lv_tab from uilv/widgets

#define FMT_ROW_MAJOR 0

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "deepstoa_v1.h"
#include "aw9523.h"
#include "epd_display.h"
#include "ft6336.h"
#include "lvgl.h"
#include "lv_tab.h"

static const char *TAG = "t_lvgl";

#define LCD_W 480
#define LCD_H 800
#define EPD_BUF_SIZE  (LCD_W * LCD_H / 8)
#define LVGL_BUF_SIZE (LCD_W * LCD_H * sizeof(lv_color_t) / 10)

#define PANEL_OFFSET_X  0
#define PANEL_OFFSET_Y  0
#define LVGL_BUF_OFFSET  8

static uint8_t *s_epd_buf = NULL;
static lv_display_t *s_disp = NULL;
static lv_obj_t *s_bench_label;
static unsigned s_frame;
static int64_t s_convert_us;

/* A timeout is a failed measurement, never a measured panel refresh rate. */
static bool wait_idle(void)
{
    int64_t deadline = esp_timer_get_time() + 10000000;
    while (epd_is_busy()) {
        if (esp_timer_get_time() >= deadline) return false;
        vTaskDelay(1);
    }
    return true;
}

// ── LVGL flush ──────────────────────────────────────────────────────

static void lvgl_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map)
{
    int64_t convert_start = esp_timer_get_time();
    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);
    uint8_t *buf_ptr = px_map + LVGL_BUF_OFFSET;
    uint32_t src_stride = (w + 7) / 8;

    for (int y = 0; y < h; y++) {
        int32_t screen_y = area->y1 + y;
        for (int x = 0; x < w; x++) {
            int32_t screen_x = area->x1 + x;
            int32_t hw_x = screen_x + PANEL_OFFSET_X;
            int32_t hw_y = screen_y + PANEL_OFFSET_Y;
            if (hw_x < 0 || hw_x >= LCD_W || hw_y < 0 || hw_y >= LCD_H) continue;

            uint32_t src_byte = y * src_stride + (x / 8);
            uint8_t  src_bit  = 7 - (x % 8);
            uint8_t  pixel    = (buf_ptr[src_byte] >> src_bit) & 0x01;

#if FMT_ROW_MAJOR
            uint32_t dst_byte = hw_y * (LCD_W / 8) + (hw_x / 8);
            uint8_t  dst_bit  = 7 - (hw_x % 8);
#else
            uint32_t dst_byte = hw_x * (LCD_H / 8) + (hw_y / 8);
            uint8_t  dst_bit  = 7 - (hw_y % 8);
#endif
            if (pixel) s_epd_buf[dst_byte] |= (1 << dst_bit);
            else       s_epd_buf[dst_byte] &= ~(1 << dst_bit);
        }
    }

    s_convert_us += esp_timer_get_time() - convert_start;
    if (lv_display_flush_is_last(d)) {
        int64_t start = esp_timer_get_time();
        bool pre_ready = wait_idle();
        int64_t tx_start = esp_timer_get_time();
        /* Preserve the debug driver's timeout-and-continue diagnostic behavior. */
        EPD_Dis_PartAll_Async(s_epd_buf);
        int64_t tx_end = esp_timer_get_time();
        bool saw_high = false, released = false;
        /* Allow BUSY assertion after the refresh command before accepting idle. */
        while (esp_timer_get_time() - tx_end < 10000000) {
            bool busy = epd_is_busy();
            saw_high |= busy;
            if (saw_high && !busy) { released = true; break; }
            vTaskDelay(1);
        }
        int64_t end = esp_timer_get_time();
        bool valid = pre_ready && released;
        ESP_LOGI(TAG, "[refresh] frame=%u convert=%.2f ms pre_wait=%.2f ms "
                 "submit=%.2f ms post_wait=%.2f ms total=%.2f ms "
                 "pre_ready=%d busy_seen=%d released=%d valid=%d",
                 ++s_frame, s_convert_us / 1000.0, (tx_start-start) / 1000.0,
                 (tx_end-tx_start) / 1000.0, (end-tx_end) / 1000.0,
                 (end-start+s_convert_us) / 1000.0,
                 pre_ready, saw_high, released, valid);
        if (valid) ESP_LOGI(TAG, "[refresh] transfer+panel rate=%.3f Hz",
                           1000000.0 / (end-tx_start));
        else ESP_LOGW(TAG, "[refresh] panel rate unavailable: BUSY handshake invalid");
        s_convert_us = 0;
    }
    lv_display_flush_ready(d);
}

// ── LVGL rounder ────────────────────────────────────────────────────

static void lvgl_rounder_cb(lv_event_t *e)
{
    lv_area_t *area = lv_event_get_param(e);
    area->x1 = area->x1 & ~0x07;
    area->x2 = area->x2 | 0x07;
    area->y1 = area->y1 & ~0x07;
    area->y2 = area->y2 | 0x07;
}

// ── LVGL tick ───────────────────────────────────────────────────────

static uint32_t lvgl_now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// ── Touch ───────────────────────────────────────────────────────────

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static ft6336_touch_data_t td;
    if (ft6336_read(&td) == ESP_OK && td.count > 0) {
        data->point.x = td.points[0].x;
        data->point.y = td.points[0].y;
        data->state = (td.points[0].event != FT6336_EVENT_UP)
                      ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void touch_init(void)
{
    aw9523_set_pin(DEEPV1_AW_PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    aw9523_set_pin(DEEPV1_AW_PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(350));

    esp_err_t ret = ft6336_init_shared(aw9523_get_i2c_bus(), GPIO_NUM_NC,
                                       DEEPV1_TOUCH_I2C_ADDR);
    if (ret != ESP_OK) { ESP_LOGW(TAG, "Touch init failed (0x%X)", ret); return; }
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_group(indev, lv_group_create());
    ESP_LOGI(TAG, "Touch ready");
}

// ── UI ──────────────────────────────────────────────────────────────

static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_pad_all(scr, 0, 0);

    static const char *names[] = {"Alarm", "Timer", "Settings"};
    int count = sizeof(names) / sizeof(names[0]);

    lv_tab_t *tab = lv_tab_create(scr, names, count, 36);

    for (int i = 0; i < count; i++) {
        lv_obj_t *page = lv_tab_add_page(tab);
        lv_obj_t *lbl  = lv_label_create(page);
        lv_label_set_text_fmt(lbl, "This is %s page", names[i]);
        lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
        lv_obj_center(lbl);
    }

    lv_tab_finalize(tab);
    s_bench_label = lv_label_create(lv_layer_top());
    lv_obj_align(s_bench_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_text_color(s_bench_label, lv_color_black(), 0);
    lv_obj_set_style_bg_color(s_bench_label, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_bench_label, LV_OPA_COVER, 0);
    lv_label_set_text(s_bench_label, "Refresh benchmark");
}

// ── Main ────────────────────────────────────────────────────────────

void app_main(void)
{
    ESP_LOGI(TAG, "=== t_lvgl: lv_tab Test ===");
    /* Match production's approximate display startup time for comparison. */
    vTaskDelay(pdMS_TO_TICKS(2200));

    // Init EPD
    int64_t init_start = esp_timer_get_time();
    epd_gpio_config();
    EPD_HW_Init();
    ESP_LOGI(TAG, "[refresh] hardware init=%.2f ms", (esp_timer_get_time()-init_start)/1000.0);
    int64_t clear_start = esp_timer_get_time();
    EPD_WhiteScreen_White();
    ESP_LOGI(TAG, "[refresh] initial clear=%.2f ms", (esp_timer_get_time()-clear_start)/1000.0);
    s_epd_buf = malloc(EPD_BUF_SIZE);
    assert(s_epd_buf);
    memset(s_epd_buf, 0xFF, EPD_BUF_SIZE);

    // Init LVGL
    lv_init();
    s_disp = lv_display_create(LCD_W, LCD_H);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_I1);
    static uint8_t lvgl_buf[LVGL_BUF_SIZE] __attribute__((aligned(4)));
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);
    lv_display_add_event_cb(s_disp, lvgl_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_display_set_buffers(s_disp, lvgl_buf, NULL, LVGL_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_default(s_disp);
    lv_tick_set_cb(lvgl_now_ms);

    // Init touch
    touch_init();

    // Build UI
    build_ui();

    ESP_LOGI(TAG, "[refresh] 5 full-frame partial updates; submit includes SPI + AW9523 control, not panel waveform");
    for (unsigned i = 1; i <= 5; ++i) {
        lv_label_set_text_fmt(s_bench_label, "Refresh benchmark %u / 5", i);
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(s_disp);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    ESP_LOGI(TAG, "=== Benchmark done; touch tabs to measure further updates ===");
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
