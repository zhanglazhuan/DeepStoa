// debug/t_quality/main/t_quality.c
// E-ink image-quality test: GDEM0397T81P via ESP-IDF hardware SPI + direct GPIO
//
// Board: selected in main/CMakeLists.txt (default = boards/esp32s3/esp32s3_devkit.h)
//   GPIO 11=MOSI, 12=SCK, 15=CS, 6=DC, 7=RST, 8=BUSY
// Button: on-board BOOT/PRG button on GPIO0 (active low, internal pull-up) — same on both boards.
// Touch:  FT6336U on I2C0 (SDA=41, SCL=42, RST=5), optional — the test still runs without it.
//
// Each button press cycles the panel through three patterns:
//   1. all black
//   2. all white
//   3. 32px checkerboard (chess board)
// Each one- or two-finger touch redraws the current pattern with every contact overlaid:
//   - an inverted crosshair at the raw FT6336 coordinate
//   - a numbered coordinate label beside the corresponding crosshair
// All refreshes go out through the FAST waveform (EPD_HW_Init_Fast + EPD_WhiteScreen_ALL_Fast,
// ~1.5 s) rather than the full one (~3-4 s). Every frame is composed into `framebuffer`
// first so there is a single refresh path.
// After every refresh the panel is put into deep sleep; the image persists.
// Driver shared with debug/t_display (epd_display.c) — do not modify it from here.

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "epd_display.h"
#include "esp32s3_devkit.h"
#include "ft6336.h"

static const char *TAG = "t_quality";

#define BUTTON_GPIO         GPIO_NUM_0
#define BUTTON_ACTIVE       0           // active low
#define POLL_MS             10          // button + touch polling period
#define BUTTON_DEBOUNCE_MS  50
#define CHECKER_CELL_PX     32

// Touch overlay geometry (framebuffer pixels)
#define TEXT_SCALE          2           // 5x7 glyph -> 10x14 px
#define TEXT_MARGIN_PX      4
#define TEXT_LINE_GAP_PX    2
#define LABEL_GAP_PX        8
#define CROSS_ARM_PX        24          // crosshair half-length
#define CROSS_THICK_PX      3
#define TOUCH_SETTLE_MS     100         // allow near-simultaneous fingers to form one snapshot

typedef enum {
    PATTERN_BLACK = 0,
    PATTERN_WHITE,
    PATTERN_CHECKER,
    PATTERN_COUNT
} pattern_t;

static const char *pattern_name[PATTERN_COUNT] = {
    "black", "white", "checkerboard 32px",
};

// 1 bit per pixel, 1 = white, 0 = black, MSB first.
// Layout is column-major: EPD_WIDTH columns x (EPD_HEIGHT/8) bytes, 8 vertical pixels per byte.
static uint8_t framebuffer[EPD_ARRAY];

// ─── Framebuffer primitives ────────────────────────────────────────────
// Same mapping as system/controller/display_control.c:
//   byte = x * (EPD_HEIGHT / 8) + (y / 8),  bit = 7 - (y % 8)

static inline bool fb_in_bounds(int x, int y)
{
    return x >= 0 && x < EPD_WIDTH && y >= 0 && y < EPD_HEIGHT;
}

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static inline void fb_set_pixel(uint8_t *buf, int x, int y, bool white)
{
    if (!fb_in_bounds(x, y)) return;
    uint32_t byte = (uint32_t)x * (EPD_HEIGHT / 8) + (uint32_t)(y / 8);
    uint8_t mask = (uint8_t)(1U << (7 - (y % 8)));
    if (white) buf[byte] |= mask;
    else       buf[byte] &= (uint8_t)~mask;
}

static inline void fb_invert_pixel(uint8_t *buf, int x, int y)
{
    if (!fb_in_bounds(x, y)) return;
    buf[(uint32_t)x * (EPD_HEIGHT / 8) + (uint32_t)(y / 8)] ^= (uint8_t)(1U << (7 - (y % 8)));
}

static void fb_fill_rect(uint8_t *buf, int x0, int y0, int w, int h, bool white)
{
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            fb_set_pixel(buf, x, y, white);
        }
    }
}

static void fb_invert_rect(uint8_t *buf, int x0, int y0, int w, int h)
{
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            fb_invert_pixel(buf, x, y);
        }
    }
}

// ─── Minimal 5x7 font ──────────────────────────────────────────────────
// Only the glyphs needed for the point number and X/Y coordinate labels.
// One byte per column, bit 0 = top row.

#define GLYPH_W  5
#define GLYPH_H  7
#define GLYPH_ADVANCE  (GLYPH_W + 1)

