#ifndef EPAPER_LVGL_DISPLAY_H
#define EPAPER_LVGL_DISPLAY_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

bool epaper_lvgl_display_init(void);
void epaper_lvgl_display_set_rotation(int rotation);
int epaper_lvgl_display_get_width(void);
int epaper_lvgl_display_get_height(void);
lv_obj_t *epaper_lvgl_display_create_screen(void);
bool epaper_lvgl_display_render(void);
void epaper_lvgl_display_draw_bitmap(const unsigned char *image_buffer, int x, int y, int width, int height);
void epaper_lvgl_display_flush(int refresh_mode);
void epaper_lvgl_display_present(int refresh_mode);
void epaper_lvgl_display_reset_partial_base(void);

#ifdef __cplusplus
}
#endif

#endif
