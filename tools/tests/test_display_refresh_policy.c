#include <assert.h>
#include <stdio.h>

#include "display_refresh_policy.h"

static void assert_rect(epd_refresh_rect_t actual,
                        int x1, int y1, int x2, int y2)
{
    assert(actual.x1 == x1);
    assert(actual.y1 == y1);
    assert(actual.x2 == x2);
    assert(actual.y2 == y2);
}

int main(void)
{
    const epd_refresh_rect_t input = {16, 147, 463, 194};
    epd_refresh_rect_t window;

    /* Ascenders, descenders and short glyphs must all drive the same panel
     * window; otherwise differential EPD addressing visibly moves text. */
    const epd_refresh_rect_t ascender = {28, 152, 48, 185};
    const epd_refresh_rect_t descender = {50, 160, 70, 192};
    assert(epd_refresh_choose_window(&ascender, true, &input, &window));
    assert_rect(window, 16, 147, 463, 194);
    assert(epd_refresh_choose_window(&descender, true, &input, &window));
    assert_rect(window, 16, 147, 463, 194);

    /* Keyboard animation is entirely outside the input whitelist. */
    const epd_refresh_rect_t keyboard = {0, 542, 479, 791};
    assert(!epd_refresh_choose_window(&keyboard, true, &input, &window));

    /* LVGL may merge textarea and key animations into one tall invalidation;
     * the hardware window must still collapse to the textarea. */
    const epd_refresh_rect_t merged = {16, 144, 479, 791};
    assert(epd_refresh_choose_window(&merged, true, &input, &window));
    assert_rect(window, 16, 147, 463, 194);

    /* Outside input mode, retain the caller's normal dirty rectangle. */
    assert(epd_refresh_choose_window(&ascender, false, NULL, &window));
    assert_rect(window, 28, 152, 48, 185);

    puts("PASS: input refresh window is stable and keyboard redraw is rejected");
    return 0;
}