static const uint8_t *glyph_columns(char c)
{
    static const uint8_t digits[10][GLYPH_W] = {
        { 0x3E, 0x51, 0x49, 0x45, 0x3E },  // 0
        { 0x00, 0x42, 0x7F, 0x40, 0x00 },  // 1
        { 0x42, 0x61, 0x51, 0x49, 0x46 },  // 2
        { 0x21, 0x41, 0x45, 0x4B, 0x31 },  // 3
        { 0x18, 0x14, 0x12, 0x7F, 0x10 },  // 4
        { 0x27, 0x45, 0x45, 0x45, 0x39 },  // 5
        { 0x3C, 0x4A, 0x49, 0x49, 0x30 },  // 6
        { 0x01, 0x71, 0x09, 0x05, 0x03 },  // 7
        { 0x36, 0x49, 0x49, 0x49, 0x36 },  // 8
        { 0x06, 0x49, 0x49, 0x29, 0x1E },  // 9
    };
    static const uint8_t glyph_x[GLYPH_W]     = { 0x63, 0x14, 0x08, 0x14, 0x63 };
    static const uint8_t glyph_y[GLYPH_W]     = { 0x07, 0x08, 0x70, 0x08, 0x07 };
    static const uint8_t glyph_eq[GLYPH_W]    = { 0x14, 0x14, 0x14, 0x14, 0x14 };
    static const uint8_t glyph_space[GLYPH_W] = { 0x00, 0x00, 0x00, 0x00, 0x00 };

    if (c >= '0' && c <= '9') return digits[c - '0'];
    switch (c) {
    case 'X': return glyph_x;
    case 'Y': return glyph_y;
    case '=': return glyph_eq;
    default:  return glyph_space;
    }
}

static int fb_text_width(const char *text, int scale)
{
    int len = (int)strlen(text);
    return (len * GLYPH_ADVANCE - 1) * scale;
}

static void fb_draw_text_line(uint8_t *buf, int x0, int y0, const char *text, int scale)
{
    int len = (int)strlen(text);
    int pen_x = x0;
    for (int i = 0; i < len; i++) {
        const uint8_t *cols = glyph_columns(text[i]);
        for (int cx = 0; cx < GLYPH_W; cx++) {
            for (int cy = 0; cy < GLYPH_H; cy++) {
                if (cols[cx] & (1U << cy)) {
                    fb_fill_rect(buf, pen_x + cx * scale, y0 + cy * scale, scale, scale, false);
                }
            }
        }
        pen_x += GLYPH_ADVANCE * scale;
    }
}

// Inverted crosshair centred on (x, y): visible on black, white and the checkerboard alike.
static void fb_draw_crosshair(uint8_t *buf, int x, int y)
{
    const int half = CROSS_THICK_PX / 2;
    fb_invert_rect(buf, x - CROSS_ARM_PX, y - half, 2 * CROSS_ARM_PX + 1, CROSS_THICK_PX);
    // Vertical arm, skipping the already-inverted centre band so it does not cancel out.
    fb_invert_rect(buf, x - half, y - CROSS_ARM_PX, CROSS_THICK_PX, CROSS_ARM_PX - half);
    fb_invert_rect(buf, x - half, y + half + 1,     CROSS_THICK_PX, CROSS_ARM_PX - half);
}

static void fb_draw_touch_label(uint8_t *buf, const ft6336_touch_point_t *point,
                                uint8_t point_index)
{
    char line1[16];
    char line2[16];
    snprintf(line1, sizeof(line1), "%u X=%u",
             (unsigned)(point_index + 1), (unsigned)point->x);
    snprintf(line2, sizeof(line2), "Y=%u", (unsigned)point->y);

    int line1_w = fb_text_width(line1, TEXT_SCALE);
    int line2_w = fb_text_width(line2, TEXT_SCALE);
    int box_w = (line1_w > line2_w ? line1_w : line2_w) + 2 * TEXT_MARGIN_PX;
    int box_h = 2 * GLYPH_H * TEXT_SCALE + TEXT_LINE_GAP_PX + 2 * TEXT_MARGIN_PX;
    int point_x = point->x;
    int point_y = point->y;

    // Place labels outward from the screen centre so two ordinary finger positions
    // remain readable; clamp the result for contacts close to a panel edge.
    int label_x;
    if (point_x < EPD_WIDTH / 2) {
        label_x = point_x - CROSS_ARM_PX - LABEL_GAP_PX - box_w;
        if (label_x < 0) label_x = point_x + CROSS_ARM_PX + LABEL_GAP_PX;
    } else {
        label_x = point_x + CROSS_ARM_PX + LABEL_GAP_PX;
        if (label_x + box_w > EPD_WIDTH) {
            label_x = point_x - CROSS_ARM_PX - LABEL_GAP_PX - box_w;
        }
    }

    int label_y = point_y - box_h / 2;
    label_x = clamp_int(label_x, 0, EPD_WIDTH - box_w);
    label_y = clamp_int(label_y, 0, EPD_HEIGHT - box_h);
    fb_fill_rect(buf, label_x, label_y, box_w, box_h, true);
    fb_draw_text_line(buf, label_x + TEXT_MARGIN_PX, label_y + TEXT_MARGIN_PX,
                      line1, TEXT_SCALE);
    fb_draw_text_line(buf, label_x + TEXT_MARGIN_PX,
                      label_y + TEXT_MARGIN_PX + GLYPH_H * TEXT_SCALE + TEXT_LINE_GAP_PX,
                      line2, TEXT_SCALE);
}

