#pragma once
#include <cstdint>
#include <cstdlib>

// Emit existing menu key codes exactly once per gesture, after release.
class TouchGestures {
public:
    // Portrait UI coordinates (480 x 800), shared with the top status bar.
    static bool IsBackButton(int x, int y) {
        return x >= 4 && x < 80 && y >= 4 && y < 40;
    }
    void Cancel() {
        active_ = pending_tap_ = second_tap_ = false;
        blocked_ = true; // Require release after I2C failure or multitouch.
    }
    int Update(int64_t ms, unsigned count, int x = 0, int y = 0, int id = 0) {
        if (count > 1) { Cancel(); return -1; }
        if (blocked_) {
            if (count == 0) blocked_ = false;
            return -1;
        }
        if (count == 1) {
            if (!active_) {
                const bool back_start = IsBackButton(x, y);
                const bool double_start = !back_start && pending_tap_ && ms - tap_at_ <= 350 &&
                    std::abs(x - tap_x_) <= 40 && std::abs(y - tap_y_) <= 40;
                const int previous = pending_tap_ && !double_start && !back_start ? 7 : -1;
                second_tap_ = double_start;
                pending_tap_ = false;
                active_ = true; started_ = ms;
                start_x_ = last_x_ = x; start_y_ = last_y_ = y;
                id_ = id; max_motion_ = 0;
                return previous;
            }
            if (id != id_) { Cancel(); return -1; }
            last_x_ = x; last_y_ = y;
            const int motion = std::abs(x - start_x_) + std::abs(y - start_y_);
            if (motion > max_motion_) max_motion_ = motion;
            return -1;
        }
        if (active_) {
            active_ = false;
            const int dx = last_x_ - start_x_, dy = last_y_ - start_y_;
            const int64_t duration = ms - started_;
            if (IsBackButton(start_x_, start_y_)) {
                second_tap_ = pending_tap_ = false;
                return IsBackButton(last_x_, last_y_) && max_motion_ <= 24 &&
                    duration >= 30 && duration <= 500 ? 8 : -1;
            }
            if (std::abs(dx) >= 50 || std::abs(dy) >= 50) {
                second_tap_ = false;
                const int direction = std::abs(dy) >= std::abs(dx) ? dy : dx;
                return direction < 0 ? 14 : 0;
            }
            if (max_motion_ > 24) { second_tap_ = false; return -1; }
            if (duration >= 800) { second_tap_ = false; return 12; }
            if (duration < 30 || duration > 500) { second_tap_ = false; return -1; }
            if (second_tap_) { second_tap_ = false; return 8; }
            pending_tap_ = true;
            tap_at_ = ms; tap_x_ = last_x_; tap_y_ = last_y_;
        }
        if (pending_tap_ && ms - tap_at_ > 350) {
            pending_tap_ = false;
            return 7;
        }
        return -1;
    }
private:
    bool active_ = false, blocked_ = false, pending_tap_ = false, second_tap_ = false;
    int start_x_ = 0, start_y_ = 0, last_x_ = 0, last_y_ = 0;
    int tap_x_ = 0, tap_y_ = 0, max_motion_ = 0, id_ = 0;
    int64_t started_ = 0, tap_at_ = 0;
};
