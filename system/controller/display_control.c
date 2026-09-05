// system/controller/display_control.c
// E-ink display controller — LVGL + GDEM0397T81P driver
// Ported from D:\Codes\EPOS\epos\drivers\epd_display_control.c
//
// Key adaptations: Zephyr work → FreeRTOS tasks, Zephyr PSRAM → ESP-IDF heap_caps PSRAM

#include <string.h>
#include <sys/param.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include <lvgl.h>

#include "Display_EPD_W21.h"
#include "display_control.h"
#include "display_refresh_policy.h"

static const char *TAG = "display_ctrl";

lv_display_t *disp = NULL;

epd_context_t epd_ctx = {
    .epd_buffer = NULL,
    .epd_buffer_4g = NULL,
    .orientation = EPD_ORIENT_PORTRAIT,
    .flush_mode = EPD_FLUSH_MODE_PARTIAL_ALL,
    .is_first_flush_in_cycle = true,
    .state = DISPLAY_STATE_SLEEPING,
    .is_fast_lut_loaded = false,
    .need_hw_sync_after_wakeup = true,
    .need_sync_base = true,
    .next_frame_flush_mode = false,
    .requested_flush_mode = EPD_FLUSH_MODE_PARTIAL_ALL,
    .transition_frame_ready = false,
    .is_4g_lut_loaded = false,
};

/* 外部 RAM 做 DMA 时的对齐要求（cache line）。SPI 主机要求地址和长度
 * 都满足，否则会退回内部反弹缓冲，见 epd_alloc_frame()。 */
#define EPD_DMA_ALIGN  64

/* LVGL 渲染缓冲。
 *
 * 原来写的是 LCD_H_RES*LCD_V_RES*sizeof(lv_color_t)/10 = 76800 字节 —— 那是
 * 按 16bpp 算的，可屏幕是 LV_COLOR_FORMAT_I1（1bpp），整屏才 48000 字节。
 * 等于这个"局部"缓冲比整个 framebuffer 还大 28800 字节，而且它是 .bss 里的
 * 静态数组，常驻内部 RAM 拿不回来。内部堆总共只有 ~162KB、长期 84% 占用，
 * 这是账面上最大的一笔无谓开销。
 *
 * 尺寸怎么定：lv_display_set_buffers() 里 PARTIAL 模式是 h = buf_size / stride，
 * I1 的 stride = LCD_H_RES/8 = 60。所以余量必须**小于一个 stride**，否则会被
 * h 多算一行重新吃掉，等于白留。这里留 56 字节给 I1 调色板和对齐。
 *
 * 块数只影响 flush_cb 被调用几次（每次的像素总量不变），面板更新仍然只有
 * 一次 —— 见 lvgl_flush_cb 里靠 is_last 才通知 flush_task。 */
#define EPD_I1_STRIDE    (LCD_H_RES / 8)          /* 1bpp：60 字节/行 */
#define DRAW_BUF_ROWS    100                       /* 1/8 屏 */
#define DRAW_BUF_SIZE    (DRAW_BUF_ROWS * EPD_I1_STRIDE + 56)
#define DST_BYTES_PER_COL  (LCD_V_RES / 8)  // 100
#define LVGL_TICK_PERIOD_MS  50

// Ported from Zephyr epd_display_control.h: LVGL buffer byte offset
#define LVGL_BUF_OFFSET  8
// Physical panel offset — tune to center the image on your GDEM0397T81P unit
// Positive X → shift RIGHT, Negative X → shift LEFT
#define PANEL_OFFSET_X  0
#define PANEL_OFFSET_Y  0

// ── FreeRTOS primitives (replacing Zephyr work queues) ─────────────────

static TimerHandle_t s_lvgl_tick_timer = NULL;
static TaskHandle_t s_flush_task = NULL;
static SemaphoreHandle_t s_display_mutex = NULL;
static volatile bool s_flush_pending = false;
/* Hardware owns these snapshots while LVGL prepares the next frame. */
static uint8_t *s_hw_buffer = NULL;
static uint8_t *s_hw_buffer_4g = NULL;
// ── Forward declarations ───────────────────────────────────────────────

