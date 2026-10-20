# LXJ Clock

This archived area contains the former LXJ Clock adapter and a source snapshot
from `refer/esp32_paper-v2.0/main/page_clock`. The adapter displayed the current
time and a simple SNTP sync label. The current time is visible in the global
status bar; `apps/clock` owns alarms and timers. The separate LXJ Clock entry
is retired.

The files under `legacy/` are a source snapshot only. They are deliberately
not part of the ESP-IDF component graph and are not registered with
`app_manager` yet. The reference implementation owns the e-paper bus,
button polling loop, RTC access, and refresh lifecycle directly; compiling it
inside the current firmware before those boundaries are adapted would allow
two owners to drive the same display.

The archived `CMakeLists.txt` is no longer in the firmware component graph.

