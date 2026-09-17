// system/controller/touch_control.c
// Touch controller — LVGL input device wrapper for FT6336
// Ported from D:\Codes\EPOS\epos\drivers\epd_touch_control.c

#include "esp_log.h"
#include <lvgl.h>
#include "touch_control.h"
#include "display_control.h"
#include "ft6336.h"

static const char *TAG = "touch_ctrl";

lv_indev_t *touch_indev = NULL;
lv_group_t *input_group = NULL;

// Touch context (updated by ft6336_read)
static struct {
    int32_t x, y;
    bool pressed;
} s_touch_ctx;

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static ft6336_touch_data_t td;
    if (ft6336_read(&td) == ESP_OK && td.count > 0) {
        s_touch_ctx.x = td.points[0].x;
        s_touch_ctx.y = td.points[0].y;
        s_touch_ctx.pressed = (td.points[0].event != FT6336_EVENT_UP);
    } else {
        s_touch_ctx.pressed = false;
    }

    // Orientation mapping
    int16_t lx = s_touch_ctx.x, ly = s_touch_ctx.y;
    switch (epd_display_get_orientation()) {
    case EPD_ORIENT_LANDSCAPE:
        lx = s_touch_ctx.y; ly = LCD_H_RES - 1 - s_touch_ctx.x; break;
    case EPD_ORIENT_PORTRAIT_INVERTED:
        lx = LCD_H_RES - 1 - s_touch_ctx.x; ly = LCD_V_RES - 1 - s_touch_ctx.y; break;
    case EPD_ORIENT_LANDSCAPE_INVERTED:
        lx = LCD_V_RES - 1 - s_touch_ctx.y; ly = s_touch_ctx.x; break;
    default: break;
    }

    if (lx < 0) lx = 0;
    if (ly < 0) ly = 0;
    int16_t mx = epd_display_control_get_horizontal_resolution() - 1;
    int16_t my = epd_display_control_get_vertical_resolution() - 1;
    if (lx > mx) lx = mx;
    if (ly > my) ly = my;

    data->point.x = lx;
    data->point.y = ly;
    data->state = s_touch_ctx.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void touch_control_init(void)
{
    touch_indev = lv_indev_create();
    if (touch_indev) {
        lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(touch_indev, touch_read_cb);
        ESP_LOGI(TAG, "Touch input initialized");
    }

    input_group = lv_group_create();
    lv_group_set_default(input_group);
    lv_indev_set_group(touch_indev, input_group);
}

void touch_control_set_power(bool on)
{
    // FT6336 power control via I2C sleep command
    ft6336_sleep(!on);
}
