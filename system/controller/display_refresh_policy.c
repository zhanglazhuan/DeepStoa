#include "display_refresh_policy.h"

static int32_t max_i32(int32_t a, int32_t b) { return a > b ? a : b; }
static int32_t min_i32(int32_t a, int32_t b) { return a < b ? a : b; }

bool epd_refresh_rect_intersect(const epd_refresh_rect_t *a,
                                const epd_refresh_rect_t *b,
                                epd_refresh_rect_t *result)
{
    if (!a || !b || !result) return false;

    result->x1 = max_i32(a->x1, b->x1);
    result->y1 = max_i32(a->y1, b->y1);
    result->x2 = min_i32(a->x2, b->x2);
    result->y2 = min_i32(a->y2, b->y2);
    return result->x1 <= result->x2 && result->y1 <= result->y2;
}

bool epd_refresh_choose_window(const epd_refresh_rect_t *changed,
                               bool input_only,
                               const epd_refresh_rect_t *input_area,
                               epd_refresh_rect_t *window)
{
    if (!changed || !window) return false;
    if (!input_only) {
        *window = *changed;
        return true;
    }
    epd_refresh_rect_t intersection;
    if (!input_area ||
        !epd_refresh_rect_intersect(changed, input_area, &intersection)) {
        return false;
    }

    /* Differential EPD addressing must remain stable across glyph shapes.
     * The framebuffer still contains only the latest pixels, but the panel
     * always receives the same textarea window. */
    *window = *input_area;
    return true;
}
