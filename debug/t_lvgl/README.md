# LVGL refresh measurement

Build and flash from this directory with the ESP-IDF environment active:

```powershell
idf.py -B build-idf55 build
idf.py -B build-idf55 -p COM4 flash monitor
```

The test uses `debug/t_display/main/epd_display.c` on DeepStoa V1,
10 MHz SPI, and full-frame partial updates. It sends five numbered LVGL
frames, with a 3-second pause between measurements, then accepts tab touches.
It does not measure the production background flush task or its PSRAM path.

Each `[refresh]` record reports milliseconds:

- `convert`: accumulated LVGL pixel-to-panel buffer conversion time.
- `pre_wait`: wait for idle before sending, bounded at 10 seconds.
- `submit`: synchronous driver call, including SPI data, commands and AW9523 DC writes.
- `post_wait`: observation after submission, bounded at 10 seconds.
- `total`: conversion plus pre-wait, submission and post-wait; excludes LVGL rendering and the deliberate pause.

`valid=1` requires idle before transmission and an observed BUSY high-to-low
sequence afterwards. Only then is `transfer+panel rate` printed: reciprocal
of submission plus post-wait, not application FPS or an optical measurement.
BUSY is sampled once per FreeRTOS tick (10 ms with the current config); short
pulses can be missed. `valid=0` means no reliable panel rate was measured.
The debug timeout-and-continue behavior is retained to expose a stuck BUSY
without hanging forever; such runs must not be interpreted as successful
refresh timing. No timeout has been reduced to 100 ms.

## Hardware run, 2026-10-15

Built with ESP-IDF 5.5.3, flashed to COM4 and captured five frames.
Test config: CPU 160 MHz, SPI 10 MHz, internal RAM buffers.
All five frames reported `pre_ready=1 busy_seen=1 released=1 valid=1`.

| Stage | Mean time |
| --- | ---: |
| Pixel conversion | 149.11 ms |
| Pre-transfer idle wait | 0.02 ms |
| Submission (SPI + control) | 42.57 ms |
| Post-transfer BUSY wait | 551.34 ms |
| Sum of measured stages | 743.04 ms |

Submission plus panel completion measured 588.32–597.01 ms, equivalent
to 1.675–1.700 Hz. This is not whole-application FPS: LVGL rendering,
scheduling and the intentional 3-second pauses are excluded.

Startup white clear took 8085.82 ms, with the driver reporting 3860 ms
of BUSY polling. Unlike partial frame submission, that clear function
sends both 48 KB planes byte by byte. Thus startup clear has a separate
transport overhead. Early initialization logs were not fully captured.

The earlier production symptom of BUSY staying high for every 10-second
timeout did not reproduce in this run. This proves the V1 board can report
BUSY completion under this test; it does not establish why production
previously failed. Raw capture: `build-idf55/refresh.log` (local build artifact).

## Production comparison, 2026-10-16

The main firmware was rebuilt, flashed and measured on the same COM4 board.
It reproduced the fault: hardware initialization 30155.57 ms, initial clear
12785.11 ms, frame pre-wait about 9997 ms, snapshot about 8.06 ms, submission
43.61–43.99 ms. BUSY was already stuck before the first SPI command and
before Wi-Fi/audio/app initialization. Raw capture: `../../build/production-refresh.log`.
Temporary production timing instrumentation was removed after capture.

An A/B build option now permits the exact production driver in this harness:

```powershell
idf.py -B build-idf55 -DT_LVGL_PRODUCTION_DRIVER=ON build
```

All five frames with that driver completed normally (submission 42.63–42.65 ms,
post-wait 545.60–555.04 ms). A further run delayed display startup by 2200 ms
to roughly match production's boot time; it also passed all five frames
(submission 42.65–42.66 ms, post-wait 545.46–554.22 ms).
That startup delay is retained in the harness for reproducibility.
The local build cache currently selects the production driver; pass
`-DT_LVGL_PRODUCTION_DRIVER=OFF` to select the debug driver again.

These comparisons exclude display-driver source differences and elapsed
boot time alone as sufficient explanations. They do not yet distinguish
system configuration (single-core/dual-core, PSRAM, console, CPU clock)
from other pre-display initialization effects. The root cause is unresolved.