// ─── Pattern composition ───────────────────────────────────────────────

static void fill_checkerboard(uint8_t *buf)
{
    // Panel RAM is COLUMN-major, same mapping as system/controller/display_control.c:
    //   byte = x * (EPD_HEIGHT / 8) + (y / 8),  bit = 7 - (y % 8)
    // i.e. EPD_WIDTH columns of BYTES_PER_COL bytes, each byte holding 8 pixels
    // stacked VERTICALLY. Writing row-major here tears the pattern diagonally.
    const int bytes_per_col = EPD_HEIGHT / 8;        // 100
    const int bytes_per_cell = CHECKER_CELL_PX / 8;  // 32px = 4 bytes → cells are byte-aligned in y

    for (int x = 0; x < EPD_WIDTH; x++) {
        int cell_col = x / CHECKER_CELL_PX;
        uint8_t *col = buf + x * bytes_per_col;
        for (int b = 0; b < bytes_per_col; b++) {
            int cell_row = b / bytes_per_cell;
            col[b] = ((cell_row + cell_col) & 1) ? 0x00 : 0xFF;
        }
    }
}

static void fill_pattern(uint8_t *buf, pattern_t p)
{
    switch (p) {
    case PATTERN_BLACK:
        memset(buf, 0x00, EPD_ARRAY);
        break;
    case PATTERN_WHITE:
        memset(buf, 0xFF, EPD_ARRAY);
        break;
    case PATTERN_CHECKER:
        fill_checkerboard(buf);
        break;
    default:
        break;
    }
}

// Composes pattern `p` (plus all active contacts when `touches` is non-NULL) and refreshes.
static void show_pattern(pattern_t p, const ft6336_touch_data_t *touches)
{
    uint8_t touch_count = touches ? touches->count : 0;
    if (touch_count > FT6336_MAX_TOUCH_POINTS) touch_count = FT6336_MAX_TOUCH_POINTS;

    if (touch_count > 0) {
        ESP_LOGI(TAG, "Showing pattern %d: %s + %u touch point(s)",
                 p + 1, pattern_name[p], touch_count);
        for (uint8_t i = 0; i < touch_count; i++) {
            const ft6336_touch_point_t *point = &touches->points[i];
            ESP_LOGI(TAG, "  Point %u: X=%u Y=%u weight=%u area=%u",
                     (unsigned)(i + 1), (unsigned)point->x, (unsigned)point->y,
                     (unsigned)point->weight, (unsigned)point->area);
        }
    } else {
        ESP_LOGI(TAG, "Showing pattern %d: %s", p + 1, pattern_name[p]);
    }
    int64_t t0 = esp_timer_get_time();

    // Panel is in deep sleep after the previous refresh; re-init (HW reset) first.
    // Fast init loads the 1.5 s waveform LUT (0x1A=0x6A) instead of the full one.
    EPD_HW_Init_Fast();
    int64_t t_init = esp_timer_get_time();

    fill_pattern(framebuffer, p);
    for (uint8_t i = 0; i < touch_count; i++) {
        fb_draw_touch_label(framebuffer, &touches->points[i], i);
    }
    // Draw crosshairs last so the exact contact positions stay visible even if a
    // label must be clamped across another contact near a panel edge.
    for (uint8_t i = 0; i < touch_count; i++) {
        fb_draw_crosshair(framebuffer, touches->points[i].x, touches->points[i].y);
    }
    int64_t t_fill = esp_timer_get_time();

    // One path for every frame: push it and run the fast waveform.
    EPD_WhiteScreen_ALL_Fast(framebuffer);
    int64_t t_refresh = esp_timer_get_time();

    EPD_DeepSleep();
    ESP_LOGI(TAG, "Pattern %s done in %lld ms (init %lld + fill %lld + refresh %lld), panel in deep sleep",
             pattern_name[p],
             (t_refresh - t0) / 1000,
             (t_init - t0) / 1000,
             (t_fill - t_init) / 1000,
             (t_refresh - t_fill) / 1000);
}

