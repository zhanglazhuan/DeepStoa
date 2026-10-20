# DeepStoa V1 sustained-load / temperature test

Standalone ESP-IDF project for the V1 board. It enables dual-core operation,
240 MHz CPU, octal PSRAM, 4-bit SDMMC, the e-paper panel and ES8311 speaker.
It runs two pinned CPU workers, sequentially reads a 4 MiB test file from SD,
plays a quiet 880 Hz tone, and alternates full-screen e-paper partial updates.

Defaults: 10 minutes, log the ESP32-S3 on-die temperature every 2 seconds,
stop the load at 80 °C, and mute/close audio at the end. The sensor is on-die,
not a case-temperature measurement; use an external thermometer for the
enclosure. Its configured 20–100 °C range has the ESP-IDF stated error bound
of less than 2 °C.

The program never formats the card. It creates
`/sdcard/.deepstoa_stress/read.bin` (4 MiB) only when absent, reads it in a
loop, and removes it after a normal run if this run created it. If power is
cut, that file may remain; the next run reuses it. The SD card needs at least
4 MiB free. Other files are left untouched.

Build and flash:

```powershell
. C:/Espressif/frameworks/esp-idf-v5.5.3/export.ps1
idf.py build
idf.py -p COM4 flash monitor
```

The device logs `LOAD STARTED`, periodic temperature/memory, SD throughput,
and display submissions. Press RESET to stop early. Run it on a hard, open,
nonflammable surface; do not leave it unattended. The firmware stops itself
at 80 °C on the chip sensor even if the 10-minute timer has not elapsed.
