#ifndef LV_FLOATING_BUTTON_H
#define LV_FLOATING_BUTTON_H

#include "lvgl.h"

#define LV_FLOATING_BUTTON_SIZE   72
#define LV_FLOATING_BUTTON_MARGIN 24

/* Circular action button fixed to the bottom-right edge of its parent. */
lv_obj_t *lv_floating_button_create(lv_obj_t *parent, const char *symbol,
                                    lv_event_cb_t callback, void *user_data);
lv_obj_t *lv_floating_button_create_image(lv_obj_t *parent,
                                          const lv_image_dsc_t *image,
                                          lv_event_cb_t callback, void *user_data);

#endif
