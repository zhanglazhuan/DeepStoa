#include "touch_gestures.h"
#include "ft6336u_report.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    {
        TouchGestures back;
        assert(back.Update(0, 1, 30, 20) == -1);
        assert(back.Update(80, 0) == 8);
        assert(back.Update(500, 0) == -1);
        // Dragging out of the button never confirms or goes back.
        assert(back.Update(600, 1, 30, 20) == -1);
        assert(back.Update(700, 1, 150, 120) == -1);
        assert(back.Update(800, 0) == -1);
        // Back cancels a pending content tap instead of confirming first.
        assert(back.Update(900, 1, 100, 100) == -1);
        assert(back.Update(980, 0) == -1);
        assert(back.Update(1050, 1, 30, 20) == -1);
        assert(back.Update(1130, 0) == 8);
        assert(back.Update(1500, 0) == -1);
    }
    // Single tap waits for double-tap window; no premature confirm on back.
    TouchGestures g;
    assert(g.Update(0, 1, 100, 100) == -1);
    assert(g.Update(80, 0) == -1);
    assert(g.Update(400, 0) == -1);
    assert(g.Update(431, 0) == 7);
    assert(g.Update(450, 0) == -1);
    g = {};
    assert(g.Update(0, 1, 100, 100) == -1);
    assert(g.Update(80, 0) == -1);
    assert(g.Update(200, 1, 110, 110) == -1);
    assert(g.Update(280, 0) == 8);
    assert(g.Update(900, 0) == -1);
    // Both axes: finger moves up/left -> next; down/right -> previous.
    for (int axis = 0; axis < 2; ++axis) {
        for (int delta : {-100, 100}) {
            g = {};
            assert(g.Update(0, 1, 200, 200) == -1);
            assert(g.Update(100, 1, 200 + (axis ? 0 : delta),
                            200 + (axis ? delta : 0)) == -1);
            assert(g.Update(200, 0) == (delta < 0 ? 14 : 0));
            assert(g.Update(1000, 0) == -1);
        }
    }
    g = {};
    assert(g.Update(0, 1, 100, 100) == -1);
    assert(g.Update(1000, 1, 100, 100) == -1);
    assert(g.Update(1100, 0) == 12);
    assert(g.Update(2000, 0) == -1);
    // Bus failure must not turn a held finger into a false release/click.
    g = {};
    g.Update(0, 1, 100, 100);
    g.Cancel();
    assert(g.Update(500, 1, 100, 100) == -1);
    assert(g.Update(600, 0) == -1);
    assert(g.Update(1000, 0) == -1);
    assert(g.Update(1100, 1, 100, 100) == -1);
    assert(g.Update(1200, 0) == -1);
    assert(g.Update(1600, 0) == 7);
    // Multitouch and tracking-ID replacement cancel until all fingers lift.
    for (bool multiple : {false, true}) {
        g = {};
        g.Update(0, 1, 100, 100, 0);
        assert(g.Update(100, multiple ? 2 : 1, 100, 100, 1) == -1);
        assert(g.Update(200, 1, 100, 100) == -1);
        assert(g.Update(300, 0) == -1);
        assert(g.Update(1000, 0) == -1);
    }
    // A drag that returns to its origin must not become a tap.
    g = {};
    g.Update(0, 1, 100, 100);
    g.Update(100, 1, 200, 100);
    g.Update(200, 1, 100, 100);
    assert(g.Update(300, 0) == -1);
    assert(g.Update(1000, 0) == -1);

    uint8_t report[13] = {1, 0x01, 0x23, 0x24, 0x56, 9};
    Ft6336Point points[2]; uint8_t count;
    assert(DecodeFt6336Report(report, sizeof(report), points, count));
    assert(count == 1 && points[0].x == 0x123 && points[0].y == 0x456);
    assert(points[0].id == 2 && points[0].pressure == 9);
    report[1] |= 0x80; // contact
    assert(DecodeFt6336Report(report, sizeof(report), points, count) && count == 1);
    report[1] = 0x40; // lift-up
    assert(DecodeFt6336Report(report, sizeof(report), points, count) && count == 0);
    report[1] = 0xc0; // reserved
    assert(!DecodeFt6336Report(report, sizeof(report), points, count) && count == 0);
    report[0] = 3;
    assert(!DecodeFt6336Report(report, sizeof(report), points, count));
    report[0] = 2; report[1] = 0; report[7] = 0x80; report[9] = 0x10;
    assert(DecodeFt6336Report(report, sizeof(report), points, count) && count == 2);
    assert(points[1].id == 1);
    report[0] = 0;
    assert(DecodeFt6336Report(report, sizeof(report), points, count) && count == 0);
    assert(points[0].x == 0 && points[1].id == 0);
    assert(!DecodeFt6336Report(report, 12, points, count));
    puts("Touch report and gesture tests passed");
}