static void lvgl_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map);
static void lvgl_rounder_cb(lv_event_t *e);
static void flush_task(void *arg);
static void lvgl_tick_cb(TimerHandle_t t);

static bool map_screen_point_to_panel(int32_t screen_x, int32_t screen_y,
                                      int32_t *panel_x, int32_t *panel_y)
{
    int32_t px = screen_x;
    int32_t py = screen_y;

    switch (epd_ctx.orientation) {
    case EPD_ORIENT_LANDSCAPE:
        px = LCD_H_RES - 1 - screen_y;
        py = screen_x;
        break;
    case EPD_ORIENT_PORTRAIT_INVERTED:
        px = LCD_H_RES - 1 - screen_x;
        py = LCD_V_RES - 1 - screen_y;
        break;
    case EPD_ORIENT_LANDSCAPE_INVERTED:
        px = screen_y;
        py = LCD_V_RES - 1 - screen_x;
        break;
    default:
        break;
    }

    px += PANEL_OFFSET_X;
    py += PANEL_OFFSET_Y;
    if (px < 0 || px >= LCD_H_RES || py < 0 || py >= LCD_V_RES) return false;

    *panel_x = px;
    *panel_y = py;
    return true;
}

static bool map_screen_area_to_panel(const epd_refresh_rect_t *screen,
                                     lv_area_t *panel)
{
    const int32_t sx[4] = {screen->x1, screen->x2, screen->x1, screen->x2};
    const int32_t sy[4] = {screen->y1, screen->y1, screen->y2, screen->y2};
    int32_t min_x = LCD_H_RES;
    int32_t min_y = LCD_V_RES;
    int32_t max_x = -1;
    int32_t max_y = -1;

    for (size_t i = 0; i < 4; i++) {
        int32_t px;
        int32_t py;
        if (!map_screen_point_to_panel(sx[i], sy[i], &px, &py)) continue;
        min_x = MIN(min_x, px);
        min_y = MIN(min_y, py);
        max_x = MAX(max_x, px);
        max_y = MAX(max_y, py);
    }

    if (max_x < min_x || max_y < min_y) return false;
    panel->x1 = min_x;
    panel->y1 = min_y;
    panel->x2 = max_x;
    panel->y2 = max_y;
    return true;
}

// ── Hardware flush background task ─────────────────────────────────────

