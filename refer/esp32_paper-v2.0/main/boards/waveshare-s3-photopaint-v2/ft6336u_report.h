#pragma once
#include <cstddef>
#include <cstdint>

struct Ft6336Point {
    uint16_t x = 0, y = 0;
    uint8_t pressure = 0, id = 0;
};

// Coherent read from TD_STATUS (0x02) through both six-byte point slots.
inline bool DecodeFt6336Report(const uint8_t *data, size_t size,
                              Ft6336Point (&points)[2], uint8_t &count) {
    count = 0;
    points[0] = {}; points[1] = {};
    if (!data || size < 13 || (data[0] & 0x0f) > 2) return false;
    const unsigned reported = data[0] & 0x0f;
    for (unsigned i = 0; i < reported; ++i) {
        const uint8_t *p = data + 1 + 6 * i;
        const unsigned event = p[0] >> 6;
        if (event == 3) { count = 0; return false; }
        if (event == 1) continue; // lift-up; 0=down, 2=contact
        points[count++] = {static_cast<uint16_t>(((p[0] & 15) << 8) | p[1]),
                           static_cast<uint16_t>(((p[2] & 15) << 8) | p[3]),
                           p[4], static_cast<uint8_t>(p[2] >> 4)};
    }
    return true;
}
