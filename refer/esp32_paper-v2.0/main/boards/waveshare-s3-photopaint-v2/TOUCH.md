# YRD04MDW0670B touch integration

The fitted module is documented as FT6336U. This board has previously responded
at the **7-bit address 0x2E**. The vendor `app.bin` / `all.bin` files are touch
controller firmware, not ESP32 images; this integration does not flash them.

## Wiring

The module drawing and board schematic agree on J9 pins 1=GND, 2=3.3V,
4=INT, 5=SDA (ESP32 GPIO17), 6=SCL (GPIO18). Pin 3 is RST on the module but
unconnected on the board schematic. Software cannot pulse this reset line.
If the touch controller cannot start, verify its reset level against the
module requirements. INT is connected to AW9523 P0.5; the driver polls directly
over the shared I2C bus and does not need AW9523 interrupt reads.

## Existing-page controls

| Gesture | Existing key action |
| --- | --- |
| Swipe up or left, at least 50 coordinate units | DOWN / next, code 14 |
| Swipe down or right | UP / previous, code 0 |
| Tap | CONFIRM, code 7 |
| Double tap in the same area | Confirm double-click / back, code 8 |
| Hold at least 800 ms, then release | Confirm long-press, code 12 |

These gestures act on the current selection; they do **not** hit-test individual
icons or implement arbitrary drawing. The existing page decides what each key
does. Single tap waits 350 ms after release to distinguish double tap. Multitouch,
tracking-ID changes and failed I2C reads cancel the gesture until a clean release.

`TOUCH_SWAP_XY`, `TOUCH_INVERT_X`, `TOUCH_INVERT_Y` in `config.h` control gesture
orientation. The default assumes native axes align with the portrait UI; this
requires physical verification on the fitted panel. Coordinate logs show both
raw and transformed positions. No absolute-coordinate calibration is inferred
from the mechanical drawing.

The 20 ms polling task posts to the same menu consumer as hardware keys. It never
draws or launches a page. Firmware built from this workspace also includes the
earlier independent key sampler, short AW9523 locking and queued key events.
Touch uses a one-item mailbox: a new gesture replaces the previous unprocessed
gesture during slow refresh. This avoids replaying many stale confirms across
pages. Hardware keys retain their existing 32-item queue. Display updates still
wait for synchronous transfer; this does not remove the AW9523 bandwidth limit.

## Verification

Host tests (from project root):

```sh
g++ -std=c++17 -Wall -Wextra -Werror \
  -I main/boards/waveshare-s3-photopaint-v2 \
  tests/touch_gestures_test.cc -o /tmp/paw-touch-gestures-test
/tmp/paw-touch-gestures-test
```

Runtime checkpoints:

1. `FT6336U: Touch ready` reports address, A3 chip ID and A6 firmware version.
2. `TOUCH points=... raw=... ui=...` proves successful contact sampling.
3. `TOUCH gesture -> key=...` and `KEY_QUEUE: Latest touch action=...` prove gesture routing.
4. `KEY_QUEUE: Consumed action=...` / `home: Received key=...` prove page delivery.
5. `Frame transfer: .../48000` shows transfer progress; `Base frame sent` marks
   refresh triggering. `Base refresh wait returned` is not proof of visual
   success: also check for BUSY timeout and observe the actual display.

Initialization fails cleanly on absent hardware/configuration errors and leaves
physical buttons available. Factory touch sensitivity settings are retained.