static void flush_task(void *arg)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // wait for signal
        if (!disp) continue;

        while (epd_is_busy()) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        /* Take an immutable frame snapshot before touching the panel. */
        xSemaphoreTake(s_display_mutex, portMAX_DELAY);
        if (epd_ctx.is_first_flush_in_cycle) {
            /* Notifications raised before the previous snapshot can remain
             * counted while the panel is BUSY.  They no longer represent a
             * dirty frame, so never replay the stale dirty rectangle. */
            xSemaphoreGive(s_display_mutex);
            continue;
        }
        lv_area_t area = epd_ctx.dirty_area;
        epd_flush_mode_t mode = epd_ctx.flush_mode;
        bool sync_after_wakeup = epd_ctx.need_hw_sync_after_wakeup;
        bool transition_frame = epd_ctx.transition_frame_ready;
        epd_flush_mode_t transition_target = epd_ctx.requested_flush_mode;
        epd_ctx.is_first_flush_in_cycle = true;
        epd_ctx.need_sync_base = false;
        if (transition_frame) {
            /* This snapshot now owns the armed transition.  Clear the shared
             * state here so a later request cannot be consumed when this
             * (potentially slow) hardware update completes. */
            epd_ctx.transition_frame_ready = false;
            epd_ctx.next_frame_flush_mode = false;
            epd_ctx.flush_mode = transition_target;
        }
        if (s_hw_buffer) memcpy(s_hw_buffer, epd_ctx.epd_buffer, EPD_ARRAY);
        if (s_hw_buffer_4g) memcpy(s_hw_buffer_4g, epd_ctx.epd_buffer_4g, EPD_ARRAY);
        xSemaphoreGive(s_display_mutex);

        const uint8_t *frame = s_hw_buffer ? s_hw_buffer : epd_ctx.epd_buffer;
        const uint8_t *frame_4g = s_hw_buffer_4g ? s_hw_buffer_4g : epd_ctx.epd_buffer_4g;
        switch (mode) {
        case EPD_FLUSH_MODE_FULL:
            EPD_Dis_PartAll(frame);
            break;
        case EPD_FLUSH_MODE_FAST:
            EPD_WhiteScreen_ALL_Fast(frame);
            break;
        case EPD_FLUSH_MODE_PARTIAL_ALL:
            EPD_Dis_PartAll_Async(frame);
            break;
        case EPD_FLUSH_MODE_PARTIAL_WIN:
            if (sync_after_wakeup) {
                EPD_Dis_Part_Window_Activate();
                xSemaphoreTake(s_display_mutex, portMAX_DELAY);
                epd_ctx.need_hw_sync_after_wakeup = false;
                xSemaphoreGive(s_display_mutex);
            }
            EPD_Dis_Part_Window_Async(area.y1, area.x1,
                                      area.y2, area.x2,
                                      frame, LCD_V_RES / 8);
            break;
        case EPD_FLUSH_MODE_4G:
            if (!epd_ctx.is_4g_lut_loaded) {
                EPD_HW_Init_4G();
                epd_ctx.is_4g_lut_loaded = true;
                epd_ctx.is_fast_lut_loaded = false;
            }
            EPD_Update_4Gray_WithBuffers(frame, frame_4g);
            break;
        }

        if (transition_frame) {
            while (epd_is_busy()) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            /* The physical panel now matches this immutable snapshot.  Use
             * the same frame as the differential-update base; the live LVGL
             * buffer may already contain later keystrokes. */
            EPD_Sync_Base_Map(frame);
        }
    }
}

// ── LVGL tick ──────────────────────────────────────────────────────────

static void lvgl_tick_cb(TimerHandle_t t)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

// ── LVGL rounder (8-pixel boundary alignment) ──────────────────────────

static void lvgl_rounder_cb(lv_event_t *e)
{
    lv_area_t *area = lv_event_get_param(e);
    area->x1 = area->x1 & ~0x07;
    area->x2 = area->x2 | 0x07;
    area->y1 = area->y1 & ~0x07;
    area->y2 = area->y2 | 0x07;
}

// ── LVGL flush callback (column-major pixel copy) ──────────────────────

