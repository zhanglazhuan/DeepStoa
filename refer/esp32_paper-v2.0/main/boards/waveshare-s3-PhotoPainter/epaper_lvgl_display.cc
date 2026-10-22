#include "epaper_lvgl_display.h"

#include "epaper_port.h"

#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/task.h>

#define TAG "EpaperLvglDisplay"

namespace {
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

static bool IsBlackRgb565(uint16_t color)
{
    uint8_t r = ((color >> 11) & 0x1f) << 3;
    uint8_t g = ((color >> 5) & 0x3f) << 2;
    uint8_t b = (color & 0x1f) << 3;
    return static_cast<uint16_t>(r) + g + b < 384;
}

static void SetPixel(int x, int y, bool black)
{
    if (s_epd_buffer == nullptr || x < 0 || x >= s_logical_width || y < 0 || y >= s_logical_height) {
        return;
    }

    int epd_x = 0;
    int epd_y = 0;
    switch (s_rotation) {
        case 0:
            epd_x = x;
            epd_y = y;
            break;
        case 90:
            epd_x = EPD_WIDTH - y - 1;
            epd_y = x;
            break;
        case 180:
            epd_x = EPD_WIDTH - x - 1;
            epd_y = EPD_HEIGHT - y - 1;
            break;
        case 270:
        default:
            epd_x = y;
            epd_y = EPD_HEIGHT - x - 1;
            break;
    }

    if (epd_x < 0 || epd_x >= EPD_WIDTH || epd_y < 0 || epd_y >= EPD_HEIGHT) {
        return;
    }

    const size_t addr = static_cast<size_t>(epd_x / 8) + static_cast<size_t>(epd_y) * (EPD_WIDTH / 8);
    const uint8_t mask = 0x80 >> (epd_x % 8);
    if (black) {
        s_epd_buffer[addr] &= ~mask;
    } else {
        s_epd_buffer[addr] |= mask;
    }
}

static void Flush(lv_display_t *display, const lv_area_t *area, uint8_t *color_p)
{
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

extern "C" bool epaper_lvgl_display_init(void)
{
    if (s_display != nullptr) {
        return true;
    }

    s_epd_buffer = static_cast<uint8_t *>(heap_caps_malloc(EPD_SIZE_MONO, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_epd_buffer == nullptr) {
        s_epd_buffer = static_cast<uint8_t *>(heap_caps_malloc(EPD_SIZE_MONO, MALLOC_CAP_8BIT));
    }
    if (s_epd_buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate e-paper buffer");
        return false;
    }

    const size_t lvgl_buffer_size = EPD_WIDTH * kLvglBufferRows * sizeof(lv_color16_t);
    s_lvgl_buffer = static_cast<uint8_t *>(heap_caps_malloc(lvgl_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_lvgl_buffer == nullptr) {
        s_lvgl_buffer = static_cast<uint8_t *>(heap_caps_malloc(lvgl_buffer_size, MALLOC_CAP_8BIT));
    }
    if (s_lvgl_buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffer");
        return false;
    }

    lv_init();
    s_display = lv_display_create(s_logical_width, s_logical_height);
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_display, Flush);
    lv_display_set_buffers(s_display, s_lvgl_buffer, nullptr, lvgl_buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    return true;
}

extern "C" void epaper_lvgl_display_set_rotation(int rotation)
{
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
        rotation = 270;
    }

    const int width = (rotation == 0 || rotation == 180) ? EPD_WIDTH : EPD_HEIGHT;
    const int height = (rotation == 0 || rotation == 180) ? EPD_HEIGHT : EPD_WIDTH;
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

extern "C" int epaper_lvgl_display_get_width(void)
{
    return s_logical_width;
}

extern "C" int epaper_lvgl_display_get_height(void)
{
    return s_logical_height;
}

extern "C" lv_obj_t *epaper_lvgl_display_create_screen(void)
{
    if (!epaper_lvgl_display_init()) {
        return nullptr;
    }

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

extern "C" void epaper_lvgl_display_present(int refresh_mode)
{
    if (!epaper_lvgl_display_render()) {
        return;
    }

    epaper_lvgl_display_flush(refresh_mode);
}

extern "C" bool epaper_lvgl_display_render(void)
{
    if (s_display == nullptr || s_epd_buffer == nullptr) {
        return false;
    }

    lv_display_enable_invalidation(s_display, true);
    lv_obj_invalidate(lv_screen_active());
    vTaskDelay(pdMS_TO_TICKS(1));
    memset(s_epd_buffer, 0xff, EPD_SIZE_MONO);
    lv_refr_now(s_display);
    vTaskDelay(pdMS_TO_TICKS(1));
    return true;
}

extern "C" void epaper_lvgl_display_draw_bitmap(const unsigned char *image_buffer, int x, int y, int width, int height)
{
    if (image_buffer == nullptr || s_epd_buffer == nullptr) {
        return;
    }

    const int padded_width = (width % 8 == 0) ? width : (width / 8 + 1) * 8;
    const unsigned char *ptr = image_buffer;
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < padded_width; ++col) {
            const bool black = !(*ptr & (0x80 >> (col % 8)));
            if (col < width) {
                SetPixel(x + col, y + row, black);
            }
            if (col % 8 == 7) {
                ptr++;
            }
        }
    }
}

extern "C" void epaper_lvgl_display_flush(int refresh_mode)
{
    if (s_epd_buffer == nullptr) {
        return;
    }

    if (refresh_mode == Global_refresh || !s_partial_base_ready) {
        EPD_Display_Base(s_epd_buffer);
        s_partial_base_ready = true;
    } else {
        EPD_Display_Partial(s_epd_buffer, 0, 0, EPD_WIDTH, EPD_HEIGHT);
    }
}

extern "C" void epaper_lvgl_display_reset_partial_base(void)
{
    s_partial_base_ready = false;
}
