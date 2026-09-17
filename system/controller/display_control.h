// system/controller/display_control.h
// E-ink display controller — LVGL display driver for GDEM0397T81P
// Ported from D:\Codes\EPOS\epos\drivers\epd_display_control.h

#ifndef DISPLAY_CONTROL_H
#define DISPLAY_CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Display resolution (GDEM0397T81P: 480×800) ────────────────────────
#define LCD_H_RES  480
#define LCD_V_RES  800

// ── Orientation ────────────────────────────────────────────────────────
typedef enum {
    EPD_ORIENT_PORTRAIT = 0,
    EPD_ORIENT_LANDSCAPE,
    EPD_ORIENT_PORTRAIT_INVERTED,
    EPD_ORIENT_LANDSCAPE_INVERTED,
} epd_orientation_t;

// ── Flush mode ─────────────────────────────────────────────────────────
typedef enum {
    EPD_FLUSH_MODE_FULL = 0,
    EPD_FLUSH_MODE_FAST,
    EPD_FLUSH_MODE_PARTIAL_ALL,
    EPD_FLUSH_MODE_PARTIAL_WIN,
    EPD_FLUSH_MODE_4G,
} epd_flush_mode_t;

// ── Display state ──────────────────────────────────────────────────────
typedef enum {
    DISPLAY_STATE_SLEEPING = 0,
    DISPLAY_STATE_AWAKE,
    DISPLAY_STATE_POWERED_OFF,
} display_state_t;

// ── Display context ────────────────────────────────────────────────────
typedef struct {
    uint8_t    *epd_buffer;
    uint8_t    *epd_buffer_4g;
    epd_orientation_t orientation;
    epd_flush_mode_t  flush_mode;
    lv_area_t  dirty_area;
    bool       is_first_flush_in_cycle;
    display_state_t state;
    bool       is_fast_lut_loaded;
    bool       need_hw_sync_after_wakeup;
    bool       need_sync_base;
    bool       next_frame_flush_mode;
    epd_flush_mode_t requested_flush_mode;
    bool       transition_frame_ready;
    bool       is_4g_lut_loaded;
    bool       has_refresh_area;
    lv_area_t  refresh_area;
    uint16_t   screen_w;
    uint16_t   screen_h;
} epd_context_t;

extern lv_display_t *disp;
extern epd_context_t epd_ctx;

// ── API ────────────────────────────────────────────────────────────────
void display_control_init(void);
void display_control_deinit(void);

void epd_display_set_orientation(epd_orientation_t orient);
epd_orientation_t epd_display_get_orientation(void);

void epd_display_set_flush_mode(epd_flush_mode_t mode);
void epd_display_set_flush_mode_next_frame(epd_flush_mode_t mode);
epd_flush_mode_t epd_display_get_flush_mode(void);

void epd_display_clear_buffer(void);
void epd_display_flush_once(epd_flush_mode_t mode);
void epd_display_sync_base(void);
/* Restrict post-transition window updates to one UI area.  The restriction
 * is bypassed while the initial static keyboard frame establishes its base. */
void epd_display_set_refresh_area(bool enable, const lv_area_t *area);

void epd_display_show_4gray_image(const uint8_t *image_2bpp);
void epd_display_show_4gray_buffers(const uint8_t *ram1, const uint8_t *ram2);
void epd_display_enter_4gray_mode(void);
void epd_display_exit_4gray_mode(void);

uint16_t epd_display_control_get_vertical_resolution(void);
uint16_t epd_display_control_get_horizontal_resolution(void);

int  epd_display_control_sleep_ctrl(bool on);
int  epd_display_control_pwr_ctrl(bool on);
int  epd_display_control_set_render_enabled(bool on);

#ifdef __cplusplus
}
#endif

#endif