static void lvgl_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map)
{
    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);

    // Ported from Zephyr: skip LVGL buffer alignment bytes
    uint8_t *buf_ptr = (uint8_t *)px_map + LVGL_BUF_OFFSET;
    uint32_t src_stride = (w + 7) / 8;

    int32_t min_px = 9999, min_py = 9999, max_px = -1, max_py = -1;
    bool has_valid = false;
    bool is_last = lv_display_flush_is_last(d);

    xSemaphoreTake(s_display_mutex, portMAX_DELAY);

    /* A next-frame mode request belongs to the complete screen invalidation
     * issued by keyboard/form code.  Do not apply it in the setter: a queued
     * textarea notification may still be waiting behind an EPD BUSY period
     * and would otherwise consume the request before the keyboard is drawn. */
    bool is_full_screen = area->x1 <= 0 && area->y1 <= 0 &&
                          area->x2 >= (int32_t)epd_ctx.screen_w - 1 &&
                          area->y2 >= (int32_t)epd_ctx.screen_h - 1;
    if (epd_ctx.next_frame_flush_mode && is_full_screen) {
        /* Establish the static keyboard with the much faster whole-frame
         * transport.  The hardware task switches to the requested window
         * mode immediately after taking this immutable snapshot. */
        epd_ctx.flush_mode =
            epd_ctx.requested_flush_mode == EPD_FLUSH_MODE_PARTIAL_WIN
                ? EPD_FLUSH_MODE_PARTIAL_ALL
                : epd_ctx.requested_flush_mode;
        epd_ctx.need_sync_base =
            epd_ctx.requested_flush_mode == EPD_FLUSH_MODE_PARTIAL_WIN;
        epd_ctx.transition_frame_ready = true;
    }

    int32_t start_x = 0;
    int32_t start_y = 0;
    int32_t end_x = w - 1;
    int32_t end_y = h - 1;
    bool limit_to_refresh_area =
        epd_ctx.flush_mode == EPD_FLUSH_MODE_PARTIAL_WIN &&
        epd_ctx.has_refresh_area && !epd_ctx.need_sync_base;

    if (limit_to_refresh_area) {
        epd_refresh_rect_t source = {
            .x1 = area->x1, .y1 = area->y1,
            .x2 = area->x2, .y2 = area->y2,
        };
        epd_refresh_rect_t input = {
            .x1 = epd_ctx.refresh_area.x1, .y1 = epd_ctx.refresh_area.y1,
            .x2 = epd_ctx.refresh_area.x2, .y2 = epd_ctx.refresh_area.y2,
        };
        epd_refresh_rect_t scan;
        if (epd_refresh_rect_intersect(&source, &input, &scan)) {
            start_x = scan.x1 - area->x1;
            start_y = scan.y1 - area->y1;
            end_x = scan.x2 - area->x1;
            end_y = scan.y2 - area->y1;
        } else {
            start_x = 1;
            end_x = 0;
            start_y = 1;
            end_y = 0;
        }
    }

    for (int y = start_y; y <= end_y; y++) {
        int32_t screen_y = area->y1 + y;
        for (int x = start_x; x <= end_x; x++) {
            int32_t screen_x = area->x1 + x;

            uint32_t src_byte = y * src_stride + (x / 8);
            uint8_t  src_bit  = 7 - (x % 8);
            uint8_t  pixel    = (buf_ptr[src_byte] >> src_bit) & 0x01;

            int32_t px;
            int32_t py;
            if (!map_screen_point_to_panel(screen_x, screen_y, &px, &py)) continue;

            // Column-major: byte = x * (height/8) + (y/8)
            uint32_t dst_byte = px * DST_BYTES_PER_COL + (py / 8);
            uint8_t  dst_bit  = 7 - (py % 8);
            uint8_t  dst_mask = (uint8_t)(1U << dst_bit);

            /* LVGL can redraw a focused textarea even when its pixels did not
             * change.  In differential window mode those redraws must not
             * become physical EPD updates.  The first base-sync frame is
             * intentionally exempt so the panel can be established. */
            if (epd_ctx.flush_mode == EPD_FLUSH_MODE_PARTIAL_WIN &&
                !epd_ctx.need_sync_base) {
                bool current_pixel = (epd_ctx.epd_buffer[dst_byte] & dst_mask) != 0;
                if (current_pixel == (pixel != 0)) {
                    continue;
                }
            }

            min_px = MIN(min_px, px); min_py = MIN(min_py, py);
            max_px = MAX(max_px, px); max_py = MAX(max_py, py);
            has_valid = true;

            if (pixel)
                epd_ctx.epd_buffer[dst_byte] |= dst_mask;
            else
                epd_ctx.epd_buffer[dst_byte] &= (uint8_t)~dst_mask;
        }
    }

    if (!has_valid) {
        if (is_last && !epd_ctx.is_first_flush_in_cycle) {
            xTaskNotifyGive(s_flush_task);
        }
        xSemaphoreGive(s_display_mutex);
        lv_display_flush_ready(d);
        return;
    }

    if (limit_to_refresh_area) {
        epd_refresh_rect_t changed = {
            .x1 = area->x1 + start_x,
            .y1 = area->y1 + start_y,
            .x2 = area->x1 + end_x,
            .y2 = area->y1 + end_y,
        };
        epd_refresh_rect_t input = {
            .x1 = epd_ctx.refresh_area.x1, .y1 = epd_ctx.refresh_area.y1,
            .x2 = epd_ctx.refresh_area.x2, .y2 = epd_ctx.refresh_area.y2,
        };
        epd_refresh_rect_t stable_window;
        lv_area_t panel_window;
        if (epd_refresh_choose_window(&changed, true, &input, &stable_window) &&
            map_screen_area_to_panel(&stable_window, &panel_window)) {
            min_px = panel_window.x1;
            min_py = panel_window.y1;
            max_px = panel_window.x2;
            max_py = panel_window.y2;
        }
    }

    if (epd_ctx.is_first_flush_in_cycle) {
        epd_ctx.dirty_area.x1 = min_px; epd_ctx.dirty_area.y1 = min_py;
        epd_ctx.dirty_area.x2 = max_px; epd_ctx.dirty_area.y2 = max_py;
        epd_ctx.is_first_flush_in_cycle = false;
    } else {
        epd_ctx.dirty_area.x1 = MIN(epd_ctx.dirty_area.x1, min_px);
        epd_ctx.dirty_area.y1 = MIN(epd_ctx.dirty_area.y1, min_py);
        epd_ctx.dirty_area.x2 = MAX(epd_ctx.dirty_area.x2, max_px);
        epd_ctx.dirty_area.y2 = MAX(epd_ctx.dirty_area.y2, max_py);
    }

    if (is_last) {
        xTaskNotifyGive(s_flush_task);
    }
    xSemaphoreGive(s_display_mutex);
    lv_display_flush_ready(d);
}