// ─── Inputs ────────────────────────────────────────────────────────────

static bool s_button_was_down = false;

static void button_gpio_config(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    // A button held at boot (e.g. BOOT still down after flashing) must not count as a press.
    s_button_was_down = (gpio_get_level(BUTTON_GPIO) == BUTTON_ACTIVE);
}

// Non-blocking. Returns true exactly once per debounced press (level going to BUTTON_ACTIVE);
// a held button does not auto-repeat.
static bool button_poll_press(void)
{
    bool down = (gpio_get_level(BUTTON_GPIO) == BUTTON_ACTIVE);
    if (down && !s_button_was_down) {
        vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
        down = (gpio_get_level(BUTTON_GPIO) == BUTTON_ACTIVE);
    }
    bool pressed = down && !s_button_was_down;
    s_button_was_down = down;
    return pressed;
}

static bool touch_init(void)
{
    esp_err_t ret = ft6336_init(DEVKIT_TOUCH_I2C_PORT,
                                DEVKIT_PIN_TOUCH_SDA,
                                DEVKIT_PIN_TOUCH_SCL,
                                DEVKIT_PIN_TOUCH_RST,
                                DEVKIT_TOUCH_I2C_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ft6336_init failed: %s — touch disabled, button still works", esp_err_to_name(ret));
        return false;
    }
    return true;
}

// Non-blocking. Captures all active contacts once per touch sequence. The first point waits
// briefly for a possible second finger; adding a second point later also emits a new snapshot.
static bool touch_poll_snapshot(ft6336_touch_data_t *snapshot)
{
    static uint8_t previous_count = 0;
    static bool sequence_reported = false;
    static int64_t sequence_started_us = 0;
    ft6336_touch_data_t td;
    if (ft6336_read(&td) != ESP_OK) {
        return false;
    }

    // The driver guarantees that points[0..count-1] are active contacts.
    *snapshot = td;
    uint8_t active_count = td.count;
    int64_t now_us = esp_timer_get_time();
    if (active_count == 0) {
        previous_count = 0;
        sequence_reported = false;
        sequence_started_us = 0;
        return false;
    }

    if (previous_count == 0) {
        sequence_started_us = now_us;
    }
    bool point_added = active_count > previous_count;
    previous_count = active_count;

    if (!sequence_reported) {
        bool settle_elapsed = now_us - sequence_started_us >= TOUCH_SETTLE_MS * 1000LL;
        if (active_count == FT6336_MAX_TOUCH_POINTS || settle_elapsed) {
            sequence_reported = true;
            return true;
        }
    } else if (point_added) {
        return true;
    }

    return false;
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== t_quality: E-ink Image Quality Test ===");

    ESP_LOGI(TAG, "Step 1: Init GPIO and hardware SPI...");
    epd_gpio_config();
    button_gpio_config();
    ESP_LOGI(TAG, "PASS: GPIO, SPI and button (GPIO%d) initialized", BUTTON_GPIO);

    ESP_LOGI(TAG, "Step 2: Init FT6336 touch on I2C%d (SDA=%d SCL=%d)...",
             DEVKIT_TOUCH_I2C_PORT, DEVKIT_PIN_TOUCH_SDA, DEVKIT_PIN_TOUCH_SCL);
    bool touch_ok = touch_init();
    if (touch_ok) {
        ESP_LOGI(TAG, "PASS: touch initialized");
    }

    pattern_t current = PATTERN_BLACK;
    show_pattern(current, NULL);

    ESP_LOGI(TAG, "Press the GPIO%d button to cycle: black -> white -> checkerboard", BUTTON_GPIO);
    if (touch_ok) {
        ESP_LOGI(TAG, "Touch with one or two fingers to show every contact coordinate");
    }

    while (1) {
        if (button_poll_press()) {
            current = (current + 1) % PATTERN_COUNT;
            show_pattern(current, NULL);
        } else if (touch_ok) {
            ft6336_touch_data_t touches;
            if (touch_poll_snapshot(&touches)) {
                show_pattern(current, &touches);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
