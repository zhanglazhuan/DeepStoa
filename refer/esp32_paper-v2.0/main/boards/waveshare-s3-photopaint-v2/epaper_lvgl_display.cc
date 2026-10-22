#include "epaper_lvgl_display.h"
#include "config.h"
#include "aw9523.h"

#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>

#define TAG "EpaperLvglDisplay"

#define Global_refresh 1
#define Partial_refresh 0

namespace {

constexpr int kEpaperWidth = 800;
constexpr int kEpaperHeight = 480;
constexpr int kEpaperArray = kEpaperWidth * kEpaperHeight / 8;
constexpr int kLogicalWidth = 480;
constexpr int kLogicalHeight = 800;
constexpr int kLvglBufferRows = 40;

uint8_t *s_epd_buffer = nullptr;
uint8_t *s_lvgl_buffer = nullptr;
lv_display_t *s_display = nullptr;
bool s_partial_base_ready = false;
int s_rotation = 270;
int s_logical_width = kLogicalWidth;
int s_logical_height = kLogicalHeight;

// --- SSD1677 SPI helpers ---
static void SpiWrite(uint8_t value) {
    ESP_ERROR_CHECK(aw9523_shift_out(EPD_SCLK_PIN, EPD_MOSI_PIN, &value, 1));
}

static void WriteCMD(uint8_t cmd) {
    aw9523_write(EPD_CS_PIN | EPD_DC_PIN, false);
    SpiWrite(cmd);
    aw9523_write(EPD_CS_PIN, true);
}

static void WriteDATA(uint8_t data) {
    aw9523_write(EPD_CS_PIN, false);
    aw9523_write(EPD_DC_PIN, true);
    SpiWrite(data);
    aw9523_write(EPD_CS_PIN, true);
}

static void ReadBusy() {
    uint16_t levels = 0;
    for (int retry = 0; retry < 3000; ++retry) {
        if (aw9523_read(EPD_BUSY_PIN, &levels) != ESP_OK || levels == 0) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGE(TAG, "EPD BUSY timeout after 30 seconds");
}

// SSD1677 hardware init (from Arduino example)
static void EPD_HW_Init() {
    aw9523_write(EPD_POWER_PIN, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    aw9523_write(EPD_POWER_PIN, true);
    vTaskDelay(pdMS_TO_TICKS(10));

    ReadBusy();
    WriteCMD(0x12); // SWRESET
    ReadBusy();

    WriteCMD(0x18);
    WriteDATA(0x80);

    WriteCMD(0x0C);
    WriteDATA(0xAE);
    WriteDATA(0xC7);
    WriteDATA(0xC3);
    WriteDATA(0xC0);
    WriteDATA(0x80);

    WriteCMD(0x01); // Driver output control
    WriteDATA((kEpaperWidth - 1) & 0xFF);
    WriteDATA((kEpaperWidth - 1) >> 8);
    WriteDATA(0x02);

    WriteCMD(0x3C); // Border waveform
    WriteDATA(0x01);

    WriteCMD(0x11); // Data entry mode
    WriteDATA(0x03);

    WriteCMD(0x44); // RAM X address
    WriteDATA(0x00);
    WriteDATA(0x00);
    WriteDATA((kEpaperHeight - 1) & 0xFF);
    WriteDATA((kEpaperHeight - 1) >> 8);

    WriteCMD(0x45); // RAM Y address
    WriteDATA(0x00);
    WriteDATA(0x00);
    WriteDATA((kEpaperWidth - 1) & 0xFF);
    WriteDATA((kEpaperWidth - 1) >> 8);

    WriteCMD(0x4E);
    WriteDATA(0x00);
    WriteDATA(0x00);
    WriteCMD(0x4F);
    WriteDATA(0x00);
    WriteDATA(0x00);
    ReadBusy();
}

// --- SSD1677 LUT waveform tables (temperature-compensated) ---
// Each LUT is 112 bytes: 105 for VCOM register 0x32 + 1 for reg 0x03 + 3 for reg 0x04 + 1 for reg 0x2C

static const uint8_t LUT_WS_0_5[112] = {
0xAA,0x48,0x55,0x44,0x00,0x00,0x00,0x00,0x00,0x00,
0x55,0x48,0xAA,0x88,0x00,0x00,0x00,0x00,0x00,0x00,
0xAA,0x48,0x55,0x44,0x00,0x00,0x00,0x00,0x00,0x00,
0x55,0x48,0xAA,0x88,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x1E,0x23,0x21,0x23,0x00,
0x28,0x01,0x28,0x01,0x03,
0x1B,0x19,0x05,0x03,0x01,
0x05,0x00,0x08,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x22,0x22,0x22,0x22,0x22,
0x17,0x41,0xA8,0x32,0x48,
0x00,0x00,
};

static const uint8_t LUT_WS_5_10[112] = {
0xAA,0x48,0x55,0x44,0x00,0x00,0x00,0x00,0x00,0x00,
0x55,0x48,0xAA,0x88,0x00,0x00,0x00,0x00,0x00,0x00,
0xAA,0x48,0x55,0x44,0x00,0x00,0x00,0x00,0x00,0x00,
0x55,0x48,0xAA,0x88,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x1E,0x23,0x05,0x02,0x00,
0x2B,0x01,0x2B,0x01,0x02,
0x1B,0x19,0x05,0x03,0x00,
0x05,0x00,0x07,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x22,0x22,0x22,0x22,0x22,
0x17,0x41,0xA8,0x32,0x48,
0x00,0x00,
};

static const uint8_t LUT_WS_10_15[112] = {
0xAA,0x48,0x55,0x44,0x00,0x00,0x00,0x00,0x00,0x00,
0x55,0x48,0xAA,0x88,0x00,0x00,0x00,0x00,0x00,0x00,
0xAA,0x48,0x55,0x44,0x00,0x00,0x00,0x00,0x00,0x00,
0x55,0x48,0xAA,0x88,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x14,0x1A,0x0B,0x06,0x00,
0x21,0x01,0x21,0x01,0x02,
0x18,0x16,0x05,0x03,0x00,
0x04,0x00,0x05,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x22,0x22,0x22,0x22,0x22,
0x17,0x41,0xA8,0x32,0x48,
0x00,0x00,
};

static const uint8_t LUT_WS_15_20[112] = {
0xA2,0x48,0x51,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x54,0x48,0xA8,0x80,0x00,0x00,0x00,0x00,0x00,0x00,
0xA2,0x48,0x51,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x54,0x48,0xA8,0x80,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x0D,0x0D,0x08,0x05,0x00,
0x0F,0x01,0x0F,0x01,0x04,
0x0D,0x0D,0x05,0x05,0x00,
0x03,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x01,
0x22,0x22,0x22,0x22,0x22,
0x17,0x41,0xA8,0x32,0x48,
0x00,0x00,
};

static const uint8_t LUT_WS_20_80[112] = {
0xA0,0x48,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x50,0x48,0xA8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0xA0,0x48,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x50,0x48,0xA8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x1A,0x14,0x00,0x00,0x00,
0x0D,0x01,0x0D,0x01,0x02,
0x0A,0x0A,0x03,0x00,0x01,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x01,
0x22,0x22,0x22,0x22,0x22,
0x17,0x41,0xA8,0x32,0x48,
0x00,0x00,
};

static const uint8_t LUT_WS_80_127[112] = {
0xA8,0x00,0x55,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x54,0x00,0xAA,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0xA8,0x00,0x55,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x54,0x00,0xAA,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x0C,0x0D,0x0B,0x01,0x00,
0x00,0x00,0x00,0x00,0x00,
0x0A,0x0A,0x05,0x0B,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x01,0x01,
0x22,0x22,0x22,0x22,0x22,
0x17,0x41,0xA8,0x32,0x30,
0x00,0x00,
};

static void Write_LUT(const uint8_t *waveform) {
    WriteCMD(0x32); // Write VCOM register
    for (int i = 0; i < 105; i++) {
        WriteDATA(waveform[i]);
    }
    ReadBusy();

    WriteCMD(0x03);
    WriteDATA(waveform[105]);

    WriteCMD(0x04);
    WriteDATA(waveform[106]);
    WriteDATA(waveform[107]);
    WriteDATA(waveform[108]);

    WriteCMD(0x2C); // VCOM
    WriteDATA(waveform[109]);
}

// Use fixed room temp (25°C) — selects WS_20_80 LUT.
// Full temperature compensation requires SPI readback which needs a MISO pin.
static void Write_LUT_All(void) {
    Write_LUT(LUT_WS_20_80);
}

static void EPD_Update() {
    Write_LUT_All();
    WriteCMD(0x22);
    WriteDATA(0xC7);
    WriteCMD(0x20);
    ReadBusy();
}

static void EPD_Part_Update() {
    WriteCMD(0x22);
    WriteDATA(0xFF);
    WriteCMD(0x20);
    ReadBusy();
}

static void EPD_DeepSleep() {
    WriteCMD(0x10);
    WriteDATA(0x01);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void EPD_Init() {
    aw9523_set_direction(EPD_MOSI_PIN | EPD_SCLK_PIN | EPD_CS_PIN |
                         EPD_DC_PIN | EPD_POWER_PIN, false);
    aw9523_set_direction(EPD_BUSY_PIN, true);
    aw9523_write(EPD_CS_PIN | EPD_DC_PIN | EPD_POWER_PIN, true);

    EPD_HW_Init();
}

static void EPD_Display_Base(const uint8_t *buffer) {
    WriteCMD(0x24); // Write RAM for black/white
    for (int i = 0; i < kEpaperArray; i++) {
        WriteDATA(buffer[i]);
    }
    EPD_Update();
}

static void EPD_Display_Partial(const uint8_t *buffer, int x, int y, int w, int h) {
    // Reset for partial
    aw9523_write(EPD_POWER_PIN, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    aw9523_write(EPD_POWER_PIN, true);
    vTaskDelay(pdMS_TO_TICKS(10));

    WriteCMD(0x18);
    WriteDATA(0x80);
    WriteCMD(0x3C);
    WriteDATA(0x80);

    WriteCMD(0x44);
    WriteDATA(x & 0xFF);
    WriteDATA(x >> 8);
    WriteDATA((x + w - 1) & 0xFF);
    WriteDATA((x + w - 1) >> 8);

    WriteCMD(0x45);
    WriteDATA(y & 0xFF);
    WriteDATA(y >> 8);
    WriteDATA((y + h - 1) & 0xFF);
    WriteDATA((y + h - 1) >> 8);

    WriteCMD(0x4E);
    WriteDATA(x & 0xFF);
    WriteDATA(x >> 8);
    WriteCMD(0x4F);
    WriteDATA(y & 0xFF);
    WriteDATA(y >> 8);

    WriteCMD(0x24);
    for (int i = 0; i < w * h / 8; i++) {
        WriteDATA(buffer[i + (y * kEpaperWidth + x) / 8]);
    }
    EPD_Part_Update();
}

static void EPD_Sleep() {
    EPD_DeepSleep();
}

static bool IsBlackRgb565(uint16_t color) {
    uint8_t r = ((color >> 11) & 0x1f) << 3;
    uint8_t g = ((color >> 5) & 0x3f) << 2;
    uint8_t b = (color & 0x1f) << 3;
    return static_cast<uint16_t>(r) + g + b < 384;
}

static void SetPixel(int x, int y, bool black) {
    if (s_epd_buffer == nullptr || x < 0 || x >= s_logical_width || y < 0 || y >= s_logical_height) return;

    int epd_x = 0, epd_y = 0;
    switch (s_rotation) {
        case 0:   epd_x = x; epd_y = y; break;
        case 90:  epd_x = kEpaperWidth - y - 1; epd_y = x; break;
        case 180: epd_x = kEpaperWidth - x - 1; epd_y = kEpaperHeight - y - 1; break;
        case 270:
        default:  epd_x = y; epd_y = kEpaperHeight - x - 1; break;
    }

    if (epd_x < 0 || epd_x >= kEpaperWidth || epd_y < 0 || epd_y >= kEpaperHeight) return;

    const size_t addr = static_cast<size_t>(epd_x / 8) + static_cast<size_t>(epd_y) * (kEpaperWidth / 8);
    const uint8_t mask = 0x80 >> (epd_x % 8);
    if (black) s_epd_buffer[addr] &= ~mask;
    else s_epd_buffer[addr] |= mask;
}

static void Flush(lv_display_t *display, const lv_area_t *area, uint8_t *color_p) {
    const uint16_t *pixels = reinterpret_cast<const uint16_t *>(color_p);
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) {
            SetPixel(x, y, IsBlackRgb565(*pixels++));
        }
        if (((y - area->y1) & 0x0f) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    lv_display_flush_ready(display);
}
} // namespace

extern "C" bool epaper_lvgl_display_init(void) {
    if (s_display != nullptr) return true;

    s_epd_buffer = static_cast<uint8_t *>(heap_caps_malloc(kEpaperArray, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_epd_buffer == nullptr)
        s_epd_buffer = static_cast<uint8_t *>(heap_caps_malloc(kEpaperArray, MALLOC_CAP_8BIT));
    if (s_epd_buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate e-paper buffer");
        return false;
    }

    const size_t lvgl_buf_size = kEpaperWidth * kLvglBufferRows * sizeof(lv_color16_t);
    s_lvgl_buffer = static_cast<uint8_t *>(heap_caps_malloc(lvgl_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_lvgl_buffer == nullptr)
        s_lvgl_buffer = static_cast<uint8_t *>(heap_caps_malloc(lvgl_buf_size, MALLOC_CAP_8BIT));
    if (s_lvgl_buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffer");
        return false;
    }

    EPD_Init();
    lv_init();
    s_display = lv_display_create(s_logical_width, s_logical_height);
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_display, Flush);
    lv_display_set_buffers(s_display, s_lvgl_buffer, nullptr, lvgl_buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    return true;
}

extern "C" void epaper_lvgl_display_set_rotation(int rotation) {
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) rotation = 270;
    const int width = (rotation == 0 || rotation == 180) ? kEpaperWidth : kEpaperHeight;
    const int height = (rotation == 0 || rotation == 180) ? kEpaperHeight : kEpaperWidth;
    const bool changed = rotation != s_rotation || width != s_logical_width || height != s_logical_height;
    s_rotation = rotation;
    s_logical_width = width;
    s_logical_height = height;
    if (s_display != nullptr && changed) {
        lv_display_enable_invalidation(s_display, false);
        lv_display_set_resolution(s_display, s_logical_width, s_logical_height);
        s_partial_base_ready = false;
    }
}

extern "C" int epaper_lvgl_display_get_width(void) { return s_logical_width; }
extern "C" int epaper_lvgl_display_get_height(void) { return s_logical_height; }

extern "C" lv_obj_t *epaper_lvgl_display_create_screen(void) {
    if (!epaper_lvgl_display_init()) return nullptr;
    lv_display_enable_invalidation(s_display, false);
    lv_obj_clean(lv_screen_active());
    lv_obj_t *screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(screen, s_logical_width, s_logical_height);
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    return screen;
}

extern "C" bool epaper_lvgl_display_render(void) {
    if (s_display == nullptr || s_epd_buffer == nullptr) return false;
    lv_display_enable_invalidation(s_display, true);
    lv_obj_invalidate(lv_screen_active());
    vTaskDelay(pdMS_TO_TICKS(1));
    memset(s_epd_buffer, 0xff, kEpaperArray);
    lv_refr_now(s_display);
    vTaskDelay(pdMS_TO_TICKS(1));
    return true;
}

extern "C" void epaper_lvgl_display_draw_bitmap(const unsigned char *image_buffer, int x, int y, int width, int height) {
    if (image_buffer == nullptr || s_epd_buffer == nullptr) return;
    const int padded_width = (width % 8 == 0) ? width : (width / 8 + 1) * 8;
    const unsigned char *ptr = image_buffer;
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < padded_width; ++col) {
            const bool black = !(*ptr & (0x80 >> (col % 8)));
            if (col < width) SetPixel(x + col, y + row, black);
            if (col % 8 == 7) ptr++;
        }
    }
}

extern "C" void epaper_lvgl_display_flush(int refresh_mode) {
    if (s_epd_buffer == nullptr) return;
    if (refresh_mode == Global_refresh || !s_partial_base_ready) {
        EPD_Display_Base(s_epd_buffer);
        s_partial_base_ready = true;
    } else {
        EPD_Display_Partial(s_epd_buffer, 0, 0, kEpaperWidth, kEpaperHeight);
    }
}

extern "C" void epaper_lvgl_display_present(int refresh_mode) {
    if (!epaper_lvgl_display_render()) return;
    epaper_lvgl_display_flush(refresh_mode);
}

extern "C" void epaper_lvgl_display_reset_partial_base(void) {
    s_partial_base_ready = false;
}