/* EPD 帧缓冲的分配。
 *
 * 64 字节对齐不是可有可无的：SPI 主机只有在 (地址 | 长度) 都对齐到外部 DMA
 * 对齐要求时，才肯直接从 PSRAM 做 DMA（spi_master.c:1201 的
 * `need_malloc |= ((buffer|len) & (alignment-1))`）。不对齐它就退回去现场
 * 申请一块**内部** DMA 反弹缓冲再 memcpy —— 内部 RAM 一紧张就会看到
 * "spi_master: Failed to allocate priv TX buffer"，刷屏直接失败。
 *
 * 注意 PSRAM 堆没有 MALLOC_CAP_DMA（那个 cap 只挂在内部 DRAM 区上），
 * 所以这里要的是 SPIRAM|8BIT + 对齐，而不是 SPIRAM|DMA —— 后者分配不出来。
 * 能不能被 DMA 由 esp_ptr_dma_ext_capable() 判断，ESP32-S3 上 PSRAM 地址恒为真。
 */
static uint8_t *epd_alloc_frame(void)
{
    uint8_t *p = heap_caps_aligned_alloc(EPD_DMA_ALIGN, EPD_ARRAY,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;

    /* 没有 PSRAM（或已用尽）时退回内部 DRAM。内部缓冲本来就是 DMA 可用的，
     * 不需要反弹，对齐与否都不影响，走原来的路径即可。 */
    ESP_LOGW(TAG, "PSRAM frame alloc failed, falling back to internal DRAM");
    return (uint8_t *)heap_caps_malloc(EPD_ARRAY, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
}

// ── Public API ─────────────────────────────────────────────────────────

void display_control_init(void)
{
    epd_gpio_config();
    EPD_HW_Init();
    EPD_WhiteScreen_White();

    // Allocate EPD framebuffers: prefer PSRAM, fall back to internal DRAM
    epd_ctx.epd_buffer = (uint8_t *)epd_alloc_frame();
    if (!epd_ctx.epd_buffer) { ESP_LOGE(TAG, "framebuffer allocation failed"); return; }
    memset(epd_ctx.epd_buffer, 0xFF, EPD_ARRAY);

    epd_ctx.epd_buffer_4g = (uint8_t *)epd_alloc_frame();
    if (!epd_ctx.epd_buffer_4g) {
        ESP_LOGE(TAG, "4-gray framebuffer allocation failed");
        heap_caps_free(epd_ctx.epd_buffer); epd_ctx.epd_buffer = NULL;
        return;
    }
    memset(epd_ctx.epd_buffer_4g, 0, EPD_ARRAY);

    s_hw_buffer = epd_alloc_frame();
    s_hw_buffer_4g = epd_alloc_frame();
    if (!s_hw_buffer || !s_hw_buffer_4g) {
        ESP_LOGE(TAG, "Failed to allocate hardware frame snapshots");
        heap_caps_free(s_hw_buffer); s_hw_buffer = NULL;
        heap_caps_free(s_hw_buffer_4g); s_hw_buffer_4g = NULL;
        heap_caps_free(epd_ctx.epd_buffer); epd_ctx.epd_buffer = NULL;
        heap_caps_free(epd_ctx.epd_buffer_4g); epd_ctx.epd_buffer_4g = NULL;
        return;
    }
    memset(s_hw_buffer, 0xFF, EPD_ARRAY);
    memset(s_hw_buffer_4g, 0, EPD_ARRAY);

    epd_ctx.screen_w = LCD_H_RES;
    epd_ctx.screen_h = LCD_V_RES;

    disp = lv_display_create(epd_ctx.screen_w, epd_ctx.screen_h);
    if (!disp) {
        ESP_LOGE(TAG, "LVGL display allocation failed");
        heap_caps_free(epd_ctx.epd_buffer); epd_ctx.epd_buffer = NULL;
        heap_caps_free(epd_ctx.epd_buffer_4g); epd_ctx.epd_buffer_4g = NULL;
        return;
    }

    // Monochrome (1bpp) — the flush callback treats px_map as packed 1-bit data
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);

    /* lv_display_set_buffers() 会断言 buf 按 LV_DRAW_BUF_ALIGN(4) 对齐。
     * uint8_t 数组默认只保证 1 字节对齐，显式标注免得换编译器/改大小时踩到。 */
    static uint8_t buf1[DRAW_BUF_SIZE] __attribute__((aligned(4)));
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_add_event_cb(disp, lvgl_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_display_set_buffers(disp, buf1, NULL, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_default(disp);

    epd_ctx.state = DISPLAY_STATE_AWAKE;
    epd_ctx.flush_mode = EPD_FLUSH_MODE_PARTIAL_ALL;

    // Create mutex
    s_display_mutex = xSemaphoreCreateMutex();

    // Create flush background task
    xTaskCreate(flush_task, "epd_flush", 4096, NULL, 5, &s_flush_task);

    // Start LVGL tick timer
    s_lvgl_tick_timer = xTimerCreate("lvgl_tick",
                                     pdMS_TO_TICKS(LVGL_TICK_PERIOD_MS),
                                     pdTRUE, NULL, lvgl_tick_cb);
    xTimerStart(s_lvgl_tick_timer, 0);

    ESP_LOGI(TAG, "Display controller initialized (%d×%d)", epd_ctx.screen_w, epd_ctx.screen_h);
}

void display_control_deinit(void)
{
    if (s_flush_task) { vTaskDelete(s_flush_task); s_flush_task = NULL; }
    if (s_lvgl_tick_timer) { xTimerDelete(s_lvgl_tick_timer, 0); s_lvgl_tick_timer = NULL; }
    if (s_display_mutex) { vSemaphoreDelete(s_display_mutex); s_display_mutex = NULL; }
    heap_caps_free(epd_ctx.epd_buffer); epd_ctx.epd_buffer = NULL;
    heap_caps_free(epd_ctx.epd_buffer_4g); epd_ctx.epd_buffer_4g = NULL;
    heap_caps_free(s_hw_buffer); s_hw_buffer = NULL;
    heap_caps_free(s_hw_buffer_4g); s_hw_buffer_4g = NULL;
}

// ── Orientation ────────────────────────────────────────────────────────

void epd_display_set_orientation(epd_orientation_t orient)
{
    if (orient > EPD_ORIENT_LANDSCAPE_INVERTED || epd_ctx.orientation == orient) return;
    epd_ctx.orientation = orient;
    if (orient == EPD_ORIENT_PORTRAIT || orient == EPD_ORIENT_PORTRAIT_INVERTED) {
        lv_display_set_resolution(disp, LCD_H_RES, LCD_V_RES);
        epd_ctx.screen_w = LCD_H_RES; epd_ctx.screen_h = LCD_V_RES;
    } else {
        lv_display_set_resolution(disp, LCD_V_RES, LCD_H_RES);
        epd_ctx.screen_w = LCD_V_RES; epd_ctx.screen_h = LCD_H_RES;
    }
    lv_obj_invalidate(lv_screen_active());
}

epd_orientation_t epd_display_get_orientation(void) { return epd_ctx.orientation; }

// ── Flush mode ─────────────────────────────────────────────────────────

void epd_display_set_flush_mode(epd_flush_mode_t mode)
{
    if (s_display_mutex) xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    epd_ctx.flush_mode = mode;
    epd_ctx.next_frame_flush_mode = false;
    epd_ctx.transition_frame_ready = false;
    epd_ctx.need_sync_base = mode == EPD_FLUSH_MODE_PARTIAL_WIN;
    if (mode != EPD_FLUSH_MODE_PARTIAL_WIN) {
        epd_ctx.has_refresh_area = false;
    }
    if (s_display_mutex) xSemaphoreGive(s_display_mutex);
}

void epd_display_set_flush_mode_next_frame(epd_flush_mode_t mode)
{
    if (s_display_mutex) xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    epd_ctx.next_frame_flush_mode = true;
    epd_ctx.requested_flush_mode = mode;
    epd_ctx.transition_frame_ready = false;
    if (s_display_mutex) xSemaphoreGive(s_display_mutex);
}

epd_flush_mode_t epd_display_get_flush_mode(void) { return epd_ctx.flush_mode; }

// ── Buffer ─────────────────────────────────────────────────────────────

void epd_display_clear_buffer(void)
{
    if (s_display_mutex) xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    memset(epd_ctx.epd_buffer, 0xFF, EPD_ARRAY);
    if (s_display_mutex) xSemaphoreGive(s_display_mutex);
}

void epd_display_flush_once(epd_flush_mode_t mode)
{
    if (mode == EPD_FLUSH_MODE_PARTIAL_ALL)
        EPD_Dis_PartAll(epd_ctx.epd_buffer);
    else
        EPD_WhiteScreen_ALL(epd_ctx.epd_buffer);
}

void epd_display_sync_base(void)
{
    EPD_Sync_Base_Map(epd_ctx.epd_buffer);
}

void epd_display_set_refresh_area(bool enable, const lv_area_t *area)
{
    if (enable && !area) return;
    if (s_display_mutex) xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    epd_ctx.has_refresh_area = enable;
    if (enable) {
        epd_ctx.refresh_area.x1 = MAX(area->x1, 0);
        epd_ctx.refresh_area.y1 = MAX(area->y1, 0);
        epd_ctx.refresh_area.x2 = MIN(area->x2, (int32_t)epd_ctx.screen_w - 1);
        epd_ctx.refresh_area.y2 = MIN(area->y2, (int32_t)epd_ctx.screen_h - 1);
        if (epd_ctx.refresh_area.x1 > epd_ctx.refresh_area.x2 ||
            epd_ctx.refresh_area.y1 > epd_ctx.refresh_area.y2) {
            epd_ctx.has_refresh_area = false;
        }
    }
    if (s_display_mutex) xSemaphoreGive(s_display_mutex);
}

// ── Resolution ─────────────────────────────────────────────────────────

uint16_t epd_display_control_get_vertical_resolution(void)   { return epd_ctx.screen_h; }
uint16_t epd_display_control_get_horizontal_resolution(void) { return epd_ctx.screen_w; }

// ── Sleep / power ──────────────────────────────────────────────────────

int epd_display_control_sleep_ctrl(bool on)
{
    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    int res = 0;

    if (epd_ctx.state == DISPLAY_STATE_AWAKE && !on) {
        ESP_LOGD(TAG, "Put display to sleep");
        vTaskDelay(pdMS_TO_TICKS(100));
        epd_ctx.state = DISPLAY_STATE_SLEEPING;
        EPD_DeepSleep();
        lv_obj_invalidate(lv_screen_active());
    } else if (epd_ctx.state == DISPLAY_STATE_SLEEPING && on) {
        ESP_LOGD(TAG, "Wake up display");
        epd_ctx.state = DISPLAY_STATE_AWAKE;
        EPD_HW_Init();
        epd_ctx.need_hw_sync_after_wakeup = true;
    }

    xSemaphoreGive(s_display_mutex);
    return res;
}

int epd_display_control_pwr_ctrl(bool on)
{
    if (on && epd_ctx.state == DISPLAY_STATE_POWERED_OFF)
        epd_ctx.state = DISPLAY_STATE_SLEEPING;
    else if (!on && epd_ctx.state == DISPLAY_STATE_SLEEPING)
        epd_ctx.state = DISPLAY_STATE_POWERED_OFF;
    return 0;
}

int epd_display_control_set_render_enabled(bool on)
{
    // For ESP-IDF, LVGL is always running in the main loop
    return 0;
}

// ── 4 Gray support ─────────────────────────────────────────────────────

static void epd_convert_2bpp_to_4gray_planes(const uint8_t *image_2bpp,
                                              uint8_t *ram1, uint8_t *ram2)
{
    memset(ram1, 0x00, EPD_ARRAY);
    memset(ram2, 0x00, EPD_ARRAY);

    for (int y = 0; y < EPD_HEIGHT; y++) {
        for (int x = 0; x < EPD_WIDTH; x++) {
            uint32_t src_byte = y * (EPD_WIDTH / 4) + (x / 4);
            uint8_t  src_shift = 6 - 2 * (x % 4);
            uint8_t  pixel = (image_2bpp[src_byte] >> src_shift) & 0x03;

            bool ram1_bit = (pixel & 0x01) != 0;
            bool ram2_bit = (pixel & 0x02) != 0;

            uint32_t dst_byte = x * (EPD_HEIGHT / 8) + (y / 8);
            uint8_t  dst_bit  = 7 - (y % 8);
            uint8_t  mask = 1 << dst_bit;

            if (ram1_bit) ram1[dst_byte] |= mask;
            if (ram2_bit) ram2[dst_byte] |= mask;
        }
    }
}

void epd_display_show_4gray_image(const uint8_t *image_2bpp)
{
    if (!epd_ctx.epd_buffer || !epd_ctx.epd_buffer_4g) return;
    epd_convert_2bpp_to_4gray_planes(image_2bpp, epd_ctx.epd_buffer, epd_ctx.epd_buffer_4g);
    if (!epd_ctx.is_4g_lut_loaded) {
        EPD_HW_Init_4G();
        epd_ctx.is_4g_lut_loaded = true;
    }
    EPD_Update_4Gray_WithBuffers(epd_ctx.epd_buffer, epd_ctx.epd_buffer_4g);
}

void epd_display_show_4gray_buffers(const uint8_t *ram1, const uint8_t *ram2)
{
    if (!ram1 || !ram2) return;
    if (!epd_ctx.is_4g_lut_loaded) {
        EPD_HW_Init_4G();
        epd_ctx.is_4g_lut_loaded = true;
    }
    EPD_Update_4Gray_WithBuffers(ram1, ram2);
}

void epd_display_enter_4gray_mode(void)
{
    EPD_HW_Init_4G();
    epd_ctx.is_4g_lut_loaded = true;
}

void epd_display_exit_4gray_mode(void)
{
    EPD_HW_Init();
    epd_ctx.is_4g_lut_loaded = false;
}
