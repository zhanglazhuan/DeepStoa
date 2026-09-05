#ifndef DISPLAY_REFRESH_POLICY_H
#define DISPLAY_REFRESH_POLICY_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
} epd_refresh_rect_t;

bool epd_refresh_rect_intersect(const epd_refresh_rect_t *a,
                                const epd_refresh_rect_t *b,
                                epd_refresh_rect_t *result);

bool epd_refresh_choose_window(const epd_refresh_rect_t *changed,
                               bool input_only,
                               const epd_refresh_rect_t *input_area,
                               epd_refresh_rect_t *window);

#endif
